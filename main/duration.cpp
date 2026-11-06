#include "duration.h"
#include <algorithm>
#include <cstdio>
namespace timer_app {
DurationText format_duration(int64_t duration_ms) {
  const int64_t total_seconds = std::max<int64_t>(0, duration_ms) / 1000;
  const int64_t hours = total_seconds / 3600;
  const int minutes = (int)((total_seconds / 60) % 60);
  const int seconds = (int)(total_seconds % 60);
  char text[48];
  if (hours > 0) {
    snprintf(text, sizeof text, "%lld:%02d:%02d", (long long)hours, minutes,
             seconds);
    return {text, ""};
  }
  if (minutes > 0) {
    snprintf(text, sizeof text, "%d:%02d", minutes, seconds);
    return {text, ""};
  }
  snprintf(text, sizeof text, "%d", seconds);
  return {text, "s"};
}
} // namespace timer_app
