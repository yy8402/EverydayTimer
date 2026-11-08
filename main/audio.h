#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {
constexpr unsigned sample_rate = 16000;
using Exchange = bool (*)(void *context, int16_t *pcm, size_t frames);
bool stream(Exchange exchange, void *context);
// After init, the timer model lock serializes these calls.
void init();
const char *selection();
bool adjust_interval(int direction);
void start_run(int64_t started);
void stop_run();
void poll();
}
