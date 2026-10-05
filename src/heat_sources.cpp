#include "heat_sources.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include "cJSON.h"
#include "esp_log.h"

#include "app_config.h"
#include "heatboard/heat_list.h"

namespace heat_sources {

namespace {

const char* TAG = "sources";

constexpr int kHeatListTimeoutMs = 20000;       // Apps Script cold starts take several seconds
constexpr std::size_t kHeatListMaxBytes = 256 * 1024;

// Firestore encodes values as {"stringValue": "12"} or {"integerValue": "12"}; droras writes
// a string today, but accept both so a type change on the server does not blank the board.
std::optional<int> readHeatField(const cJSON* document) {
  const cJSON* fields = cJSON_GetObjectItemCaseSensitive(document, "fields");
  const cJSON* heat = cJSON_GetObjectItemCaseSensitive(fields, "heat");
  const cJSON* value = cJSON_GetObjectItemCaseSensitive(heat, "stringValue");
  if (!cJSON_IsString(value)) {
    value = cJSON_GetObjectItemCaseSensitive(heat, "integerValue");
  }
  if (!cJSON_IsString(value)) {
    return std::nullopt;
  }
  char* end = nullptr;
  const long number = strtol(value->valuestring, &end, 10);
  if (end == value->valuestring || *end != '\0' || number < 1 || number > 100000) {
    return std::nullopt;
  }
  return static_cast<int>(number);
}

}  // namespace

HeatListResult fetchHeatList() {
  HeatListResult result;
  HttpClient client(kHeatListTimeoutMs, kHeatListMaxBytes);
  const HttpResponse response = client.get(app_config::kHeatListUrl);
  if (response.error != ESP_OK) {
    ESP_LOGW(TAG, "heat list download failed: %s", esp_err_to_name(response.error));
    result.error = std::string("通信エラー (") + esp_err_to_name(response.error) + ")";
    return result;
  }
  if (response.status != 200) {
    ESP_LOGW(TAG, "heat list download failed: HTTP %d", response.status);
    result.error = "HTTP " + std::to_string(response.status);
    return result;
  }

  heatboard::HeatListParseResult parsed = heatboard::parseHeatListCsv(response.body);
  if (!parsed.ok) {
    ESP_LOGW(TAG, "heat list rejected: %s", parsed.error.c_str());
    result.error = "CSVの形式が不正 (" + parsed.error + ")";
    return result;
  }
  ESP_LOGI(TAG, "heat list loaded: %u heats", static_cast<unsigned>(parsed.heats.size()));
  result.heats = std::make_shared<const std::vector<heatboard::Heat>>(std::move(parsed.heats));
  return result;
}

std::optional<int> fetchFirestoreCurrentHeat(HttpClient& client) {
  const HttpResponse response = client.get(app_config::kFirestoreCurrentHeatUrl);
  if (response.error != ESP_OK || response.status != 200) {
    ESP_LOGW(TAG, "Firestore read failed: %s, HTTP %d", esp_err_to_name(response.error), response.status);
    return std::nullopt;
  }

  cJSON* document = cJSON_ParseWithLength(response.body.data(), response.body.size());
  if (document == nullptr) {
    ESP_LOGW(TAG, "Firestore response is not JSON");
    return std::nullopt;
  }
  const std::optional<int> heat = readHeatField(document);
  cJSON_Delete(document);
  if (!heat) {
    ESP_LOGW(TAG, "Firestore document has no usable heat field");
  }
  return heat;
}

}  // namespace heat_sources
