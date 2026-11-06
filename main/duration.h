#pragma once
#include <cstdint>
#include <string>
namespace timer_app {
struct DurationText {
  std::string digits;
  std::string unit;        // "s" only when there are no minute/hour units.
  std::string with_unit() const {
    return unit.empty() ? digits : digits + " " + unit;
  }
};
// Truncates to whole seconds, without rounding across boundaries.
// Negative inputs render as zero; best's unset sentinel is handled by the UI.
DurationText format_duration(int64_t duration_ms);
} // namespace timer_app
