#include "clock_view.h"
#include <cstdio>
#include <ctime>
namespace timer_app {
bool valid_utc_offset(int minutes) { return minutes >= -720 && minutes <= 840; }
ClockText format_clock(int64_t utc_epoch, bool valid, int utc_offset_minutes) {
  if (!valid_utc_offset(utc_offset_minutes))
    return {"CHECK TIMEZONE VIA USB", "OFFSET INVALID"};
  char zone[16];
  const int absolute =
      utc_offset_minutes < 0 ? -utc_offset_minutes : utc_offset_minutes;
  snprintf(zone, sizeof zone, "UTC%c%02d:%02d",
           utc_offset_minutes < 0 ? '-' : '+', absolute / 60, absolute % 60);
  if (!valid || utc_epoch < 1704067200LL || utc_epoch >= 4102444800LL)
    return {"SYNC CLOCK VIA USB", zone};
  const time_t local = (time_t)(utc_epoch + int64_t(utc_offset_minutes) * 60);
  tm t{};
  if (!gmtime_r(&local, &t))
    return {"SYNC CLOCK VIA USB", zone};
  char stamp[24];
  strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &t);
  return {stamp, zone};
}
} // namespace timer_app
