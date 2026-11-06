#include "ui.h"
#include "clock_view.h"
#include "duration.h"
#include "fonts.h"
#include <algorithm>
#include <cstring>
extern const uint8_t cjk_start[] asm("_binary_cjk_bin_start");
namespace {
uint8_t *fb;
constexpr int BLACK = 0, WHITE = 1;
void pixel(int x, int y, int c) {
  if (x < 0 || x >= 400 || y < 0 || y >= 300)
    return;
  int p = y * 100 + x / 4, shift = 6 - (x % 4) * 2;
  fb[p] = (fb[p] & ~(3 << shift)) | ((c & 3) << shift);
}
void rect(int x, int y, int w, int h, int c) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++)
      pixel(xx, yy, c);
}
uint32_t cp(const char *&s) {
  uint8_t a = *s++;
  if (a < 128)
    return a;
  if (a >= 0xe0 && a <= 0xef) {
    uint8_t b = *s++, c = *s++;
    return ((a & 15) << 12) | ((b & 63) << 6) | (c & 63);
  }
  return '?';
}
int width(const std::string &s, const Font &f) {
  int w = 0;
  const char *p = s.c_str();
  while (*p) {
    uint32_t c = cp(p);
    w += c >= 0x4e00 && c <= 0x9fff
             ? 24
             : f.glyphs[(c >= 32 && c < 127 ? c : '?') - 32].advance;
  }
  return w;
}
void text(int x, int baseline, const std::string &s, const Font &f,
          int color = BLACK) {
  const char *p = s.c_str();
  while (*p) {
    uint32_t c = cp(p);
    if (c >= 0x4e00 && c <= 0x9fff) {
      const auto *b = cjk_start + (c - 0x4e00) * 96;
      for (int y = 0; y < 32; y++)
        for (int xx = 0; xx < 24; xx++)
          if (b[(y * 24 + xx) / 8] & (1 << (7 - xx % 8)))
            pixel(x + xx, baseline - 26 + y, color);
      x += 24;
      continue;
    }
    const auto &g = f.glyphs[(c >= 32 && c < 127 ? c : '?') - 32];
    for (int y = 0; y < g.h; y++)
      for (int xx = 0; xx < g.w; xx++) {
        int bit = y * g.w + xx;
        if (f.bits[g.offset + bit / 8] & (1 << (7 - bit % 8)))
          pixel(x + g.x + xx, baseline + g.y + y, color);
      }
    x += g.advance;
  }
}
std::string fit(const std::string &s, int max, const Font &font) {
  if (width(s, font) <= max)
    return s;
  std::string out;
  const char *p = s.c_str();
  while (*p) {
    const char *start = p;
    cp(p);
    std::string next = out + std::string(start, p - start);
    if (width(next + "...", font) > max)
      return out + "...";
    out = next;
  }
  return out;
}
void smile() {
  for (int y = -140; y <= 140; ++y) {
    for (int x = -140; x <= 140; ++x) {
      const int radius_squared = x * x + y * y;
      const int eye_x = (x < 0 ? -x : x) - 48;
      const int eye_y = y + 36;
      const bool outline = radius_squared <= 140 * 140 &&
                           radius_squared >= 129 * 129;
      const bool eyes = eye_x * eye_x + eye_y * eye_y <= 15 * 15;
      const bool mouth = y >= 30 && radius_squared >= 72 * 72 &&
                         radius_squared <= 85 * 85;
      if (outline || eyes || mouth)
        pixel(200 + x, 150 + y, WHITE);
    }
  }
}

void stars(uint64_t count) {
  // Rasterized regular five-point star with a centered lower notch.
  constexpr uint32_t star[] = {
      0x00100, 0x00100, 0x00380, 0x00380, 0x00380, 0x007c0,
      0x1ffff, 0x0fffe, 0x03ff8, 0x01ff0, 0x00fe0, 0x01ff0,
      0x01ff0, 0x01ef0, 0x01830, 0x02008,
  };
  constexpr unsigned capacity = 16;
  const unsigned shown = count > capacity ? 11 : count;
  for (unsigned i = 0; i < shown; ++i)
    for (unsigned y = 0; y < sizeof(star) / sizeof(*star); ++y)
      for (int x = 0; x < 17; ++x)
        if (star[y] & (1u << x))
          pixel(22 + 22 * i + x, 262 + y, BLACK);
  if (count > capacity) {
    const int x = 22 + 22 * shown;
    text(x, 277, fit("+" + std::to_string(count - shown), 378 - x, font_small),
         font_small);
  }
}
const Font &duration_font(const timer_app::DurationText &time) {
  const int unit_width =
      time.unit.empty() ? 0 : 9 + width(time.unit, font_unit);
  for (const Font *f : {&font_digits, &font_medium_digits, &font_compact,
                        &font_small_digits, &font_tiny_digits})
    if ((!time.unit.empty() || f != &font_digits) &&
        width(time.digits, *f) + unit_width <= 360)
      return *f;
  return font_tiny_digits; // int64 milliseconds always fits by this point.
}
} // namespace
int duration_layout_width(int64_t duration_ms) {
  const auto time = timer_app::format_duration(duration_ms);
  return width(time.digits, duration_font(time)) +
         (time.unit.empty() ? 0 : 9 + width(time.unit, font_unit));
}
void render_ui(uint8_t *frame, const timer_app::View &v) {
  fb = frame;
  memset(fb, v.idle_smile ? 0x00 : 0x55, 30000);
  if (v.idle_smile) {
    smile();
    return;
  }
  const auto clock =
      timer_app::format_clock(v.utc_epoch, v.clock_valid, v.utc_offset_minutes);
  const std::string state = v.running ? "RUNNING"
                          : v.has_completed_run ? "STOPPED" : "READY";
  if (v.clock_valid)
    text(22, 28, clock.date_time, font_small);
  text(378 - width(state, font_small), 28, state, font_small);
  rect(22, 42, 356, 1, BLACK);

  text(22, 81, fit(v.name.empty() ? "Timer" : v.name, 356, font_title),
       font_title);
  const auto current = timer_app::format_duration(v.elapsed_ms);
  const Font &digits = duration_font(current);
  const int x = (400 - duration_layout_width(v.elapsed_ms)) / 2;
  text(x, 187, current.digits, digits);
  if (!current.unit.empty())
    text(x + width(current.digits, digits) + 9, 187, current.unit, font_unit);

  rect(22, 216, 356, 1, BLACK);
  const std::string best = v.best_ms < 0 ? "BEST  --" :
      "BEST  " + timer_app::format_duration(v.best_ms).with_unit();
  text(22, 246, fit(best, 356, font_best), font_best);
  std::string footer;
  if (!v.error.empty())
    footer = v.error;
  else if (!v.storage_ok)
    footer = "STORAGE ERROR";
  else if (!v.clock_valid)
    footer = "SYNC CLOCK VIA USB";
  else if (v.stars_available)
    stars(v.daily_stars);
  if (!footer.empty())
    text(22, 277, fit(footer, 356, font_small), font_small);
}
