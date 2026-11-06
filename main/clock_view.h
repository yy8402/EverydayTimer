#pragma once
#include <cstdint>
#include <string>
namespace timer_app {
constexpr int DEFAULT_UTC_OFFSET_MINUTES = 480;
bool valid_utc_offset(int minutes);
struct ClockText {
  std::string date_time, zone;
};
ClockText format_clock(int64_t utc_epoch, bool valid, int utc_offset_minutes);
} // namespace timer_app
