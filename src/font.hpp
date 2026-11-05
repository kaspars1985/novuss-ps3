#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Novuss {
namespace Render {

// Decode the next UTF-8 codepoint starting at p, not reading past end.
// Advances p past the consumed bytes. Returns 0xFFFD on malformed input
// (and advances by at least one byte to guarantee progress).
uint32_t nextCodepoint(const char*& p, const char* end);

// Width in pixels of the given UTF-8 string at the given scale.
int textWidth(const std::string& utf8, float scale = 1.0f);

// Draw UTF-8 text into an ARGB8888 framebuffer.
//   fb        : pointer to pixel buffer (0xAARRGGBB)
//   fbW/fbH   : framebuffer dimensions in pixels
//   fbStride  : stride in pixels (>= fbW)
//   x,y       : top-left of the text baseline box (y is top of glyph cell)
//   colorARGB : 0xAARRGGBB color
//   scale     : pixel scale factor (1.0 = native 8x16)
//   align     : 0=left, 1=center, 2=right (relative to x)
void drawText(uint32_t* fb, int fbW, int fbH, int fbStride,
              int x, int y, const std::string& utf8,
              uint32_t colorARGB, float scale = 1.0f, int align = 0);

// Wrap UTF-8 text to maxWidthPx at the given scale. Strips simple HTML tags
// (<b>, </b>, <kbd>, </kbd>, <span ...>, </span>, etc.) and collapses
// whitespace. Explicit '\n' forces a line break.
std::vector<std::string> wrapText(const std::string& utf8,
                                  int maxWidthPx, float scale = 1.0f);

} // namespace Render
} // namespace Novuss
