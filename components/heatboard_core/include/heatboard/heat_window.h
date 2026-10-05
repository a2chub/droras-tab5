// Which heats the board shows around the current one, and where prev/next buttons lead.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "heatboard/heat_list.h"

namespace heatboard {

// Indices into `heats` for the four rows on screen. Neighbours wrap around the ends of the
// list, matching the droras operator UI (the heat before heat 1 is the last heat).
struct HeatWindow {
  std::size_t previous;
  std::size_t current;
  std::size_t next;
  std::size_t afterNext;
};

// nullopt when `heats` is empty or no heat has number == currentNumber.
std::optional<HeatWindow> computeHeatWindow(const std::vector<Heat>& heats, int currentNumber);

// Heat number to switch to for the "next"/"previous" buttons (wrapping). nullopt under the same
// conditions as computeHeatWindow.
std::optional<int> nextHeatNumber(const std::vector<Heat>& heats, int currentNumber);
std::optional<int> previousHeatNumber(const std::vector<Heat>& heats, int currentNumber);

}  // namespace heatboard
