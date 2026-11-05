#include "font.hpp"

#include <algorithm>
#include <cstring>

namespace Novuss {
namespace Render {

#include "font_ascii.inc"

namespace {

constexpr uint32_t kReplacement = 0xFFFD;

// Glyph cell dimensions for the base ASCII font.
constexpr int kGlyphW = 8;
constexpr int kGlyphH = 16;

// ---------------------------------------------------------------------------
// Glyph storage: a glyph is a 16-row bitmap, each row an 8-bit mask.
// ---------------------------------------------------------------------------
struct Glyph {
    uint8_t rows[kGlyphH];
};

// Copy base ASCII glyph (index 0..94 for codepoints 0x20..0x7E).
void copyAsciiGlyph(uint32_t cp, uint8_t out[kGlyphH]) {
    int idx = -1;
    if (cp >= 0x20 && cp <= 0x7E) {
        idx = static_cast<int>(cp - 0x20);
    } else if (cp == '\t') {
        idx = static_cast<int>(' ' - 0x20);
    } else if (cp == '\r') {
        idx = static_cast<int>(' ' - 0x20);
    }
    if (idx < 0 || idx >= 95) {
        // Unknown: render as a hollow box.
        for (int y = 0; y < kGlyphH; ++y) out[y] = 0;
        out[2] = 0x7E;
        out[13] = 0x7E;
        for (int y = 3; y < 13; ++y) out[y] = 0x42;
        return;
    }
    for (int y = 0; y < kGlyphH; ++y) out[y] = kAsciiFont[idx][y];
}

// Set a single pixel in a glyph bitmap.
inline void setBit(uint8_t rows[kGlyphH], int x, int y) {
    if (x < 0 || x >= kGlyphW || y < 0 || y >= kGlyphH) return;
    rows[y] |= static_cast<uint8_t>(1u << (7 - x));
}

// Draw a horizontal bar of `len` pixels starting at (x0,y).
void hbar(uint8_t rows[kGlyphH], int x0, int y, int len) {
    for (int i = 0; i < len; ++i) setBit(rows, x0 + i, y);
}

// Draw a small comma-like tail below the glyph (for cedilla/comma).
void drawCommaTail(uint8_t rows[kGlyphH], int x, int y) {
    setBit(rows, x, y);
    setBit(rows, x, y + 1);
    setBit(rows, x - 1, y + 2);
}

// Draw a cedilla (hook) below the glyph.
void drawCedilla(uint8_t rows[kGlyphH], int x, int y) {
    setBit(rows, x, y);
    setBit(rows, x, y + 1);
    setBit(rows, x - 1, y + 2);
    setBit(rows, x, y + 2);
    setBit(rows, x + 1, y + 2);
}

// Draw a caron (haček ˇ) above the glyph. The caron points DOWN:
// bottom vertex at (x, y), upper wings at (x-1, y-1) and (x+1, y-1).
void drawCaron(uint8_t rows[kGlyphH], int x, int y) {
    setBit(rows, x, y);
    setBit(rows, x - 1, y - 1);
    setBit(rows, x + 1, y - 1);
}

// Draw a macron (horizontal bar) above the glyph.
void drawMacron(uint8_t rows[kGlyphH], int x0, int y, int len) {
    hbar(rows, x0, y, len);
}

// Build a Latvian glyph by combining base ASCII glyph with diacritics.
// Returns true if cp was handled.
bool buildLatvianGlyph(uint32_t cp, uint8_t out[kGlyphH]) {
    // Map Latvian codepoints to (base ASCII char, diacritic type).
    // diacritic: 0=none, 1=macron, 2=caron, 3=cedilla, 4=comma-below
    struct Map { uint32_t cp; char base; int dia; };
    static const Map kMap[] = {
        {0x0100, 'A', 1}, // Ā
        {0x0101, 'a', 1}, // ā
        {0x010C, 'C', 2}, // Č
        {0x010D, 'c', 2}, // č
        {0x0112, 'E', 1}, // Ē
        {0x0113, 'e', 1}, // ē
        {0x0122, 'G', 3}, // Ģ
        {0x0123, 'g', 3}, // ģ
        {0x012A, 'I', 1}, // Ī
        {0x012B, 'i', 1}, // ī
        {0x0136, 'K', 3}, // Ķ
        {0x0137, 'k', 3}, // ķ
        {0x013B, 'L', 3}, // Ļ
        {0x013C, 'l', 3}, // ļ
        {0x0145, 'N', 3}, // Ņ
        {0x0146, 'n', 3}, // ņ
        {0x0160, 'S', 2}, // Š
        {0x0161, 's', 2}, // š
        {0x016A, 'U', 1}, // Ū
        {0x016B, 'u', 1}, // ū
        {0x017D, 'Z', 2}, // Ž
        {0x017E, 'z', 2}, // ž
    };

    for (const auto& m : kMap) {
        if (m.cp != cp) continue;
        copyAsciiGlyph(static_cast<uint32_t>(static_cast<unsigned char>(m.base)), out);
        switch (m.dia) {
            case 1: { // macron above
                bool upper = (m.base >= 'A' && m.base <= 'Z');
                if (m.base == 'i') {
                    // Lowercase ī: clear the dot rows so the macron replaces it.
                    out[1] = 0;
                    out[2] = 0;
                    drawMacron(out, 1, 2, 6);
                } else if (upper) {
                    // Uppercase macron letters: shift body down one row.
                    for (int y = 11; y >= 2; --y) out[y] = out[y - 1];
                    out[1] = 0;
                    drawMacron(out, 1, 0, 6);
                } else {
                    // Other lowercase macron letters (ā, ē, ū).
                    drawMacron(out, 1, 2, 6);
                }
                break;
            }
            case 2: { // caron above
                bool upper = (m.base >= 'A' && m.base <= 'Z');
                if (upper) {
                    for (int y = 11; y >= 2; --y) out[y] = out[y - 1];
                    out[1] = 0;
                    drawCaron(out, 3, 1);
                } else {
                    drawCaron(out, 3, 2);
                }
                break;
            }
            case 3: { // cedilla / comma below
                if (m.base == 'g') {
                    // Latvian ģ: comma ABOVE the g.
                    setBit(out, 3, 1);
                    setBit(out, 4, 2);
                    setBit(out, 3, 3);
                } else {
                    // Ģ, Ķ, ķ, Ļ, ļ, Ņ, ņ: comma below at rows 12..14.
                    setBit(out, 3, 12);
                    setBit(out, 3, 13);
                    setBit(out, 2, 14);
                }
                break;
            }
            case 4: // comma below
                drawCommaTail(out, 4, 13);
                break;
            default:
                break;
        }
        return true;
    }
    return false;
}

// Build extra punctuation glyphs.
bool buildPunctuationGlyph(uint32_t cp, uint8_t out[kGlyphH]) {
    for (int y = 0; y < kGlyphH; ++y) out[y] = 0;
    switch (cp) {
        case 0x00B7: // · middle dot
            setBit(out, 3, 8);
            setBit(out, 4, 8);
            setBit(out, 3, 9);
            setBit(out, 4, 9);
            return true;
        case 0x2013: // – en dash
            hbar(out, 1, 8, 6);
            return true;
        case 0x2014: // — em dash
            hbar(out, 0, 8, 8);
            return true;
        case 0x2026: // … ellipsis
            setBit(out, 1, 12);
            setBit(out, 4, 12);
            setBit(out, 7, 12);
            return true;
        case 0x201E: // „ low double comma
            setBit(out, 1, 12);
            setBit(out, 1, 13);
            setBit(out, 0, 14);
            setBit(out, 4, 12);
            setBit(out, 4, 13);
            setBit(out, 3, 14);
            return true;
        case 0x201D: // ” right double quote
            setBit(out, 1, 3);
            setBit(out, 1, 4);
            setBit(out, 2, 5);
            setBit(out, 4, 3);
            setBit(out, 4, 4);
            setBit(out, 5, 5);
            return true;
        default:
            return false;
    }
}

// Resolve a codepoint to a glyph bitmap.
void resolveGlyph(uint32_t cp, uint8_t out[kGlyphH]) {
    if (buildLatvianGlyph(cp, out)) return;
    if (buildPunctuationGlyph(cp, out)) return;
    copyAsciiGlyph(cp, out);
}

// Advance width of a codepoint in native pixels (before scale).
int glyphAdvance(uint32_t cp) {
    if (cp == '\t') return kGlyphW * 4;
    return kGlyphW;
}

// ---------------------------------------------------------------------------
// UTF-8 decoding
// ---------------------------------------------------------------------------
uint32_t decodeUtf8(const char*& p, const char* end) {
    if (p >= end) return 0;
    unsigned char c0 = static_cast<unsigned char>(*p);
    if (c0 < 0x80) {
        ++p;
        return c0;
    }
    int need = 0;
    uint32_t cp = 0;
    uint32_t minCp = 0;
    if ((c0 & 0xE0) == 0xC0) {
        need = 1;
        cp = c0 & 0x1F;
        minCp = 0x80;
    } else if ((c0 & 0xF0) == 0xE0) {
        need = 2;
        cp = c0 & 0x0F;
        minCp = 0x800;
    } else if ((c0 & 0xF8) == 0xF0) {
        need = 3;
        cp = c0 & 0x07;
        minCp = 0x10000;
    } else {
        ++p;
        return kReplacement;
    }
    if (p + 1 + need > end) {
        ++p;
        return kReplacement;
    }
    for (int i = 1; i <= need; ++i) {
        unsigned char ci = static_cast<unsigned char>(p[i]);
        if ((ci & 0xC0) != 0x80) {
            ++p;
            return kReplacement;
        }
        cp = (cp << 6) | (ci & 0x3F);
    }
    if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++p;
        return kReplacement;
    }
    p += 1 + need;
    return cp;
}

// ---------------------------------------------------------------------------
// Blending
// ---------------------------------------------------------------------------
inline uint32_t blendPixel(uint32_t dst, uint32_t src, uint32_t alpha) {
    if (alpha == 0) return dst;
    if (alpha >= 255) return src;
    uint32_t sa = (src >> 24) & 0xFF;
    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >> 8) & 0xFF;
    uint32_t sb = src & 0xFF;
    uint32_t da = (dst >> 24) & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = dst & 0xFF;

    uint32_t a = (sa * alpha) / 255;
    uint32_t inv = 255 - a;
    uint32_t r = (sr * a + dr * inv) / 255;
    uint32_t g = (sg * a + dg * inv) / 255;
    uint32_t b = (sb * a + db * inv) / 255;
    uint32_t outA = a + (da * inv) / 255;
    if (outA > 255) outA = 255;
    return (outA << 24) | (r << 16) | (g << 8) | b;
}

// Sample the glyph bitmap at fractional coordinates using bilinear filtering.
// Returns coverage in [0,255].
int sampleGlyph(const uint8_t rows[kGlyphH], float fx, float fy) {
    int x0 = static_cast<int>(fx);
    int y0 = static_cast<int>(fy);
    float tx = fx - static_cast<float>(x0);
    float ty = fy - static_cast<float>(y0);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    auto get = [&](int x, int y) -> float {
        if (x < 0 || x >= kGlyphW || y < 0 || y >= kGlyphH) return 0.0f;
        return ((rows[y] >> (7 - x)) & 1u) ? 1.0f : 0.0f;
    };

    float v00 = get(x0, y0);
    float v10 = get(x1, y0);
    float v01 = get(x0, y1);
    float v11 = get(x1, y1);
    float v0 = v00 * (1.0f - tx) + v10 * tx;
    float v1 = v01 * (1.0f - tx) + v11 * tx;
    float v = v0 * (1.0f - ty) + v1 * ty;
    int iv = static_cast<int>(v * 255.0f + 0.5f);
    if (iv < 0) iv = 0;
    if (iv > 255) iv = 255;
    return iv;
}

// Draw a single glyph at integer pixel position (px, py) with scale.
void drawGlyph(uint32_t* fb, int fbW, int fbH, int fbStride,
               int px, int py, const uint8_t rows[kGlyphH],
               uint32_t colorARGB, float scale) {
    if (scale <= 0.0f) return;

    int outW = static_cast<int>(kGlyphW * scale + 0.5f);
    int outH = static_cast<int>(kGlyphH * scale + 0.5f);
    if (outW <= 0 || outH <= 0) return;

    float invScale = 1.0f / scale;

    for (int oy = 0; oy < outH; ++oy) {
        int fy = py + oy;
        if (fy < 0 || fy >= fbH) continue;
        uint32_t* row = fb + static_cast<size_t>(fy) * fbStride;
        for (int ox = 0; ox < outW; ++ox) {
            int fx = px + ox;
            if (fx < 0 || fx >= fbW) continue;
            // Sample at the center of the destination pixel.
            float sx = (static_cast<float>(ox) + 0.5f) * invScale - 0.5f;
            float sy = (static_cast<float>(oy) + 0.5f) * invScale - 0.5f;
            int cov = sampleGlyph(rows, sx, sy);
            if (cov == 0) continue;
            row[fx] = blendPixel(row[fx], colorARGB, static_cast<uint32_t>(cov));
        }
    }
}

// ---------------------------------------------------------------------------
// HTML tag stripping
// ---------------------------------------------------------------------------
bool isTagStart(const std::string& s, size_t i) {
    return i < s.size() && s[i] == '<';
}

// If s[i] begins a recognized tag, return the index just past '>'.
// Otherwise return i (no tag).
size_t skipTag(const std::string& s, size_t i) {
    if (!isTagStart(s, i)) return i;
    size_t j = i + 1;
    if (j >= s.size()) return i;
    // Closing tag?
    if (s[j] == '/') ++j;
    // Tag name must start with a letter.
    if (j >= s.size()) return i;
    char c = s[j];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return i;
    // Find '>'.
    size_t k = j;
    while (k < s.size() && s[k] != '>') ++k;
    if (k >= s.size()) return i;
    return k + 1;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
uint32_t nextCodepoint(const char*& p, const char* end) {
    if (p >= end) return 0;
    return decodeUtf8(p, end);
}

int textWidth(const std::string& utf8, float scale) {
    if (scale <= 0.0f) return 0;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    int width = 0;
    while (p < end) {
        uint32_t cp = decodeUtf8(p, end);
        if (cp == 0) break;
        if (cp == '\n' || cp == '\r') continue;
        width += glyphAdvance(cp);
    }
    return static_cast<int>(width * scale + 0.5f);
}

void drawText(uint32_t* fb, int fbW, int fbH, int fbStride,
              int x, int y, const std::string& utf8,
              uint32_t colorARGB, float scale, int align) {
    if (!fb || fbW <= 0 || fbH <= 0 || fbStride < fbW || scale <= 0.0f) return;

    int totalW = textWidth(utf8, scale);
    int startX = x;
    if (align == 1) {
        startX = x - totalW / 2;
    } else if (align == 2) {
        startX = x - totalW;
    }

    const char* p = utf8.data();
    const char* end = p + utf8.size();
    int penX = startX;
    int penY = y;

    while (p < end) {
        uint32_t cp = decodeUtf8(p, end);
        if (cp == 0) break;
        if (cp == '\n') {
            penX = startX;
            penY += static_cast<int>(kGlyphH * scale + 0.5f);
            continue;
        }
        if (cp == '\r') continue;

        uint8_t rows[kGlyphH];
        resolveGlyph(cp, rows);
        drawGlyph(fb, fbW, fbH, fbStride, penX, penY, rows, colorARGB, scale);
        penX += static_cast<int>(glyphAdvance(cp) * scale + 0.5f);
    }
}

std::vector<std::string> wrapText(const std::string& utf8,
                                  int maxWidthPx, float scale) {
    std::vector<std::string> lines;
    if (maxWidthPx <= 0 || scale <= 0.0f) {
        lines.push_back(std::string());
        return lines;
    }

    // First, strip HTML tags and collapse whitespace while preserving '\n'.
    std::string clean;
    clean.reserve(utf8.size());
    {
        size_t i = 0;
        bool lastWasSpace = false;
        while (i < utf8.size()) {
            if (utf8[i] == '<') {
                size_t next = skipTag(utf8, i);
                if (next != i) {
                    i = next;
                    continue;
                }
            }
            char c = utf8[i];
            if (c == '\n') {
                clean.push_back('\n');
                lastWasSpace = false;
                ++i;
                continue;
            }
            if (c == '\r') {
                ++i;
                continue;
            }
            if (c == ' ' || c == '\t') {
                if (!lastWasSpace && !clean.empty() && clean.back() != '\n') {
                    clean.push_back(' ');
                    lastWasSpace = true;
                }
                ++i;
                continue;
            }
            clean.push_back(c);
            lastWasSpace = false;
            ++i;
        }
        // Trim trailing space.
        while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    }

    // Now wrap by words.
    const char* p = clean.data();
    const char* end = p + clean.size();

    std::string current;
    int currentW = 0;

    auto flushLine = [&]() {
        // Trim trailing spaces.
        while (!current.empty() && current.back() == ' ') current.pop_back();
        lines.push_back(current);
        current.clear();
        currentW = 0;
    };

    // Helper: measure width of a UTF-8 substring.
    auto measure = [&](const char* s, const char* e) -> int {
        int w = 0;
        const char* q = s;
        while (q < e) {
            uint32_t cp = decodeUtf8(q, e);
            if (cp == 0) break;
            w += glyphAdvance(cp);
        }
        return static_cast<int>(w * scale + 0.5f);
    };

    while (p < end) {
        // Handle explicit newline.
        if (*p == '\n') {
            flushLine();
            ++p;
            continue;
        }

        // Skip leading spaces at line start.
        if (*p == ' ' && current.empty()) {
            ++p;
            continue;
        }

        // Find the next word (up to space or newline).
        const char* wordStart = p;
        while (p < end && *p != ' ' && *p != '\n') ++p;
        const char* wordEnd = p;

        int wordW = measure(wordStart, wordEnd);

        if (current.empty()) {
            // Word alone on line.
            if (wordW <= maxWidthPx) {
                current.assign(wordStart, wordEnd);
                currentW = wordW;
            } else {
                // Break the word by characters.
                const char* q = wordStart;
                std::string chunk;
                int chunkW = 0;
                while (q < wordEnd) {
                    const char* cpStart = q;
                    uint32_t cp = decodeUtf8(q, wordEnd);
                    if (cp == 0) break;
                    int adv = static_cast<int>(glyphAdvance(cp) * scale + 0.5f);
                    if (chunkW + adv > maxWidthPx && !chunk.empty()) {
                        lines.push_back(chunk);
                        chunk.clear();
                        chunkW = 0;
                    }
                    chunk.append(cpStart, q);
                    chunkW += adv;
                }
                current = chunk;
                currentW = chunkW;
            }
        } else {
            // Try to append word with a space.
            int spaceW = static_cast<int>(kGlyphW * scale + 0.5f);
            if (currentW + spaceW + wordW <= maxWidthPx) {
                current.push_back(' ');
                current.append(wordStart, wordEnd);
                currentW += spaceW + wordW;
            } else {
                flushLine();
                // Re-process this word on the new line.
                if (wordW <= maxWidthPx) {
                    current.assign(wordStart, wordEnd);
                    currentW = wordW;
                } else {
                    const char* q = wordStart;
                    std::string chunk;
                    int chunkW = 0;
                    while (q < wordEnd) {
                        const char* cpStart = q;
                        uint32_t cp = decodeUtf8(q, wordEnd);
                        if (cp == 0) break;
                        int adv = static_cast<int>(glyphAdvance(cp) * scale + 0.5f);
                        if (chunkW + adv > maxWidthPx && !chunk.empty()) {
                            lines.push_back(chunk);
                            chunk.clear();
                            chunkW = 0;
                        }
                        chunk.append(cpStart, q);
                        chunkW += adv;
                    }
                    current = chunk;
                    currentW = chunkW;
                }
            }
        }

        // Skip a single space after the word.
        if (p < end && *p == ' ') ++p;
    }

    if (!current.empty() || lines.empty()) {
        flushLine();
    }

    return lines;
}

} // namespace Render
} // namespace Novuss
