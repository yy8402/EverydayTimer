#include "ui.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>

extern const uint8_t test_cjk_stub[96] asm("_binary_cjk_bin_start") = {0};

using Frame = std::array<uint8_t, 30000>;

Frame render(const timer_app::View &view) {
  std::array<uint8_t, 30032> guarded;
  guarded.fill(0xa5);
  render_ui(guarded.data() + 16, view);
  assert(std::all_of(guarded.begin(), guarded.begin() + 16,
                     [](uint8_t byte) { return byte == 0xa5; }));
  assert(std::all_of(guarded.end() - 16, guarded.end(),
                     [](uint8_t byte) { return byte == 0xa5; }));
  Frame frame;
  std::copy_n(guarded.begin() + 16, frame.size(), frame.begin());
  assert(std::none_of(frame.begin(), frame.end(),
                      [](uint8_t byte) { return (byte & 0xaa) != 0; }));
  return frame;
}

int main() {
  timer_app::View view;
  view.name = "Activity 1";
  view.clock_valid = view.storage_ok = view.stars_available = true;
  view.utc_epoch = 1793683800;
  const auto ready = render(view);
  assert(ready[0] == 0x55);
  view.running = true;
  const auto running = render(view);
  assert(!std::equal(ready.begin(), ready.begin() + 4200, running.begin()));
  view.elapsed_ms = 1000;
  const auto ticking = render(view);
  assert(std::equal(running.begin(), running.begin() + 8500, ticking.begin()));
  assert(!std::equal(running.begin() + 8500, running.begin() + 21600,
                     ticking.begin() + 8500));
  assert(std::equal(running.begin() + 21600, running.end(),
                    ticking.begin() + 21600));
  view.running = false;
  view.has_completed_run = true;
  view.best_ms = 1000;
  const auto stopped = render(view);
  assert(!std::equal(ready.begin(), ready.begin() + 4200, stopped.begin()));
  view.daily_stars = 2;
  const auto two_stars = render(view);
  assert(two_stars[262 * 100 + 30 / 4] != 0x55);
  assert(two_stars[262 * 100 + 52 / 4] != 0x55);
  assert(two_stars[262 * 100 + 74 / 4] == 0x55);
  const auto pixel_at = [&](int x, int y) {
    return (two_stars[y * 100 + x / 4] >> (6 - 2 * (x % 4))) & 3;
  };
  assert(pixel_at(25, 277) == 0);
  assert(pixel_at(30, 277) == 1);
  assert(pixel_at(35, 277) == 0);
  view.idle_smile = true;
  const auto smile = render(view);
  assert(smile[0] == 0);
  assert(smile[10 * 100 + 200 / 4] != 0);
  assert(smile[114 * 100 + 152 / 4] == 0x55);
  assert(smile[230 * 100 + 200 / 4] == 0x55);
  view.daily_stars = 0;
  assert(render(view) == smile);
  view.idle_smile = false;
  assert(render(view) == stopped);
  view.daily_stars = std::numeric_limits<uint64_t>::max();
  const auto stars = render(view);
  assert(std::equal(stopped.begin(), stopped.begin() + 25500, stars.begin()));
  assert(stars != stopped);
  view.error = std::string(100, 'W');
  assert(render(view) != stars);
  view.elapsed_ms = view.best_ms = std::numeric_limits<int64_t>::max();
  render(view);
  std::puts("PASS: monochrome timer layout, live digits, distinct state, footer and frame guards");
}
