#include "heatboard/heat_list.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

#include "text_util.h"

namespace heatboard {
namespace {

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// Column layout: JDLid,name,class,heat
constexpr std::size_t kNameColumn = 1;
constexpr std::size_t kClassColumn = 2;
constexpr std::size_t kHeatColumn = 3;
constexpr std::size_t kRequiredColumns = 4;

// Splits one line into fields (RFC 4180 quoting). Returns false when a quoted field is not
// closed or is followed by anything other than a separator.
bool splitCsvLine(std::string_view line, std::vector<std::string>& fields) {
  fields.clear();
  std::size_t pos = 0;
  while (true) {
    while (pos < line.size() && detail::isAsciiSpace(line[pos])) {
      ++pos;
    }
    std::string field;
    if (pos < line.size() && line[pos] == '"') {
      ++pos;
      bool closed = false;
      while (pos < line.size() && !closed) {
        if (line[pos] != '"') {
          field += line[pos++];
        } else if (pos + 1 < line.size() && line[pos + 1] == '"') {
          field += '"';
          pos += 2;
        } else {
          closed = true;
          ++pos;
        }
      }
      while (pos < line.size() && detail::isAsciiSpace(line[pos])) {
        ++pos;
      }
      if (!closed || (pos < line.size() && line[pos] != ',')) {
        return false;
      }
    } else {
      const std::size_t end = std::min(line.find(',', pos), line.size());
      field = detail::trimAscii(line.substr(pos, end - pos));
      pos = end;
    }
    fields.push_back(std::move(field));
    if (pos >= line.size()) {
      return true;
    }
    ++pos;  // the separating comma; a trailing comma therefore yields a final empty field
  }
}

// Syntax-only check used for header detection. A value check would misread an out-of-range
// heat number on the first line as a header and silently drop that row.
bool looksLikeInteger(std::string_view text) {
  if (!text.empty() && text.front() == '-') {
    text.remove_prefix(1);
  }
  if (text.empty()) {
    return false;
  }
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

HeatListParseResult failure(std::string message) {
  HeatListParseResult result;
  result.error = std::move(message);
  return result;
}

HeatListParseResult failureAtLine(std::size_t lineNumber, const std::string& reason) {
  return failure("line " + std::to_string(lineNumber) + ": " + reason);
}

}  // namespace

HeatListParseResult parseHeatListCsv(std::string_view csv) {
  if (csv.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
    csv.remove_prefix(kUtf8Bom.size());
  }

  // Keyed by heat number so the result is sorted and rows of one heat merge even if the
  // sheet does not keep them adjacent.
  std::map<int, Heat> heatsByNumber;
  std::vector<std::string> fields;
  bool headerPossible = true;
  std::size_t lineNumber = 0;

  while (!csv.empty()) {
    const std::size_t newline = csv.find('\n');
    const std::string_view line = csv.substr(0, newline);
    csv.remove_prefix(newline == std::string_view::npos ? csv.size() : newline + 1);
    ++lineNumber;

    if (detail::trimAscii(line).empty()) {
      continue;
    }
    if (!splitCsvLine(line, fields)) {
      return failureAtLine(lineNumber, "malformed quoted field");
    }
    if (fields.size() < kRequiredColumns) {
      return failureAtLine(lineNumber, "expected 4 fields, got " + std::to_string(fields.size()));
    }

    const bool isHeader = headerPossible && !looksLikeInteger(fields[kHeatColumn]);
    headerPossible = false;
    if (isHeader) {
      continue;
    }

    const std::optional<int> number = detail::parseWholeInteger<int>(fields[kHeatColumn]);
    if (!number || *number < 1) {
      return failureAtLine(lineNumber, "heat is not an integer >= 1");
    }

    const auto [entry, isNewHeat] = heatsByNumber.try_emplace(*number);
    Heat& heat = entry->second;
    if (isNewHeat) {
      heat.number = *number;
      heat.className = std::move(fields[kClassColumn]);
    }
    heat.pilots.push_back(std::move(fields[kNameColumn]));
  }

  if (heatsByNumber.empty()) {
    return failure("no heat rows found");
  }

  HeatListParseResult result;
  result.ok = true;
  result.heats.reserve(heatsByNumber.size());
  for (auto& entry : heatsByNumber) {
    result.heats.push_back(std::move(entry.second));
  }
  return result;
}

}  // namespace heatboard
