#pragma once
#include <cstdint>
namespace display_backend {
struct Region {
  int x, y, width, height;
};
struct Capabilities {
  bool number_region_partial;
  bool clock_region_partial;
};
// Bit flags coalesce the two independent regions; Full overrides region flags.
enum class Update : unsigned {
  None = 0,
  ClockOnly = 1,
  ElapsedNumberOnly = 2,
  ClockAndNumber = 3,
  Full = 4
};
enum class Waveform { Automatic, FastPreferred };
constexpr Region CLOCK_REGION{0, 0, 400, 43};
constexpr Region NUMBER_REGION{0, 134, 400, 80};
Capabilities capabilities();
bool show(const uint8_t *frame, Update preferred, Waveform waveform = Waveform::Automatic);
int running_snapshot_seconds();
} // namespace display_backend
