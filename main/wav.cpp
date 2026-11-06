#include "wav.h"
#include <cstring>

namespace {
uint32_t little_endian(const uint8_t *bytes, unsigned size) {
  uint32_t value = 0;
  for (unsigned i = 0; i < size; ++i)
    value |= uint32_t(bytes[i]) << (8 * i);
  return value;
}
}

std::vector<int16_t> wav::read_pcm(FILE *file) {
  uint8_t header[12];
  if (std::fread(header, 1, sizeof header, file) != sizeof header ||
      std::memcmp(header, "RIFF", 4) || std::memcmp(header + 8, "WAVE", 4))
    return {};
  const uint32_t riff_size = little_endian(header + 4, 4);
  // A file on the NOTE4C drive cannot exceed the FAT partition.
  if (riff_size < 4 || riff_size > 256 * 1024 - 8 ||
      std::fseek(file, 0, SEEK_END) != 0 ||
      std::ftell(file) < long(riff_size + 8) ||
      std::fseek(file, sizeof header, SEEK_SET) != 0)
    return {};

  bool pcm_format = false;
  uint32_t remaining = riff_size - 4;
  uint8_t chunk[8];
  while (remaining >= sizeof chunk &&
         std::fread(chunk, 1, sizeof chunk, file) == sizeof chunk) {
    remaining -= sizeof chunk;
    const uint32_t size = little_endian(chunk + 4, 4);
    if (size > remaining || (size == remaining && (size & 1)))
      return {};
    const uint32_t padded = size + (size & 1);
    const long next = std::ftell(file) + padded;
    if (std::memcmp(chunk, "fmt ", 4) == 0) {
      uint8_t format[16];
      if (size < sizeof format ||
          std::fread(format, 1, sizeof format, file) != sizeof format ||
          little_endian(format, 2) != 1 ||
          little_endian(format + 2, 2) != 1 ||
          little_endian(format + 4, 4) != sample_rate ||
          little_endian(format + 8, 4) != sample_rate * 2 ||
          little_endian(format + 12, 2) != 2 ||
          little_endian(format + 14, 2) != 16)
        return {};
      pcm_format = true;
    } else if (std::memcmp(chunk, "data", 4) == 0) {
      if (!pcm_format || size == 0 || size % 2)
        return {};
      // ESP32-S3 stores int16_t in WAV's little-endian byte order.
      std::vector<int16_t> pcm(size / 2);
      if (std::fread(pcm.data(), 1, size, file) != size)
        return {};
      return pcm;
    }
    if (std::fseek(file, next, SEEK_SET) != 0)
      return {};
    remaining -= padded;
  }
  return {};
}
