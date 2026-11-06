#pragma once

#include <string>

namespace audio {
void init();
// Called only while the firmware owns the USB drive.
void load(const char *directory);
std::string selection();
void cycle(int direction);
void start_run();
void stop_run();
void poll();
}
