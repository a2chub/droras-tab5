#include "debug_console.h"

#include <M5Unified.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace debug_console {

namespace {

const char* TAG = "console";

QueueHandle_t commandQueue = nullptr;

bool parseLine(const char* line, Command& command) {
  char key = 0;
  int x = 0;
  int y = 0;
  if (sscanf(line, "key %c", &key) == 1) {
    command = {Command::Type::Key, key, 0, 0};
    return true;
  }
  if (sscanf(line, "tap %d %d", &x, &y) == 2) {
    command = {Command::Type::Tap, 0, x, y};
    return true;
  }
  if (strcmp(line, "shot") == 0) {
    command = {Command::Type::Screenshot, 0, 0, 0};
    return true;
  }
  return false;
}

void run(void*) {
  char line[48];
  std::size_t length = 0;
  while (true) {
    char c = 0;
    if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) {
      continue;
    }
    if (c == '\r' || c == '\n') {
      line[length] = '\0';
      Command command;
      if (length > 0) {
        if (parseLine(line, command)) {
          xQueueSend(commandQueue, &command, 0);
        } else {
          ESP_LOGW(TAG, "unknown command: %s", line);
        }
      }
      length = 0;
    } else if (length + 1 < sizeof(line)) {
      line[length++] = c;
    }
  }
}

}  // namespace

void start() {
  // The default console can only write; reading needs the interrupt-driven driver.
  // The large TX buffer keeps the screenshot dump from crawling.
  usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  config.tx_buffer_size = 8192;
  const esp_err_t err = usb_serial_jtag_driver_install(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "USB serial driver install failed: %s", esp_err_to_name(err));
    return;
  }
  usb_serial_jtag_vfs_use_driver();

  commandQueue = xQueueCreate(8, sizeof(Command));
  xTaskCreate(&run, "console", 4096, nullptr, 2, nullptr);
}

bool poll(Command& command) {
  return commandQueue != nullptr && xQueueReceive(commandQueue, &command, 0) == pdTRUE;
}

void dumpScreenshot() {
  const int width = M5.Display.width();
  const int height = M5.Display.height();
  std::vector<uint16_t> row(width);

  // One line per pixel row, run-length encoded as <count><rgb565> hex pairs: the UI is
  // mostly flat colour, so this is a few hundred kilobytes instead of several megabytes.
  // Each row goes out in a single write: log lines from other tasks can then only land
  // between rows, where the decoder skips them, instead of corrupting a row.
  std::string line;
  printf("\n@SHOT %d %d\n", width, height);
  for (int y = 0; y < height; ++y) {
    M5.Display.readRect(0, y, width, 1, row.data());
    line.assign("@R ");
    int x = 0;
    while (x < width) {
      int run = 1;
      while (x + run < width && row[x + run] == row[x]) {
        ++run;
      }
      // readRect fills a uint16_t buffer in the panel's byte order (big-endian RGB565).
      char encoded[9];
      snprintf(encoded, sizeof(encoded), "%04x%04x", run, __builtin_bswap16(row[x]));
      line.append(encoded, 8);
      x += run;
    }
    line.push_back('\n');
    fwrite(line.data(), 1, line.size(), stdout);
  }
  printf("@END\n");
  fflush(stdout);
}

}  // namespace debug_console
