#include "esp_heap_caps.h"
#include "miniz.h"
#include <cstdint>

extern const uint8_t cjk_start[] asm("_binary_cjk_z_start");
extern const uint8_t cjk_end[] asm("_binary_cjk_z_end");

const uint8_t *cjk_font() {
  static const auto *font = []() -> uint8_t * {
    constexpr size_t bytes = (0xa000 - 0x4e00) * 96;
    auto *glyphs = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (!glyphs)
      return nullptr;
    // The ROM convenience wrapper puts the decompressor's Huffman tables
    // on the caller's stack. Keep them in PSRAM beside the glyph buffer.
    auto *decoder = static_cast<tinfl_decompressor *>(
        heap_caps_malloc(sizeof(tinfl_decompressor), MALLOC_CAP_SPIRAM));
    if (decoder) {
      size_t input_size = cjk_end - cjk_start;
      size_t output_size = bytes;
      tinfl_init(decoder);
      const auto status = tinfl_decompress(
          decoder, cjk_start, &input_size, glyphs, glyphs, &output_size,
          TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
      heap_caps_free(decoder);
      if (status == TINFL_STATUS_DONE && output_size == bytes)
        return glyphs;
    }
    heap_caps_free(glyphs);
    return nullptr;
  }();
  return font;
}
