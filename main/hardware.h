#pragma once

#include <cstddef>
#include <cstdint>
#include "driver/gpio.h"
#include "display_backend.h"

// NOTE4C PCB v1.0. Buttons are active LOW; GPIO0 is also the boot strap.
constexpr gpio_num_t BTN_UP = GPIO_NUM_39;
constexpr gpio_num_t BTN_DOWN = GPIO_NUM_18;
constexpr gpio_num_t BTN_OK = GPIO_NUM_0;
constexpr size_t EPD_FRAME_BYTES = 400U * 300U / 4U;

// Call once, as the first operation in app_main, to latch battery power.
bool hardware_init();
// Blocking panel refresh. Call from the display task, not input/UI.
// Exactly 30,000 bytes: four pixels/byte, most significant pixel first.
// Panel encoding: black=0, white=1, yellow=2, red=3.
bool epd_show(const uint8_t* frame, display_backend::Waveform waveform =
                  display_backend::Waveform::Automatic);
// RTC stores UTC, 2000-01-01 through 2099-12-31. A false read means unset,
// invalid, stopped, low-voltage flagged, or an I2C failure; never use its output.
bool rtc_get_epoch(int64_t& utc_epoch);
bool rtc_set_epoch(int64_t utc_epoch);
void led_set(bool on);
