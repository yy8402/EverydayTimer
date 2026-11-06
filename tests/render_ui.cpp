#include "ui.h"
#include <cstdio>
#include <cstdlib>
// Arguments: name, elapsed milliseconds, best milliseconds, UTC epoch,
// daily stars, stars available (0/1), running (0/1), storage OK (0/1),
// error, completed run (0/1). Output is the actual packed framebuffer.
int main(int argc, char **argv) {
  uint8_t frame[30000];
  timer_app::View v;
  v.name = argc > 1 ? argv[1] : "Activity 1";
  v.total = 3;
  v.index = 0;
  v.clock_valid = argc < 5 || strtoll(argv[4], nullptr, 10) > 0;
  v.utc_epoch = argc > 4 ? strtoll(argv[4], nullptr, 10) : 1793683800;
  v.utc_offset_minutes = 480;
  v.storage_ok = true;
  v.best_ms = argc > 3 ? strtoll(argv[3], nullptr, 10) : 28400;
  v.elapsed_ms = argc > 2 ? strtoll(argv[2], nullptr, 10) : 32700;
  v.daily_stars = argc > 5 ? strtoull(argv[5], nullptr, 10) : 0;
  v.stars_available = argc < 7 || strtoll(argv[6], nullptr, 10) != 0;
  v.running = argc > 7 && strtoll(argv[7], nullptr, 10) != 0;
  v.storage_ok = argc < 9 || strtoll(argv[8], nullptr, 10) != 0;
  v.error = argc > 9 ? argv[9] : "";
  v.has_completed_run = argc < 11 || strtoll(argv[10], nullptr, 10) != 0;
  render_ui(frame, v);
  fwrite(frame, 1, sizeof frame, stdout);
}
