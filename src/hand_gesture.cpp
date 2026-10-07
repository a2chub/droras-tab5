#include "hand_gesture.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>

#include "driver/i2c_master.h"
#include "driver/ppa.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "esp_video_isp_ioctl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linux/videodev2.h"

#include "dl_image_define.hpp"
#include "dl_image_jpeg.hpp"
#include "hand_detect.hpp"
#include "hand_gesture_recognition.hpp"
#include "ui_common.h"

namespace hand_gesture {

namespace {

using heatboard::BoardAction;
using heatboard::Gesture;
using heatboard::GestureSettings;

const char* TAG = "gesture";

// The Tab5's internal I2C bus (touch panel, IMU, IO expanders and the camera sensor), as
// M5Unified sets it up for this board.
constexpr i2c_port_t kI2cPort = I2C_NUM_1;
constexpr gpio_num_t kI2cSda = GPIO_NUM_31;
constexpr gpio_num_t kI2cScl = GPIO_NUM_32;
constexpr uint32_t kSccbFrequencyHz = 400000;

// Two frames: one being filled by the ISP while the other is being analysed.
constexpr int kFrameBuffers = 2;

// The models look at the whole frame scaled to a quarter. The detector wants a hand between
// roughly a tenth and a half of the image (measured with Espressif's sample: a hand filling
// most of the frame is not found); at a quarter of 1280x720, a hand at arm's length (0.5 to
// 1.5 m) is a quarter of the width, like the sample's.
//
// The quarter is reached in two steps: the PPA halves the frame (its scale must be a
// multiple of 1/16, anything else leaves unwritten stripes at the edges), then 2x2 pixels
// are averaged on the CPU. The averaging matters: at the high sensor gain of a dim hall the
// picture is grainy, and the detector misses a hand in grain that plain sub-sampling keeps.
constexpr int kHalfScale = 2;
constexpr int kHalfWidth = 1280 / kHalfScale;
constexpr int kHalfHeight = 720 / kHalfScale;
constexpr std::size_t kHalfPixels = static_cast<std::size_t>(kHalfWidth) * kHalfHeight;
constexpr int kAnalysisScale = 2 * kHalfScale;
constexpr int kAnalysisWidth = 1280 / kAnalysisScale;
constexpr int kAnalysisHeight = 720 / kAnalysisScale;
constexpr std::size_t kAnalysisPixels = static_cast<std::size_t>(kAnalysisWidth) * kAnalysisHeight;

// Classifier results below this are ignored: the hand is there but the gesture unclear.
constexpr float kMinGestureScore = 0.6f;

// Espressif's sample picture for these models (esp-dl, examples/hand_gesture_recognition).
extern "C" const uint8_t sampleJpegStart[] asm("_binary_gesture_jpg_start");
extern "C" const uint8_t sampleJpegEnd[] asm("_binary_gesture_jpg_end");

i2c_master_bus_handle_t i2cBus = nullptr;
std::atomic<bool> frameDumpRequested{false};
std::atomic<bool> selfTestRequested{false};
// Debug console "gest": a gesture to report instead of the models' result, until when.
std::atomic<int> injectedGesture{-1};
std::atomic<int64_t> injectedUntilMs{0};

struct Camera {
  int fd = -1;
  int ispFd = -1;
  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t* buffers[kFrameBuffers] = {};
  ppa_client_handle_t ppa = nullptr;
  uint16_t* half = nullptr;      // kHalfWidth x kHalfHeight RGB565, the PPA's output
  uint16_t* analysis = nullptr;  // kAnalysisWidth x kAnalysisHeight RGB565
};
Camera camera;
bool running = false;

// Shared between the recognition task and the main task.
std::mutex sharedMutex;
GestureSettings sharedSettings;
Status sharedStatus;
std::optional<BoardAction> pendingAction;
std::atomic<bool> previewOn{false};
uint16_t* previewBuffer = nullptr;  // kAnalysisWidth x kAnalysisHeight, guarded by sharedMutex
uint32_t previewSequence = 0;

static_assert(kPreviewWidth == kAnalysisWidth && kPreviewHeight == kAnalysisHeight);

int64_t nowMs() { return esp_timer_get_time() / 1000; }

// --- Exposure ----------------------------------------------------------------------------
// Brightness of the middle of the latest analysed image (0..255, green channel), written by
// the recognition task; -1 until the first frame. The mean sets the exposure; the bright
// end (95th percentile) caps it, so that a lit hand in front of a dark room is not burnt to
// white, which the classifier does not recognise as skin.
std::atomic<int> frameBrightness{-1};
std::atomic<int> frameHighlights{0};
constexpr int kHighlightLimit = 235;

struct ControlRange {
  int min = 0;
  int max = 0;
  int value = 0;
};
ControlRange exposureRange;  // sensor lines
ControlRange gainRange;      // index into the sensor's gain table, about +3% per step
bool autoExposure = true;
int64_t lastExposureStepMs = 0;

constexpr int kBrightnessTarget = 110;
constexpr int kBrightnessDeadband = 20;
constexpr int64_t kExposureStepIntervalMs = 250;
// Gain table steps per doubling of the gain (1.031 per step).
constexpr float kGainStepsPerStop = 22.7f;

bool queryControl(uint32_t id, const char* name, ControlRange& range) {
  // esp_video answers the extended query only.
  v4l2_query_ext_ctrl query = {};
  query.id = id;
  if (ioctl(camera.fd, VIDIOC_QUERY_EXT_CTRL, &query) != 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERY_EXT_CTRL %s failed", name);
    return false;
  }
  range.min = static_cast<int>(query.minimum);
  range.max = static_cast<int>(query.maximum);
  range.value = static_cast<int>(query.default_value);
  ESP_LOGI(TAG, "%s: %d..%d, default %d", name, range.min, range.max, range.value);
  return true;
}

bool setControl(int fd, uint32_t controlClass, uint32_t id, const char* name, int value) {
  v4l2_ext_control control = {};
  control.id = id;
  control.value = value;
  v4l2_ext_controls controls = {};
  controls.ctrl_class = controlClass;
  controls.count = 1;
  controls.controls = &control;
  if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
    ESP_LOGE(TAG, "setting %s to %d failed", name, value);
    return false;
  }
  return true;
}

bool setExtControl(int fd, uint32_t id, const char* name, void* payload) {
  v4l2_ext_control control = {};
  control.id = id;
  control.p_u8 = static_cast<uint8_t*>(payload);
  v4l2_ext_controls controls = {};
  controls.ctrl_class = V4L2_CTRL_CLASS_USER;
  controls.count = 1;
  controls.controls = &control;
  if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
    ESP_LOGW(TAG, "ISP %s could not be set", name);
    return false;
  }
  return true;
}

bool applyExposure(int exposure, int gain) {
  exposure = std::clamp(exposure, exposureRange.min, exposureRange.max);
  gain = std::clamp(gain, gainRange.min, gainRange.max);
  const bool ok =
      (exposure == exposureRange.value ||
       setControl(camera.fd, V4L2_CID_CAMERA_CLASS, V4L2_CID_EXPOSURE, "exposure", exposure)) &&
      (gain == gainRange.value || setControl(camera.fd, V4L2_CID_USER_CLASS, V4L2_CID_GAIN, "gain", gain));
  exposureRange.value = exposure;
  gainRange.value = gain;
  return ok;
}

// --- ISP ----------------------------------------------------------------------------------
// The sensor is RAW: without correction the picture is green. Grey-world balance: the
// ISP's red and blue gains are nudged until the three channels average the same. Measured
// on the whole frame: a hand filling the frame would be balanced to grey, and skin that is
// not skin-coloured is what the models do not recognise.
constexpr int64_t kWhiteBalanceIntervalMs = 1000;
int64_t lastWhiteBalanceMs = 0;
float redGain = 1.0f;
float blueGain = 1.0f;

void balanceWhite(const uint16_t* pixels, int width, int height) {
  if (camera.ispFd < 0) {
    return;
  }
  constexpr int kStep = 16;
  uint32_t sumR = 0;
  uint32_t sumG = 0;
  uint32_t sumB = 0;
  for (int y = 0; y < height; y += kStep) {
    const uint16_t* row = pixels + static_cast<std::size_t>(y) * width;
    for (int x = 0; x < width; x += kStep) {
      const uint16_t p = row[x];
      sumR += (p >> 11) & 0x1F;
      sumG += (p >> 6) & 0x1F;  // top 5 of the 6 green bits
      sumB += p & 0x1F;
    }
  }
  if (sumR == 0 || sumG == 0 || sumB == 0) {
    return;
  }
  // Half-way towards the grey-world gains each time, so a coloured object passing by does
  // not swing the picture.
  const float targetRed = redGain * static_cast<float>(sumG) / sumR;
  const float targetBlue = blueGain * static_cast<float>(sumG) / sumB;
  redGain = std::clamp((redGain + targetRed) / 2.0f, 1.0f, 2.5f);
  blueGain = std::clamp((blueGain + targetBlue) / 2.0f, 1.0f, 2.5f);
  setControl(camera.ispFd, V4L2_CTRL_CLASS_USER, V4L2_CID_RED_BALANCE, "red balance",
             static_cast<int>(redGain * V4L2_CID_RED_BALANCE_DEN));
  setControl(camera.ispFd, V4L2_CTRL_CLASS_USER, V4L2_CID_BLUE_BALANCE, "blue balance",
             static_cast<int>(blueGain * V4L2_CID_BLUE_BALANCE_DEN));
}

// ISP processing that helps the models in poor light: the Bayer filter smooths the noise
// of a high sensor gain, and a gamma curve lifts the shadows, so a hand in a dim hall is
// not a dark blur. Set on the ISP device, which involves no I2C.
void configureIsp() {
  if (camera.ispFd < 0) {
    return;
  }
  esp_video_isp_bf_t bf = {};
  bf.enable = true;
  bf.level = 16;
  const uint8_t weights[3][3] = {{1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
  for (int y = 0; y < 3; ++y) {
    for (int x = 0; x < 3; ++x) {
      bf.matrix[y][x] = weights[y][x];
    }
  }
  setExtControl(camera.ispFd, V4L2_CID_USER_ESP_ISP_BF, "denoise", &bf);

  // y = 255 * (x / 255) ^ (1 / 1.6), on the 16 points the hardware takes: 16 apart, the
  // last one at 255 (the spacing has to be a power of two).
  esp_video_isp_gamma_t gamma = {};
  gamma.enable = true;
  for (int i = 0; i < ISP_GAMMA_CURVE_POINTS_NUM; ++i) {
    const int x = i == ISP_GAMMA_CURVE_POINTS_NUM - 1 ? 255 : i * 16;
    gamma.points[i].x = static_cast<uint8_t>(x);
    gamma.points[i].y = static_cast<uint8_t>(std::lround(255.0 * std::pow(x / 255.0, 1.0 / 1.6)));
  }
  setExtControl(camera.ispFd, V4L2_CID_USER_ESP_ISP_GAMMA, "gamma", &gamma);
}

// Green channel of every 4th pixel in both directions, 0..252, over the middle half of the
// picture: that is where the operator stands, and a bright window or lamp at the edge
// would otherwise drive the exposure down and leave the hand a dark shape. Returns the
// mean; `highlights` receives the value the brightest 5% of pixels exceed.
int measureBrightness(const uint16_t* pixels, int width, int height, int& highlights) {
  constexpr int kStep = 4;
  uint32_t sum = 0;
  int count = 0;
  int histogram[64] = {};
  for (int y = height / 4; y < height * 3 / 4; y += kStep) {
    const uint16_t* row = pixels + static_cast<std::size_t>(y) * width;
    for (int x = width / 4; x < width * 3 / 4; x += kStep) {
      const int green = (row[x] >> 5) & 0x3F;
      sum += green << 2;
      ++histogram[green];
      ++count;
    }
  }
  highlights = 0;
  int above = 0;
  for (int bin = 63; bin >= 0; --bin) {
    above += histogram[bin];
    if (above >= count / 20) {
      highlights = bin << 2;
      break;
    }
  }
  return count > 0 ? static_cast<int>(sum / count) : 0;
}

// --- Camera -------------------------------------------------------------------------------

// Opens the capture device and asks the ISP for RGB565 at the sensor's resolution.
bool openCamera() {
  camera.fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
  if (camera.fd < 0) {
    ESP_LOGE(TAG, "cannot open %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
    return false;
  }

  v4l2_capability capability = {};
  if (ioctl(camera.fd, VIDIOC_QUERYCAP, &capability) != 0) {
    ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed");
    return false;
  }
  ESP_LOGI(TAG, "camera driver %s, card %s", capability.driver, capability.card);

  v4l2_format format = {};
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(camera.fd, VIDIOC_G_FMT, &format) != 0) {
    ESP_LOGE(TAG, "VIDIOC_G_FMT failed");
    return false;
  }
  format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
  if (ioctl(camera.fd, VIDIOC_S_FMT, &format) != 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT (RGB565) failed");
    return false;
  }
  camera.width = format.fmt.pix.width;
  camera.height = format.fmt.pix.height;
  ESP_LOGI(TAG, "capturing %ux%u RGB565", static_cast<unsigned>(camera.width), static_cast<unsigned>(camera.height));
  if (camera.width != kAnalysisWidth * kAnalysisScale || camera.height != kAnalysisHeight * kAnalysisScale) {
    ESP_LOGE(TAG, "unexpected frame size; the analysis image is laid out for 1280x720");
    return false;
  }

  v4l2_requestbuffers request = {};
  request.count = kFrameBuffers;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (ioctl(camera.fd, VIDIOC_REQBUFS, &request) != 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
    return false;
  }
  for (int i = 0; i < kFrameBuffers; ++i) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = i;
    if (ioctl(camera.fd, VIDIOC_QUERYBUF, &buffer) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF %d failed", i);
      return false;
    }
    camera.buffers[i] = static_cast<uint8_t*>(
        mmap(nullptr, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED, camera.fd, buffer.m.offset));
    if (camera.buffers[i] == nullptr) {
      ESP_LOGE(TAG, "mmap of buffer %d failed", i);
      return false;
    }
    if (ioctl(camera.fd, VIDIOC_QBUF, &buffer) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF %d failed", i);
      return false;
    }
  }

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(camera.fd, VIDIOC_STREAMON, &type) != 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
    return false;
  }

  // White balance, denoise and gamma live on the ISP device. Not fatal: the picture is
  // then green-tinted and noisier.
  camera.ispFd = open(ESP_VIDEO_ISP1_DEVICE_NAME, O_RDONLY);
  if (camera.ispFd < 0) {
    ESP_LOGW(TAG, "cannot open %s; no white balance", ESP_VIDEO_ISP1_DEVICE_NAME);
  }
  configureIsp();
  return true;
}

bool setUpAnalysisImage() {
  // The PPA writes its output with DMA: that buffer has to be cache-line aligned in size
  // and address (128 bytes covers both cache levels).
  camera.half = static_cast<uint16_t*>(
      heap_caps_aligned_alloc(128, kHalfPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));
  camera.analysis = static_cast<uint16_t*>(heap_caps_malloc(kAnalysisPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (camera.half == nullptr || camera.analysis == nullptr) {
    ESP_LOGE(TAG, "no memory for the analysis image");
    return false;
  }
  ppa_client_config_t config = {};
  config.oper_type = PPA_OPERATION_SRM;
  config.max_pending_trans_num = 1;
  const esp_err_t err = ppa_register_client(&config, &camera.ppa);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PPA client failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

// Averages 2x2 blocks of `source` (w x h, RGB565) into `dest` (w/2 x h/2).
void averagePairs(const uint16_t* source, int w, int h, uint16_t* dest) {
  for (int y = 0; y < h / 2; ++y) {
    const uint16_t* top = source + static_cast<std::size_t>(2 * y) * w;
    const uint16_t* bottom = top + w;
    uint16_t* out = dest + static_cast<std::size_t>(y) * (w / 2);
    for (int x = 0; x < w / 2; ++x, top += 2, bottom += 2) {
      const uint16_t p[4] = {top[0], top[1], bottom[0], bottom[1]};
      int r = 0;
      int g = 0;
      int b = 0;
      for (const uint16_t v : p) {
        r += v >> 11;
        g += (v >> 5) & 0x3F;
        b += v & 0x1F;
      }
      out[x] = static_cast<uint16_t>(((r + 2) / 4 << 11) | ((g + 2) / 4 << 5) | (b + 2) / 4);
    }
  }
}

// Shrinks `frame` into camera.analysis, turned so that the operator's hand is upright: the
// sensor is mounted head-down relative to the landscape UI (measured: a face in front of
// the normal way up came out inverted), and when the Tab5 itself is turned upside down the
// two cancel out.
bool makeAnalysisImage(const uint8_t* frame) {
  ppa_srm_oper_config_t op = {};
  op.in.buffer = frame;
  op.in.pic_w = camera.width;
  op.in.pic_h = camera.height;
  op.in.block_w = camera.width;
  op.in.block_h = camera.height;
  op.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op.out.buffer = camera.half;
  op.out.buffer_size = kHalfPixels * sizeof(uint16_t);
  op.out.pic_w = kHalfWidth;
  op.out.pic_h = kHalfHeight;
  op.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  op.rotation_angle = ui::upsideDown() ? PPA_SRM_ROTATION_ANGLE_0 : PPA_SRM_ROTATION_ANGLE_180;
  op.scale_x = 1.0f / kHalfScale;
  op.scale_y = 1.0f / kHalfScale;
  op.mode = PPA_TRANS_MODE_BLOCKING;
  const esp_err_t err = ppa_do_scale_rotate_mirror(camera.ppa, &op);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "PPA scale failed: %s", esp_err_to_name(err));
    return false;
  }
  averagePairs(camera.half, kHalfWidth, kHalfHeight, camera.analysis);
  return true;
}

dl::image::img_t rgb565Image(void* data, int width, int height) {
  dl::image::img_t image = {};
  image.data = data;
  image.width = static_cast<uint16_t>(width);
  image.height = static_cast<uint16_t>(height);
  image.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;
  return image;
}

// --- Debugging aids -----------------------------------------------------------------------

// Same line format as debug_console::dumpScreenshot(), so the same decoder reads it.
// Written straight to the serial driver, waiting for room: through stdio a long row is
// split into pieces, and pieces went missing.
void writeSerial(const std::string& text) {
  usb_serial_jtag_write_bytes(text.data(), text.size(), portMAX_DELAY);
}

void dumpImage(const uint16_t* pixels, int width, int height) {
  std::string line;
  char header[32];
  snprintf(header, sizeof(header), "\n@SHOT %d %d\n", width, height);
  writeSerial(header);
  for (int y = 0; y < height; ++y) {
    const uint16_t* row = pixels + static_cast<std::size_t>(y) * width;
    line.assign("@R ");
    int x = 0;
    while (x < width) {
      int run = 1;
      while (x + run < width && row[x + run] == row[x]) {
        ++run;
      }
      char encoded[9];
      snprintf(encoded, sizeof(encoded), "%04x%04x", run, row[x]);
      line.append(encoded, 8);
      x += run;
    }
    line.push_back('\n');
    writeSerial(line);
  }
  writeSerial("@END\n");
}

void selfTest(HandDetect& detector, HandGestureRecognizer& recognizer) {
  dl::image::jpeg_img_t jpeg = {};
  jpeg.data = const_cast<uint8_t*>(sampleJpegStart);
  jpeg.data_len = static_cast<size_t>(sampleJpegEnd - sampleJpegStart);
  auto sample = dl::image::sw_decode_jpeg(jpeg, dl::image::DL_IMAGE_PIX_TYPE_RGB888);
  if (sample.data == nullptr) {
    ESP_LOGE(TAG, "self test: JPEG decode failed");
    return;
  }
  auto& found = detector.run(sample);
  const auto gestures = recognizer.recognize(sample, found);
  ESP_LOGI(TAG, "self test: %u hand(s) in the sample picture (1 showing \"one\" expected)",
           static_cast<unsigned>(found.size()));
  std::size_t i = 0;
  for (const auto& hand : found) {
    ESP_LOGI(TAG, "self test: box [%d,%d,%d,%d] %.2f -> %s %.2f", hand.box[0], hand.box[1], hand.box[2], hand.box[3],
             hand.score, i < gestures.size() ? gestures[i].cat_name : "?", i < gestures.size() ? gestures[i].score : 0.0f);
    ++i;
  }
  heap_caps_free(sample.data);
}

// --- Recognition task ----------------------------------------------------------------------

// What the models made of the latest frame, for the status.
struct Detection {
  bool handSeen = false;
  int box[4] = {};
  const char* label = "";
};

// The gesture of the most confidently detected hand, when its classification is clear.
Gesture pickGesture(HandDetect& detector, HandGestureRecognizer& recognizer, const dl::image::img_t& image,
                    float& score, Detection& detection) {
  score = 0.0f;
  detection = Detection{};
  auto& hands = detector.run(image);
  if (hands.empty()) {
    return Gesture::None;
  }
  // The classifier is given the detector's boxes as they are: it was trained on such
  // crops, and a box grown to take in missed fingers made it see no hand at all.
  const auto gestures = recognizer.recognize(image, hands);
  std::size_t best = 0;
  std::size_t i = 0;
  float bestHandScore = -1.0f;
  for (const auto& hand : hands) {
    if (hand.score > bestHandScore) {
      bestHandScore = hand.score;
      best = i;
    }
    ++i;
  }
  if (best >= gestures.size()) {
    return Gesture::None;
  }
  {
    const auto& hand = *std::next(hands.begin(), static_cast<long>(best));
    detection.handSeen = true;
    for (int k = 0; k < 4; ++k) {
      detection.box[k] = hand.box[k];
    }
    detection.label = gestures[best].cat_name;
  }
  // What the models make of the hand, at most twice a second: enough to tune distance and
  // lighting from the serial log without drowning it.
  static int64_t lastDetectionLogMs = 0;
  if (nowMs() - lastDetectionLogMs >= 500) {
    lastDetectionLogMs = nowMs();
    const auto& hand = *std::next(hands.begin(), static_cast<long>(best));
    ESP_LOGI(TAG, "hand [%d,%d,%d,%d] %.2f -> %s %.2f", hand.box[0], hand.box[1], hand.box[2], hand.box[3], hand.score,
             gestures[best].cat_name, gestures[best].score);
  }
  score = gestures[best].score;
  if (score < kMinGestureScore) {
    return Gesture::None;
  }
  return heatboard::gestureFromLabel(gestures[best].cat_name);
}

void run(void*) {
  // Model weights live in flash; the working buffers go to PSRAM inside ESP-DL.
  HandDetect detector;
  HandGestureRecognizer recognizer(HandGestureCls::MOBILENETV2_0_5_S8_V1);
  ESP_LOGI(TAG, "models loaded, free heap %u", static_cast<unsigned>(esp_get_free_heap_size()));

  heatboard::GestureTrigger trigger;
  int64_t lastReportMs = nowMs();
  int frames = 0;
  int64_t lastDetectMs = 0;
  Gesture lastLogged = Gesture::None;
  while (true) {
    v4l2_buffer buffer = {};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (ioctl(camera.fd, VIDIOC_DQBUF, &buffer) != 0) {
      ESP_LOGE(TAG, "VIDIOC_DQBUF failed; stopping");
      break;
    }
    const bool scaled = makeAnalysisImage(camera.buffers[buffer.index]);
    if (nowMs() - lastWhiteBalanceMs >= kWhiteBalanceIntervalMs) {
      lastWhiteBalanceMs = nowMs();
      balanceWhite(reinterpret_cast<const uint16_t*>(camera.buffers[buffer.index]), static_cast<int>(camera.width),
                   static_cast<int>(camera.height));
    }
    // The frame itself is not needed beyond this point.
    if (ioctl(camera.fd, VIDIOC_QBUF, &buffer) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF failed; stopping");
      break;
    }
    if (!scaled) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (frameDumpRequested.exchange(false)) {
      dumpImage(camera.analysis, kAnalysisWidth, kAnalysisHeight);
    }
    if (selfTestRequested.exchange(false)) {
      selfTest(detector, recognizer);
    }
    {
      int highlights = 0;
      frameBrightness = measureBrightness(camera.analysis, kAnalysisWidth, kAnalysisHeight, highlights);
      frameHighlights = highlights;
    }

    GestureSettings settings;
    {
      std::lock_guard<std::mutex> lock(sharedMutex);
      settings = sharedSettings;
      if (previewOn && previewBuffer != nullptr) {
        std::memcpy(previewBuffer, camera.analysis, kAnalysisPixels * sizeof(uint16_t));
        ++previewSequence;
      }
    }
    if (!settings.enabled && !previewOn) {
      // The picture keeps flowing (exposure, camshot) but nothing is analysed.
      trigger.reset();
      {
        std::lock_guard<std::mutex> lock(sharedMutex);
        sharedStatus = Status{};
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    const dl::image::img_t image = rgb565Image(camera.analysis, kAnalysisWidth, kAnalysisHeight);
    const int64_t startedMs = nowMs();
    float score = 0.0f;
    Detection detection;
    Gesture seen = pickGesture(detector, recognizer, image, score, detection);
    lastDetectMs = nowMs() - startedMs;
    const int64_t now = nowMs();
    if (injectedGesture >= 0) {
      if (now < injectedUntilMs) {
        seen = heatboard::gestureFromInt(injectedGesture).value_or(Gesture::None);
        score = 1.0f;
      } else {
        injectedGesture = -1;
      }
    }
    const auto action = trigger.update(seen, now, settings);
    if (seen != lastLogged) {
      ESP_LOGI(TAG, "seen %d (%.2f)", static_cast<int>(seen), score);
      lastLogged = seen;
    }
    {
      std::lock_guard<std::mutex> lock(sharedMutex);
      sharedStatus.seen = seen;
      sharedStatus.seenScore = score;
      sharedStatus.seenLabel = detection.label;
      sharedStatus.holding = trigger.holding();
      sharedStatus.progress = trigger.progress(now, settings);
      sharedStatus.handSeen = detection.handSeen;
      for (int k = 0; k < 4; ++k) {
        sharedStatus.handBox[k] = detection.box[k];
      }
      if (action) {
        ESP_LOGI(TAG, "gesture %d triggered action %d", static_cast<int>(seen), static_cast<int>(*action));
        pendingAction = action;
      }
    }

    ++frames;
    if (now - lastReportMs >= 10000) {
      ESP_LOGI(TAG,
               "%d frames analysed in 10 s (%d ms each, brightness %d, highlights %d, exposure %d, gain %d, wb %.2f/%.2f)",
               frames, static_cast<int>(lastDetectMs), frameBrightness.load(), frameHighlights.load(),
               exposureRange.value, gainRange.value, redGain, blueGain);
      frames = 0;
      lastReportMs = now;
    }
  }
  running = false;
  vTaskDelete(nullptr);
}

}  // namespace

bool prepareSharedI2c() {
  i2c_master_bus_config_t config = {};
  config.i2c_port = kI2cPort;
  config.sda_io_num = kI2cSda;
  config.scl_io_num = kI2cScl;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  const esp_err_t err = i2c_new_master_bus(&config, &i2cBus);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "shared I2C bus failed: %s", esp_err_to_name(err));
    i2cBus = nullptr;
  }
  return i2cBus != nullptr;
}

void start(const GestureSettings& settings) {
  {
    std::lock_guard<std::mutex> lock(sharedMutex);
    sharedSettings = settings;
  }
  if (i2cBus == nullptr) {
    ESP_LOGW(TAG, "no shared I2C bus; camera disabled");
    return;
  }
  // M5Unified has already taken the sensor out of reset through the IO expander, and the
  // Tab5 feeds the sensor its clock on board, so no pins are left for esp_video to drive.
  esp_video_init_csi_config_t csi = {};
  csi.sccb_config.init_sccb = false;
  csi.sccb_config.i2c_handle = i2cBus;
  csi.sccb_config.freq = kSccbFrequencyHz;
  csi.reset_pin = GPIO_NUM_NC;
  csi.pwdn_pin = GPIO_NUM_NC;
  esp_video_init_config_t config = {};
  config.csi = &csi;
  const esp_err_t err = esp_video_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(err));
    return;
  }
  if (!openCamera() || !setUpAnalysisImage()) {
    return;
  }
  previewBuffer = static_cast<uint16_t*>(heap_caps_malloc(kAnalysisPixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  if (previewBuffer == nullptr) {
    ESP_LOGW(TAG, "no memory for the camera preview");
  }
  if (!queryControl(V4L2_CID_EXPOSURE, "exposure", exposureRange) || !queryControl(V4L2_CID_GAIN, "gain", gainRange)) {
    autoExposure = false;
  }
  // Core 1: the UI and the network task share core 0 by default affinity, and inference
  // keeps a core busy for ~100 ms per frame.
  running = xTaskCreatePinnedToCore(&run, "gesture", 16384, nullptr, 4, nullptr, 1) == pdPASS;
}

bool cameraAvailable() { return running; }

GestureSettings settings() {
  std::lock_guard<std::mutex> lock(sharedMutex);
  return sharedSettings;
}

void setSettings(const GestureSettings& settings) {
  std::lock_guard<std::mutex> lock(sharedMutex);
  sharedSettings = settings;
  if (!settings.enabled) {
    sharedStatus = Status{};
    pendingAction.reset();
  }
}

Status status() {
  std::lock_guard<std::mutex> lock(sharedMutex);
  return running ? sharedStatus : Status{};
}

std::optional<BoardAction> takeAction() {
  std::lock_guard<std::mutex> lock(sharedMutex);
  const auto action = pendingAction;
  pendingAction.reset();
  return action;
}

void requestFrameDump() { frameDumpRequested = true; }

void setPreview(bool on) { previewOn = on; }

uint32_t copyPreview(uint16_t* dest) {
  std::lock_guard<std::mutex> lock(sharedMutex);
  if (previewBuffer == nullptr || previewSequence == 0) {
    return 0;
  }
  std::memcpy(dest, previewBuffer, kAnalysisPixels * sizeof(uint16_t));
  return previewSequence;
}

void injectGesture(int gesture, int durationMs) {
  injectedUntilMs = nowMs() + durationMs;
  injectedGesture = gesture;
}

void requestSelfTest() { selfTestRequested = true; }

void updateExposure() {
  if (camera.fd < 0 || !autoExposure) {
    return;
  }
  const int64_t now = nowMs();
  if (now - lastExposureStepMs < kExposureStepIntervalMs) {
    return;
  }
  const int brightness = frameBrightness.load();
  if (brightness < 0) {
    return;
  }
  lastExposureStepMs = now;
  const int highlights = frameHighlights.load();
  float stops = 0.0f;
  if (highlights > kHighlightLimit) {
    // Burning out: come down a third of a stop at a time until the bright end fits.
    stops = -0.33f;
  } else if (std::abs(brightness - kBrightnessTarget) > kBrightnessDeadband) {
    // One jump to the ratio that would reach the target, damped by half; but never up
    // into burnt highlights.
    const float ratio = static_cast<float>(kBrightnessTarget) / std::max(brightness, 4);
    stops = std::log2(ratio) / 2.0f;
    if (stops > 0.0f && highlights > kHighlightLimit - 20) {
      return;
    }
  } else {
    return;
  }
  // Exposure time is used up before gain (gain adds noise), and gain is given back before
  // exposure time.
  int exposure = exposureRange.value;
  int gain = gainRange.value;
  if (stops > 0.0f) {
    if (exposure < exposureRange.max) {
      exposure = static_cast<int>(exposure * std::exp2(stops)) + 1;
    } else {
      gain += static_cast<int>(stops * kGainStepsPerStop) + 1;
    }
  } else {
    if (gain > gainRange.min) {
      gain += static_cast<int>(stops * kGainStepsPerStop) - 1;
    } else {
      exposure = static_cast<int>(exposure * std::exp2(stops));
    }
  }
  applyExposure(exposure, gain);
}

void setExposure(int exposure, int gain) {
  if (camera.fd < 0) {
    return;
  }
  autoExposure = exposure < 0;
  if (!autoExposure) {
    applyExposure(exposure, gain);
  }
  ESP_LOGI(TAG, "exposure %s: exposure %d, gain %d", autoExposure ? "automatic" : "fixed", exposureRange.value,
           gainRange.value);
}

}  // namespace hand_gesture
