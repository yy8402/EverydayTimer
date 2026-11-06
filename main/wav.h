#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace wav {
constexpr unsigned sample_rate = 16000;
std::vector<int16_t> read_pcm(FILE *file);
}
