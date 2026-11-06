#include "wav.h"
#include <cassert>
#include <string>
#include <vector>

namespace {
std::string chunk(const char *tag, const std::string &body) {
  const uint32_t size = body.size();
  std::string bytes(tag);
  for (unsigned i = 0; i < 4; ++i)
    bytes += char(size >> (8 * i));
  bytes += body;
  if (size % 2)
    bytes += '\0';
  return bytes;
}

std::vector<int16_t> read(const std::string &bytes) {
  FILE *file = std::tmpfile();
  assert(file);
  assert(std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
  std::rewind(file);
  auto pcm = wav::read_pcm(file);
  std::fclose(file);
  return pcm;
}
}

int main() {
  const std::string pcm_format(
      "\x01\x00\x01\x00\x80\x3e\x00\x00\x00\x7d\x00\x00\x02\x00\x10\x00", 16);
  const auto format = chunk("fmt ", pcm_format);
  const auto data = chunk("data", std::string("\x34\x12\x00\x80", 4));
  const std::vector<int16_t> samples{0x1234, -32768};
  const auto valid = chunk("RIFF", "WAVE" + chunk("JUNK", "x") + format + data);
  assert(read(valid) == samples);
  assert(read(valid.substr(0, valid.size() - 1)).empty());
  assert(read(chunk("RIFF", "WAVE" + data)).empty());
  assert(read(chunk("RIFF", "WAVE" + format + chunk("data", "x"))).empty());
  auto stereo = pcm_format;
  stereo[2] = 2;
  assert(read(chunk("RIFF", "WAVE" + chunk("fmt ", stereo) + data)).empty());
  std::puts("PASS: PCM samples, padded metadata, truncated files and unsupported WAV formats");
}
