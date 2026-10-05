// Remote data the board is built from: the pilot list and the current heat number.
#pragma once

#include <optional>
#include <string>

#include "app_state.h"
#include "http_client.h"

namespace heat_sources {

struct HeatListResult {
  HeatListPtr heats;   // null on failure
  std::string error;   // user-facing (Japanese) reason when heats is null
};

// Downloads and parses the pilot list from the Apps Script endpoint.
HeatListResult fetchHeatList();

// Reads the current heat number from Firestore. `client` is passed in so that consecutive
// polls reuse one TLS connection. nullopt on any failure.
std::optional<int> fetchFirestoreCurrentHeat(HttpClient& client);

}  // namespace heat_sources
