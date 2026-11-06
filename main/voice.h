#pragma once

#include <cstdint>
#include <vector>

namespace voice {
std::vector<int16_t> render(uint64_t seconds);
}
