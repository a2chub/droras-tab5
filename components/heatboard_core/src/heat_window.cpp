#include "heatboard/heat_window.h"

namespace heatboard {

std::optional<HeatWindow> computeHeatWindow(const std::vector<Heat>& heats, int currentNumber) {
  const std::size_t count = heats.size();
  for (std::size_t current = 0; current < count; ++current) {
    if (heats[current].number == currentNumber) {
      // `+ count - 1` instead of `- 1` keeps the unsigned arithmetic from underflowing at index 0.
      return HeatWindow{(current + count - 1) % count, current, (current + 1) % count, (current + 2) % count};
    }
  }
  return std::nullopt;
}

std::optional<int> nextHeatNumber(const std::vector<Heat>& heats, int currentNumber) {
  const std::optional<HeatWindow> window = computeHeatWindow(heats, currentNumber);
  if (!window) {
    return std::nullopt;
  }
  return heats[window->next].number;
}

std::optional<int> previousHeatNumber(const std::vector<Heat>& heats, int currentNumber) {
  const std::optional<HeatWindow> window = computeHeatWindow(heats, currentNumber);
  if (!window) {
    return std::nullopt;
  }
  return heats[window->previous].number;
}

}  // namespace heatboard
