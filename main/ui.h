#pragma once
#include "core.h"
#include <cstdint>
// Used by host tests to verify all duration strings fit the number region.
int duration_layout_width(int64_t duration_ms);
void render_ui(uint8_t *frame, const timer_app::View &v);
