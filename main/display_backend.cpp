#include "display_backend.h"
#include "hardware.h"

namespace display_backend {
Capabilities capabilities() { return {true, true}; }

bool show(const uint8_t *frame, Update preferred, Waveform waveform) {
  return preferred == Update::None || epd_show(frame, waveform);
}

int running_snapshot_seconds() { return 1; }
} // namespace display_backend
