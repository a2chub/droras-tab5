// Heat list as published by the race organiser's Google Apps Script endpoint (CSV).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace heatboard {

struct Heat {
  int number = 0;                   // heat number from the CSV (>= 1)
  std::string className;            // e.g. "準々決勝 Expert" (UTF-8)
  std::vector<std::string> pilots;  // CSV row order == video channel order; "" marks a vacant slot
};

struct HeatListParseResult {
  bool ok = false;
  std::string error;        // short English message when !ok, prefixed "line N: " where relevant
  std::vector<Heat> heats;  // ascending by number, one entry per distinct heat number
};

// Parses the `JDLid,name,class,heat` CSV. Parsing is all-or-nothing: Apps Script answers
// failures with an HTML page and HTTP 200, so any row that does not look like heat data fails
// the whole parse instead of yielding a partial list.
//
// Records are one per line; a quoted field cannot contain a line break.
HeatListParseResult parseHeatListCsv(std::string_view csv);

}  // namespace heatboard
