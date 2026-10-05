#include "network_task.h"

#include <algorithm>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "app_state.h"
#include "droras_client.h"
#include "heat_sources.h"
#include "http_client.h"
#include "settings_store.h"
#include "wifi_station.h"

namespace network_task {

namespace {

const char* TAG = "network";

constexpr int kFirestoreTimeoutMs = 8000;
constexpr std::size_t kFirestoreMaxBytes = 4096;

int64_t nowMs() { return esp_timer_get_time() / 1000; }

void run(void*) {
  AppState& state = AppState::instance();
  DrorasClient& droras = DrorasClient::instance();

  wifi_station::start();

  HttpClient firestoreClient(kFirestoreTimeoutMs, kFirestoreMaxBytes);
  bool drorasConfigured = false;
  bool heatListLoaded = false;
  int heatListRetryMs = app_config::kHeatListRetryMinMs;
  int64_t nextHeatListAttemptMs = 0;
  int64_t nextFirestorePollMs = 0;
  int firestoreFailures = 0;

  while (true) {
    // The stored address is first applied once Wi-Fi is up: before that every connection
    // attempt fails and only fills the log. Later changes take effect immediately.
    const bool reconfigureRequested = state.takeServerReconfigureRequest();
    if ((reconfigureRequested && drorasConfigured) || (!drorasConfigured && wifi_station::isConnected())) {
      droras.configure(settings_store::loadServerAddress());
      drorasConfigured = true;
    }
    if (const auto slot = state.takeWifiSlotRequest()) {
      wifi_station::switchTo(*slot);
    }

    if (wifi_station::isConnected()) {
      const bool reloadRequested = state.takeHeatListReloadRequest();
      if (reloadRequested || (!heatListLoaded && nowMs() >= nextHeatListAttemptMs)) {
        state.setHeatListLoading();
        heat_sources::HeatListResult result = heat_sources::fetchHeatList();
        if (result.heats) {
          state.setHeatList(std::move(result.heats));
          heatListLoaded = true;
          heatListRetryMs = app_config::kHeatListRetryMinMs;
        } else {
          ESP_LOGW(TAG, "heat list fetch failed, retry in %d ms", heatListRetryMs);
          state.setHeatListFailed(std::move(result.error));
          nextHeatListAttemptMs = nowMs() + heatListRetryMs;
          heatListRetryMs = std::min(heatListRetryMs * 2, app_config::kHeatListRetryMaxMs);
        }
      }

      // The Raspberry Pi is the authority while it is reachable; Firestore mirrors it with
      // a delay and is only consulted as a fallback.
      if (droras.isReady()) {
        firestoreFailures = 0;
        nextFirestorePollMs = 0;
      } else if (nowMs() >= nextFirestorePollMs) {
        nextFirestorePollMs = nowMs() + app_config::kFirestorePollIntervalMs;
        if (const auto heat = heat_sources::fetchFirestoreCurrentHeat(firestoreClient)) {
          firestoreFailures = 0;
          // The connection may have come up while the request was in flight.
          if (!droras.isReady()) {
            state.setCurrentHeat(*heat, HeatSource::Firestore);
          }
        } else if (++firestoreFailures >= app_config::kFirestoreFailuresBeforeOffline) {
          state.setHeatSourceLost();
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

}  // namespace

void start() {
  // TLS handshakes run on this task's stack.
  constexpr uint32_t kStackBytes = 12288;
  xTaskCreate(&run, "network", kStackBytes, nullptr, 5, nullptr);
}

}  // namespace network_task
