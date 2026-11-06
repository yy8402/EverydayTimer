#include "voice.h"

#include <cstring>

extern const uint8_t voice_bank[] asm("_binary_voice_bin_start");

namespace {
enum Word {
  Zero, One, Two, Three, Four, Five, Six, Seven, Eight, Nine,
  Ten, Hundred, Thousand, TenThousand, HundredMillion,
  Hour, Minute, MinutePart, Second, ThirtySeconds, OneMinute,
  OneMinuteThirtySeconds
};

uint32_t offset(unsigned word) {
  uint32_t value;
  std::memcpy(&value, voice_bank + word * sizeof value, sizeof value);
  return value;
}

void append(std::vector<int16_t> &pcm, unsigned word) {
  for (auto i = offset(word); i < offset(word + 1); ++i) {
    const int16_t sample = (int(voice_bank[i]) - 128) * 256;
    // The compact bank is 8 kHz. Playback uses the existing 16 kHz clock.
    pcm.push_back(sample);
    pcm.push_back(sample);
  }
  pcm.resize(pcm.size() + 640);
}

void number(std::vector<int16_t> &pcm, uint64_t n) {
  constexpr uint64_t scales[] = {100000000, 10000, 1000, 100, 10};
  constexpr Word names[] = {HundredMillion, TenThousand, Thousand, Hundred, Ten};
  bool started = false;
  bool zero_pending = false;
  for (unsigned i = 0; i < sizeof scales / sizeof *scales; ++i) {
    const auto scale = scales[i];
    if (n >= scale) {
      if (zero_pending)
        append(pcm, Zero);
      const auto digit = n / scale;
      if (scale != 10 || digit != 1 || started)
        number(pcm, digit);
      append(pcm, names[i]);
      n %= scale;
      started = true;
      zero_pending = scale > 10 && n && n < scale / 10;
    } else if (started && n) {
      zero_pending = true;
    }
  }
  if (n) {
    if (zero_pending)
      append(pcm, Zero);
    append(pcm, n);
  }
}

void quantity(std::vector<int16_t> &pcm, uint64_t n, Word unit) {
  if (n) {
    number(pcm, n);
    append(pcm, unit);
  }
}
} // namespace

std::vector<int16_t> voice::render(uint64_t seconds) {
  // Allow the speaker path to settle before speaking the first word.
  std::vector<int16_t> pcm(8000);
  switch (seconds) {
  case 30:
    append(pcm, ThirtySeconds);
    break;
  case 60:
    append(pcm, OneMinute);
    break;
  case 90:
    append(pcm, OneMinuteThirtySeconds);
    break;
  default:
    quantity(pcm, seconds / 3600, Hour);
    quantity(pcm, seconds / 60 % 60, seconds % 60 ? MinutePart : Minute);
    quantity(pcm, seconds % 60, Second);
    break;
  }
  return pcm;
}
