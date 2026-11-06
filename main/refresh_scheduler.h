#pragma once
#include "display_backend.h"
#include <cstdint>
namespace timer_app {
// Pure scheduler: caller supplies clocks; no GPIO, waits, or display
// transactions.
class IdleScreen {
public:
  explicit IdleScreen(int64_t now = 0) : since_(now) {}
  bool visible() const { return visible_; }
  bool update(int64_t now, bool active) {
    if (active)
      since_ = now;
    const bool next = !active && now - since_ >= 300000000LL;
    const bool changed = next != visible_;
    visible_ = next;
    return changed;
  }

private:
  int64_t since_;
  bool visible_ = false;
};

class RefreshScheduler {
public:
  display_backend::Update poll(int64_t monotonic_us, int64_t utc_epoch,
                               bool clock_valid, bool running,
                               bool number_partial);

private:
  bool saw_clock_ = false, was_running_ = false, initialized_ = false;
  int64_t last_clock_minute_ = 0, last_timer_tick_ = 0;
};
} // namespace timer_app
