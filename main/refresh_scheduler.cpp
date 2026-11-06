#include "refresh_scheduler.h"
namespace timer_app {
display_backend::Update RefreshScheduler::poll(int64_t mono, int64_t epoch,
                                               bool clock_valid, bool running,
                                               bool number_partial) {
  unsigned requested = 0;
  const bool valid =
      clock_valid && epoch >= 1704067200LL && epoch < 4102444800LL;
  if (valid) {
    const int64_t minute = epoch / 60;
    if (running && saw_clock_ && minute != last_clock_minute_)
      requested |= unsigned(display_backend::Update::ClockAndNumber);
    last_clock_minute_ = minute;
    saw_clock_ = true;
  } else
    saw_clock_ = false;
  const int64_t tick = mono / (number_partial ? 1000000LL : 60000000LL);
  if (initialized_ && running && was_running_ && tick != last_timer_tick_ &&
      (number_partial || !valid))
    requested |= unsigned(display_backend::Update::ElapsedNumberOnly);
  last_timer_tick_ = tick;
  was_running_ = running;
  initialized_ = true;
  return static_cast<display_backend::Update>(requested);
}
} // namespace timer_app
