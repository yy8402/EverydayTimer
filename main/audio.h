#pragma once

#include <cstdint>

namespace audio {
// After init, the timer model lock serializes these calls.
void init();
const char *selection();
bool adjust_interval(int direction);
void start_run(int64_t started);
void stop_run();
void poll();
}
