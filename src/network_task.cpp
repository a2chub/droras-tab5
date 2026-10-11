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

// Wi-Fi and the Raspberry Pi connection. Nothing here blocks for longer than a few
// milliseconds, so a server address saved on the settings screen takes effect at once and
// a Wi-Fi switch is not stuck behind an internet request that is timing out.
void lanTask(void*) {
  AppState& state = AppState::instance();
  DrorasClient& droras = DrorasClient::instance();

  wifi_station::start();
  bool drorasConfigured = false;

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
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// The two internet sources: the pilot list from the Apps Script endpoint and the current
// heat from Firestore. Each request may block for its whole timeout when the venue has no
// internet access, which is why this runs apart from the LAN side.
void cloudTask(void*) {
  AppState& state = AppState::instance();
  DrorasClient& droras = DrorasClient::instance();

  HttpClient firestoreClient(kFirestoreTimeoutMs, kFirestoreMaxBytes);
  bool heatListFetched = false;  // from the Apps Script endpoint, specifically
  int heatListRetryMs = app_config::kHeatListRetryMinMs;
  int64_t nextHeatListAttemptMs = 0;
  int firestoreIntervalMs = app_config::kFirestorePollIntervalMs;
  int64_t nextFirestorePollMs = 0;
  int firestoreFailures = 0;

  while (true) {
    if (wifi_station::isConnected()) {
      // Fetched until it succeeds, unless the Raspberry Pi has supplied the list meanwhile:
      // then only an explicit reload goes back to the endpoint.
      const bool reloadRequested = state.takeHeatListReloadRequest();
      const bool wanted = !heatListFetched && !state.hasHeatList() && nowMs() >= nextHeatListAttemptMs;
      if (reloadRequested || wanted) {
        state.setHeatListLoading();
        heat_sources::HeatListResult result = heat_sources::fetchHeatList();
        if (result.heats) {
          state.setHeatList(std::move(result.heats), HeatListSource::Gas);
          heatListFetched = true;
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
        firestoreIntervalMs = app_config::kFirestorePollIntervalMs;
        nextFirestorePollMs = 0;
      } else if (nowMs() >= nextFirestorePollMs) {
        if (const auto heat = heat_sources::fetchFirestoreCurrentHeat(firestoreClient)) {
          firestoreFailures = 0;
          firestoreIntervalMs = app_config::kFirestorePollIntervalMs;
          // The connection may have come up while the request was in flight.
          if (!droras.isReady()) {
            state.setCurrentHeat(*heat, HeatSource::Firestore);
          }
        } else {
          if (++firestoreFailures >= app_config::kFirestoreFailuresBeforeOffline) {
            state.setHeatSourceLost();
          }
          // Each failure doubles the wait: an unreachable Firestore is retried every
          // minute, not every few seconds.
          firestoreIntervalMs = std::min(firestoreIntervalMs * 2, app_config::kFirestorePollMaxIntervalMs);
          ESP_LOGW(TAG, "Firestore unreachable, next attempt in %d ms", firestoreIntervalMs);
        }
        nextFirestorePollMs = nowMs() + firestoreIntervalMs;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

}  // namespace

void start() {
  xTaskCreate(&lanTask, "lan", 6144, nullptr, 5, nullptr);
  // TLS handshakes run on this task's stack.
  constexpr uint32_t kCloudStackBytes = 12288;
  xTaskCreate(&cloudTask, "cloud", kCloudStackBytes, nullptr, 4, nullptr);
}

}  // namespace network_task
