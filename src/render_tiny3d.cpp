#include "render_tiny3d.hpp"

#if defined(__PS3__) || defined(__PPU__)
#include <tiny3d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

#include "font.hpp"
#include "i18n.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
#pragma GCC diagnostic pop

namespace Novuss {
namespace RenderTiny3D {

namespace {

static u32 s_boardOffset = 0;
static u32 s_discAtlasOffset = 0;
static u32 s_fontAtlasOffset = 0;
static bool s_inited = false;

static std::unordered_map<uint32_t, int> s_cpToSlot;

// Colors matching original theme
static constexpr uint32_t COL_GOLD       = 0xFFE0C890u;
static constexpr uint32_t COL_TEXT       = 0xFFFFFFFFu;
static constexpr uint32_t COL_DIM        = 0xFFA09888u;
static constexpr uint32_t COL_CARD       = 0xFA1E1812u;
static constexpr uint32_t COL_CARD_EDGE  = 0xFF6E563Eu;
static constexpr uint32_t COL_ROW_SEL    = 0xFF5A3C1Eu;
static constexpr uint32_t COL_RED        = 0xFFD83030u;
static constexpr uint32_t COL_BLACK      = 0xFF282828u;
static constexpr uint32_t COL_BLUE_HINT  = 0xCC3F7FC9u;

// ---------------------------------------------------------------------------
// Helpers for drawing 2D primitives in Tiny3D
// ---------------------------------------------------------------------------
static void drawQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint32_t col) {
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(x0, y0, 0.0f); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x1, y1, 0.0f); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x2, y2, 0.0f); tiny3d_VertexColor(col);
    tiny3d_VertexPos(x3, y3, 0.0f); tiny3d_VertexColor(col);
    tiny3d_End();
}

static void drawRect(float x, float y, float w, float h, uint32_t col) {
    drawQuad(x, y, x + w, y, x + w, y + h, x, y + h, col);
}

static void drawRoundRect(float x, float y, float w, float h, float r, uint32_t col) {
    if (r <= 0.0f) {
        drawRect(x, y, w, h, col);
        return;
    }
    r = std::min(r, std::min(w * 0.5f, h * 0.5f));
    // Center rectangle
    drawRect(x + r, y, w - 2.0f * r, h, col);
    // Left & right rectangles
    drawRect(x, y + r, r, h - 2.0f * r, col);
    drawRect(x + w - r, y + r, r, h - 2.0f * r, col);
    // 4 Corner triangle fans (8 segments each for smooth look)
    const int kSegs = 6;
    float cx[4] = {x + r, x + w - r, x + w - r, x + r};
    float cy[4] = {y + r, y + r, y + h - r, y + h - r};
    float startAng[4] = {3.14159f, 4.71239f, 0.0f, 1.57079f};

    for (int c = 0; c < 4; ++c) {
        tiny3d_SetPolygon(TINY3D_TRIANGLE_FAN);
        tiny3d_VertexPos(cx[c], cy[c], 0.0f);
        tiny3d_VertexColor(col);
        for (int i = 0; i <= kSegs; ++i) {
            float a = startAng[c] + (float)i * (1.57079f / (float)kSegs);
            tiny3d_VertexPos(cx[c] + std::cos(a) * r, cy[c] + std::sin(a) * r, 0.0f);
            tiny3d_VertexColor(col);
        }
        tiny3d_End();
    }
}

static void drawStrokeRoundRect(float x, float y, float w, float h, float r, float thick, uint32_t col) {
    drawRoundRect(x, y, w, thick, 0.0f, col);
    drawRoundRect(x, y + h - thick, w, thick, 0.0f, col);
    drawRoundRect(x, y, thick, h, 0.0f, col);
    drawRoundRect(x + w - thick, y, thick, h, 0.0f, col);
}

// ---------------------------------------------------------------------------
// Text Rendering with Tiny3D Font Atlas
// ---------------------------------------------------------------------------
static void drawText(float x, float y, const std::string& utf8, uint32_t col, float scale = 1.0f, int align = 0) {
    if (utf8.empty() || s_fontAtlasOffset == 0) return;

    int totalW = Render::textWidth(utf8, scale);
    float startX = x;
    if (align == 1) startX = x - (float)totalW * 0.5f;
    else if (align == 2) startX = x - (float)totalW;

    float penX = startX;
    float penY = y;
    float gw = 8.0f * scale;
    float gh = 16.0f * scale;

    tiny3d_SetTexture(0, s_fontAtlasOffset, 256, 256, 256 * 4, TINY3D_TEX_FORMAT_A8R8G8B8, TEXTURE_NEAREST);

    const char* p = utf8.data();
    const char* end = p + utf8.size();

    while (p < end) {
        uint32_t cp = Render::nextCodepoint(p, end);
        if (cp == 0) break;
        if (cp == '\n') {
            penX = startX;
            penY += gh;
            continue;
        }
        if (cp == '\r') continue;
        if (cp == ' ') {
            penX += gw;
            continue;
        }

        int slot = 0;
        auto it = s_cpToSlot.find(cp);
        if (it != s_cpToSlot.end()) {
            slot = it->second;
        } else if (cp >= 0x20 && cp <= 0x7E) {
            slot = static_cast<int>(cp - 0x20);
        }

        int colIdx = slot % 32;
        int rowIdx = slot / 32;
        float u0 = (float)(colIdx * 8) / 256.0f;
        float v0 = (float)(rowIdx * 16) / 256.0f;
        float u1 = (float)((colIdx + 1) * 8) / 256.0f;
        float v1 = (float)((rowIdx + 1) * 16) / 256.0f;

        tiny3d_SetPolygon(TINY3D_QUADS);
        tiny3d_VertexPos(penX, penY, 0.0f); tiny3d_VertexColor(col); tiny3d_VertexTexture(u0, v0);
        tiny3d_VertexPos(penX + gw, penY, 0.0f); tiny3d_VertexColor(col); tiny3d_VertexTexture(u1, v0);
        tiny3d_VertexPos(penX + gw, penY + gh, 0.0f); tiny3d_VertexColor(col); tiny3d_VertexTexture(u1, v1);
        tiny3d_VertexPos(penX, penY + gh, 0.0f); tiny3d_VertexColor(col); tiny3d_VertexTexture(u0, v1);
        tiny3d_End();

        penX += gw;
    }
}

// ---------------------------------------------------------------------------
// Disc procedural texture evaluation (Light, Dark, Striker, Shadow)
// ---------------------------------------------------------------------------
static uint32_t evalDiscPixel(double dx, double dy, double r, double holeR, int kindIdx) {
    double d = std::sqrt(dx * dx + dy * dy);
    if (d > r + 0.5) return 0u;
    if (holeR > 0.0 && d < holeR - 0.5) return 0u;

    double nx = dx / (r > 0 ? r : 1.0);
    double ny = dy / (r > 0 ? r : 1.0);
    double lx = -0.577, ly = -0.577, lz = 0.577;
    double nz = (d < r) ? std::sqrt(1.0 - (d / r) * (d / r)) : 0.0;
    double diff = std::max(0.0, nx * lx + ny * ly + nz * lz);

    uint32_t baseCol = 0;
    if (kindIdx == 0) baseCol = 0xFFD8C29Du;       // Light wood
    else if (kindIdx == 1) baseCol = 0xFF362215u;  // Dark wood
    else baseCol = 0xFFC02828u;                    // Striker red

    uint8_t a = 0xFF;
    if (d > r - 0.5) {
        a = (uint8_t)(std::clamp(r + 0.5 - d, 0.0, 1.0) * 255.0);
    }
    if (holeR > 0.0 && d < holeR + 0.5) {
        double holeCov = std::clamp(d - (holeR - 0.5), 0.0, 1.0);
        a = (uint8_t)((a * holeCov) / 255.0);
    }

    uint8_t cr = (baseCol >> 16) & 0xFF;
    uint8_t cg = (baseCol >> 8) & 0xFF;
    uint8_t cb = baseCol & 0xFF;
    float shade = 0.70f + 0.30f * (float)diff;
    cr = (uint8_t)std::min(255.0f, cr * shade);
    cg = (uint8_t)std::min(255.0f, cg * shade);
    cb = (uint8_t)std::min(255.0f, cb * shade);

    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(cr) << 16) |
           (static_cast<uint32_t>(cg) << 8) |
           (static_cast<uint32_t>(cb));
}

static uint32_t evalShadowPixel(double dx, double dy, double r) {
    double d = std::sqrt(dx * dx + dy * dy);
    if (d > r + 3.0) return 0u;
    double falloff = 1.0 - std::clamp((d - (r - 1.0)) / 4.0, 0.0, 1.0);
    uint8_t a = (uint8_t)(falloff * 95.0);
    return (static_cast<uint32_t>(a) << 24) | 0x000000u;
}

static std::string i2s(int v) {
    char b[32];
    std::sprintf(b, "%d", v);
    return b;
}

} // namespace

bool init(int /*width*/, int /*height*/) {
    if (s_inited) return true;

    // 1. Initialize Tiny3D runtime (1 MB vertex command buffer).
    if (tiny3d_Init(1024 * 1024) != 0) {
        return false;
    }

    // 2. Load board.png into RSX VRAM (1090x1090 ARGB).
    static const char* kBoardPaths[] = {
        "/dev_hdd0/game/NOVU00001/USRDIR/board.png",
        "USRDIR/board.png",
        "assets/board.png",
        nullptr
    };
    int bw = 0, bh = 0, bch = 0;
    stbi_uc* boardData = nullptr;
    for (int i = 0; kBoardPaths[i]; ++i) {
        boardData = stbi_load(kBoardPaths[i], &bw, &bh, &bch, 4);
        if (boardData) break;
    }

    if (boardData && bw > 0 && bh > 0) {
        uint32_t* rsxBoard = (uint32_t*)tiny3d_AllocTexture(bw * bh * 4);
        if (rsxBoard) {
            s_boardOffset = tiny3d_TextureOffset(rsxBoard);
            for (int i = 0; i < bw * bh; ++i) {
                uint8_t r = boardData[i * 4 + 0];
                uint8_t g = boardData[i * 4 + 1];
                uint8_t b = boardData[i * 4 + 2];
                uint8_t a = boardData[i * 4 + 3];
                rsxBoard[i] = (static_cast<uint32_t>(a) << 24) |
                              (static_cast<uint32_t>(r) << 16) |
                              (static_cast<uint32_t>(g) << 8)  |
                              (static_cast<uint32_t>(b));
            }
        }
        stbi_image_free(boardData);
    }

    // 3. Generate Disc Atlas (256x64: Light, Dark, Striker, Shadow).
    const int kAtlasW = 256, kAtlasH = 64;
    uint32_t* rsxDisc = (uint32_t*)tiny3d_AllocTexture(kAtlasW * kAtlasH * 4);
    if (rsxDisc) {
        s_discAtlasOffset = tiny3d_TextureOffset(rsxDisc);
        std::memset(rsxDisc, 0, kAtlasW * kAtlasH * 4);
        double puckR = 25.0, puckHole = 6.0;
        double strikerR = 29.0, strikerHole = 7.0;

        for (int y = 0; y < 64; ++y) {
            double dy = y - 31.5;
            for (int x = 0; x < 64; ++x) {
                double dx = x - 31.5;
                // Slot 0: Light
                rsxDisc[y * kAtlasW + x] = evalDiscPixel(dx, dy, puckR, puckHole, 0);
                // Slot 1: Dark
                rsxDisc[y * kAtlasW + 64 + x] = evalDiscPixel(dx, dy, puckR, puckHole, 1);
                // Slot 2: Striker
                rsxDisc[y * kAtlasW + 128 + x] = evalDiscPixel(dx, dy, strikerR, strikerHole, 2);
                // Slot 3: Shadow
                rsxDisc[y * kAtlasW + 192 + x] = evalShadowPixel(dx, dy, puckR);
            }
        }
    }

    // 4. Generate Font Atlas (256x256 ARGB: 32 columns x 16 rows of 8x16 glyphs).
    uint32_t* rsxFont = (uint32_t*)tiny3d_AllocTexture(256 * 256 * 4);
    if (rsxFont) {
        s_fontAtlasOffset = tiny3d_TextureOffset(rsxFont);
        std::memset(rsxFont, 0, 256 * 256 * 4);

        int currentSlot = 0;
        // ASCII 0x20..0x7E
        for (uint32_t cp = 0x20; cp <= 0x7E; ++cp) {
            s_cpToSlot[cp] = currentSlot;
            uint8_t rows[16];
            Render::getGlyphBitmap(cp, rows);

            int col = currentSlot % 32;
            int row = currentSlot / 32;
            for (int gy = 0; gy < 16; ++gy) {
                for (int gx = 0; gx < 8; ++gx) {
                    bool on = (rows[gy] & (1 << (7 - gx))) != 0;
                    rsxFont[(row * 16 + gy) * 256 + (col * 8 + gx)] = on ? 0xFFFFFFFFu : 0x00FFFFFFu;
                }
            }
            ++currentSlot;
        }

        // Latvian codepoints
        static const uint32_t kLatvianCPs[] = {
            0x0100, 0x0101, 0x010C, 0x010D, 0x0112, 0x0113, 0x0122, 0x0123,
            0x012A, 0x012B, 0x0136, 0x0137, 0x013B, 0x013C, 0x0145, 0x0146,
            0x0160, 0x0161, 0x016A, 0x016B, 0x017D, 0x017E,
            0x00B7, 0x2013, 0x2014, 0x2026, 0x00B0
        };
        for (uint32_t cp : kLatvianCPs) {
            s_cpToSlot[cp] = currentSlot;
            uint8_t rows[16];
            Render::getGlyphBitmap(cp, rows);

            int col = currentSlot % 32;
            int row = currentSlot / 32;
            for (int gy = 0; gy < 16; ++gy) {
                for (int gx = 0; gx < 8; ++gx) {
                    bool on = (rows[gy] & (1 << (7 - gx))) != 0;
                    rsxFont[(row * 16 + gy) * 256 + (col * 8 + gx)] = on ? 0xFFFFFFFFu : 0x00FFFFFFu;
                }
            }
            ++currentSlot;
        }
    }

    s_inited = true;
    return true;
}

void shutdown() {
    if (s_inited) {
        tiny3d_Exit();
        s_inited = false;
    }
}

void renderFrame(App& app) {
    if (!s_inited) return;

    const std::string& lang = app.settings.lang;
    float ui = 1.0f;

    // 1. Clear GPU framebuffers
    tiny3d_Clear(0x140E0AFF, TINY3D_CLEAR_ALL);

    // 2. Set up virtual 1280x720 2D coordinate space
    tiny3d_UserViewport(1, 0.0f, 0.0f,
                        (float)Video_Resolution.width / 1280.0f,
                        (float)Video_Resolution.height / 720.0f,
                        1.0f, 1.0f);
    tiny3d_Project2D();

    // 3. Enable Alpha Blending
    tiny3d_BlendFunc(1,
        (blend_src_func)(TINY3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA | TINY3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA),
        (blend_dst_func)(TINY3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA | TINY3D_BLEND_FUNC_DST_ALPHA_ONE_MINUS_SRC_ALPHA),
        (blend_func)(TINY3D_BLEND_RGB_FUNC_ADD | TINY3D_BLEND_ALPHA_FUNC_ADD));

    // 4. Background room ambiance gradient
    tiny3d_SetPolygon(TINY3D_QUADS);
    tiny3d_VertexPos(0.0f, 0.0f, 0.0f); tiny3d_VertexColor(0x16100BFF);
    tiny3d_VertexPos(1280.0f, 0.0f, 0.0f); tiny3d_VertexColor(0x16100BFF);
    tiny3d_VertexPos(1280.0f, 720.0f, 0.0f); tiny3d_VertexColor(0x0B0806FF);
    tiny3d_VertexPos(0.0f, 720.0f, 0.0f); tiny3d_VertexColor(0x0B0806FF);
    tiny3d_End();

    // 5. Board positioning
    int padX = 40, padY = 28;
    int topH = 52;
    int hudX = padX, hudY = padY, hudW = 1280 - 2 * padX, hudH = topH;
    int logW = 380;
    int leftMargin = (app.settings.log && !app.menuOpen) ? (padX + logW + 16) : padX;
    int rightMargin = padX;
    int availW = 1280 - leftMargin - rightMargin;
    int availH = 720 - (hudY + topH + 10) - (padY + 28);
    double vScale = std::min(availW, availH) / 1110.0;
    double vCx = leftMargin + availW * 0.5;
    double vCy = hudY + topH + 6 + availH * 0.5;
    double boardSize = 1090.0 * vScale;
    double halfB = boardSize * 0.5;

    // Floor shadow under board
    drawRoundRect((float)(vCx - halfB), (float)(vCy - halfB + 12.0 * vScale),
                  (float)boardSize, (float)boardSize, (float)(12.0 * vScale), 0x55000000u);

    // 6. Draw Board (hardware textured quad with rotation)
    float cosA = std::cos((float)app.viewRot);
    float sinA = std::sin((float)app.viewRot);

    if (s_boardOffset != 0) {
        tiny3d_SetTexture(0, s_boardOffset, 1090, 1090, 1090 * 4, TINY3D_TEX_FORMAT_A8R8G8B8, TEXTURE_LINEAR);
        float lx[4] = {-(float)halfB, (float)halfB, (float)halfB, -(float)halfB};
        float ly[4] = {-(float)halfB, -(float)halfB, (float)halfB, (float)halfB};
        float u[4]  = {0.0f, 1.0f, 1.0f, 0.0f};
        float v[4]  = {0.0f, 0.0f, 1.0f, 1.0f};

        tiny3d_SetPolygon(TINY3D_QUADS);
        for (int i = 0; i < 4; ++i) {
            float rx = (float)vCx + lx[i] * cosA - ly[i] * sinA;
            float ry = (float)vCy + lx[i] * sinA + ly[i] * cosA;
            tiny3d_VertexPos(rx, ry, 0.0f);
            tiny3d_VertexTexture(u[i], v[i]);
        }
        tiny3d_End();
    }

    auto toScreen = [&](double bx, double by, float& sx, float& sy) {
        float lx = (float)((bx - 500.0) * vScale);
        float ly = (float)((by - 500.0) * vScale);
        sx = (float)vCx + lx * cosA - ly * sinA;
        sy = (float)vCy + lx * sinA + ly * cosA;
    };

    // 7. Draw Pieces & Drop Shadows
    if (s_discAtlasOffset != 0) {
        tiny3d_SetTexture(0, s_discAtlasOffset, 256, 64, 256 * 4, TINY3D_TEX_FORMAT_A8R8G8B8, TEXTURE_LINEAR);

        auto drawDiscSprite = [&](float sx, float sy, float radius, int slot, uint32_t tint = 0xFFFFFFFFu) {
            float u0 = (float)(slot * 64) / 256.0f;
            float u1 = (float)((slot + 1) * 64) / 256.0f;
            float r = radius * 1.18f;

            tiny3d_SetPolygon(TINY3D_QUADS);
            tiny3d_VertexPos(sx - r, sy - r, 0.0f); tiny3d_VertexColor(tint); tiny3d_VertexTexture(u0, 0.0f);
            tiny3d_VertexPos(sx + r, sy - r, 0.0f); tiny3d_VertexColor(tint); tiny3d_VertexTexture(u1, 0.0f);
            tiny3d_VertexPos(sx + r, sy + r, 0.0f); tiny3d_VertexColor(tint); tiny3d_VertexTexture(u1, 1.0f);
            tiny3d_VertexPos(sx - r, sy + r, 0.0f); tiny3d_VertexColor(tint); tiny3d_VertexTexture(u0, 1.0f);
            tiny3d_End();
        };

        float puckR = (float)(C::PUCK_R * vScale);
        float strikerR = (float)(C::STRIKER_R * vScale);
        float shOffX = 2.5f * (float)vScale;
        float shOffY = 4.5f * (float)vScale;

        // Step A: All piece shadows
        if (app.sim) {
            for (const auto& b : app.sim->bodies) {
                if (!b.on) continue;
                float sx, sy;
                toScreen(b.x, b.y, sx, sy);
                drawDiscSprite(sx + shOffX, sy + shOffY, b.striker ? strikerR : puckR, 3);
            }
        } else {
            for (const auto& p : app.game.pucks) {
                if (!p.on) continue;
                float sx, sy;
                toScreen(p.x, p.y, sx, sy);
                drawDiscSprite(sx + shOffX, sy + shOffY, puckR, 3);
            }
            if (app.hasStriker) {
                float sx, sy;
                toScreen(app.strikerX, app.strikerY, sx, sy);
                drawDiscSprite(sx + shOffX, sy + shOffY, strikerR, 3);
            }
        }

        // Step B: All pieces
        if (app.sim) {
            for (const auto& b : app.sim->bodies) {
                if (!b.on) continue;
                float sx, sy;
                toScreen(b.x, b.y, sx, sy);
                int kindIdx = b.striker ? 2 : (b.id >= 0 && b.id < (int)app.game.pucks.size() ? app.game.pucks[b.id].owner : 0);
                drawDiscSprite(sx, sy, b.striker ? strikerR : puckR, kindIdx);
            }
        } else {
            for (const auto& p : app.game.pucks) {
                if (!p.on) continue;
                float sx, sy;
                toScreen(p.x, p.y, sx, sy);
                drawDiscSprite(sx, sy, puckR, p.owner); // 0 = Light, 1 = Dark

                // Dark puck hint indicator
                if (app.helpersHints && app.phase == Phase::Place && p.owner == app.game.turn && app.game.isDark(p)) {
                    drawRoundRect(sx - puckR - 2.0f, sy - puckR - 2.0f,
                                  (puckR + 2.0f) * 2.0f, (puckR + 2.0f) * 2.0f,
                                  puckR + 2.0f, COL_BLUE_HINT);
                }
            }

            if (app.hasStriker) {
                float sx, sy;
                toScreen(app.strikerX, app.strikerY, sx, sy);
                drawDiscSprite(sx, sy, strikerR, 2); // 2 = Striker
            }
        }
    }

    // 8. Aiming guides & cue stick (Phase::Place)
    if (app.phase == Phase::Place && app.hasStriker) {
        float sx, sy;
        toScreen(app.strikerX, app.strikerY, sx, sy);

        if (app.settings.guide) {
            float ex = sx + (float)(app.aimDir[0] * 120.0 * vScale);
            float ey = sy + (float)(app.aimDir[1] * 120.0 * vScale);
            uint32_t lineCol = app.aimValid ? 0xE0E0C890u : 0xE0D83030u;

            tiny3d_SetPolygon(TINY3D_LINES);
            tiny3d_VertexPos(sx, sy, 0.0f); tiny3d_VertexColor(lineCol);
            tiny3d_VertexPos(ex, ey, 0.0f); tiny3d_VertexColor(lineCol);
            tiny3d_End();
        }

        // Cue stick
        double pullBack = (15.0 + app.aimPower * 60.0) * vScale;
        float cx0 = sx - (float)(app.aimDir[0] * pullBack);
        float cy0 = sy - (float)(app.aimDir[1] * pullBack);
        float cx1 = cx0 - (float)(app.aimDir[0] * 180.0 * vScale);
        float cy1 = cy0 - (float)(app.aimDir[1] * 180.0 * vScale);

        tiny3d_SetPolygon(TINY3D_LINES);
        tiny3d_VertexPos(cx0, cy0, 0.0f); tiny3d_VertexColor(0xFFE8D0A0u);
        tiny3d_VertexPos(cx1, cy1, 0.0f); tiny3d_VertexColor(0xFF6E4020u);
        tiny3d_End();
    }

    // 9. Top HUD
    drawRoundRect((float)hudX, (float)hudY, (float)hudW, (float)hudH, 10.0f * ui, 0xE018100Au);

    // Badges
    int badgeW = (int)(200 * ui), badgeH = (int)(40 * ui);
    int badgeY = hudY + (int)(6 * ui);

    // Player 1 badge
    drawRoundRect((float)(hudX + 10), (float)badgeY, (float)badgeW, (float)badgeH, 8.0f * ui, 0xD0241A12u);
    drawText((float)(hudX + 20), (float)(badgeY + 8), app.nameOf(0), (app.game.turn == 0) ? COL_GOLD : COL_TEXT, 1.0f * ui);
    drawText((float)(hudX + badgeW - 10), (float)(badgeY + 8), i2s(app.game.onTable(0)), COL_GOLD, 1.2f * ui, 2);

    // Player 2 badge
    int p2x = hudX + hudW - 10 - badgeW;
    drawRoundRect((float)p2x, (float)badgeY, (float)badgeW, (float)badgeH, 8.0f * ui, 0xD0241A12u);
    drawText((float)(p2x + 10), (float)(badgeY + 8), app.nameOf(1), (app.game.turn == 1) ? COL_GOLD : COL_TEXT, 1.0f * ui);
    drawText((float)(p2x + badgeW - 20), (float)(badgeY + 8), i2s(app.game.onTable(1)), COL_GOLD, 1.2f * ui, 2);

    // Score & Set info
    std::string scoreStr = app.nameOf(0) + "  " + i2s(app.game.sets[0]) + " : " + i2s(app.game.sets[1]) + "  " + app.nameOf(1);
    drawText(640.0f, (float)(hudY + 4 * ui), scoreStr, COL_GOLD, 1.35f * ui, 1);
    std::string setInfo = I18n::tr(lang, "setInfo", {{"n", i2s(app.game.setNo)}, {"w", i2s(app.game.setsToWin)}});
    drawText(640.0f, (float)(hudY + 23 * ui), setInfo, COL_DIM, 1.0f * ui, 1);
    std::string status = (app.phase == Phase::AI || app.phase == Phase::AIAnim)
        ? I18n::tr(lang, "thinking", {{"name", app.nameOf(app.game.turn)}})
        : I18n::tr(lang, "yourShot", {{"name", app.nameOf(app.game.turn)}});
    drawText(640.0f, (float)(hudY + 37 * ui), status, COL_TEXT, 1.0f * ui, 1);

    // 10. Game Log panel
    if (app.settings.log && !app.menuOpen) {
        int py = hudY + topH + 8;
        int ph = 720 - py - padY - 26;
        drawRoundRect((float)padX, (float)py, (float)logW, (float)ph, 10.0f * ui, 0xE0140E09u);
        drawStrokeRoundRect((float)padX, (float)py, (float)logW, (float)ph, 10.0f * ui, 1.0f, COL_CARD_EDGE);
        drawText((float)(padX + 12), (float)(py + 10), I18n::tr(lang, "logTitle"), COL_GOLD, 1.0f * ui, 0);

        int logWrapW = logW - 24;
        int curY = py + 34;
        int start = std::max(0, (int)app.matchLog.size() - 12);
        for (int i = start; i < (int)app.matchLog.size(); ++i) {
            const auto& e = app.matchLog[i];
            std::string key = e.hasRes ? e.res.k : e.k;
            int who = e.hasRes ? (e.res.winner >= 0 ? e.res.winner : e.shooter) : e.who;
            std::map<std::string, std::string> params;
            params["name"] = app.nameOf(who);
            params["n"] = i2s(e.n);
            std::string msg = I18n::tr(lang, key, params);
            auto wrapped = Render::wrapText(msg, logWrapW, 0.85f * ui);
            uint32_t lineCol = (who == 0) ? COL_GOLD : COL_DIM;
            for (const auto& wline : wrapped) {
                if (curY + 16 > py + ph - 8) break;
                drawText((float)(padX + 12), (float)curY, wline, lineCol, 0.85f * ui, 0);
                curY += (int)(16 * ui);
            }
        }
    }

    // 11. Toasts & Banners
    if (!app.toasts.empty()) {
        int ty = 720 - padY - 24;
        for (const auto& t : app.toasts) {
            uint32_t col = (t.kind == "warn") ? COL_RED : COL_TEXT;
            drawText(640.0f, (float)ty, t.text, col, 1.0f * ui, 1);
            ty -= 20;
        }
    }
    if (app.bannerTimer > 0.0 && !app.bannerText.empty()) {
        drawRoundRect(400.0f, 620.0f, 480.0f, 40.0f, 8.0f * ui, 0xD01E1812u);
        drawText(640.0f, 630.0f, app.bannerText, COL_GOLD, 1.1f * ui, 1);
    }

    // 12. Main Menu Overlay
    if (app.menuOpen) {
        drawRect(0.0f, 0.0f, 1280.0f, 720.0f, 0x90000000u);
        int cw = 520, ch = 495;
        int cx = 640 - cw / 2, cy = 360 - ch / 2;
        drawRoundRect((float)cx, (float)cy, (float)cw, (float)ch, 14.0f * ui, COL_CARD);
        drawStrokeRoundRect((float)cx, (float)cy, (float)cw, (float)ch, 14.0f * ui, 2.0f, COL_GOLD);
        drawText(640.0f, (float)(cy + 16), "NOVUSS", COL_GOLD, 2.0f * ui, 1);
        drawText(640.0f, (float)(cy + 52), I18n::tr(lang, "tagline"), COL_DIM, 1.0f * ui, 1);

        int ry = cy + 76;
        int rh = 26;
        int rowStep = 29;
        int rowIdx = 0;

        auto drawMenuRow = [&](const std::string& label, const std::string& val) {
            if (rowIdx == app.menuRow) {
                drawRoundRect((float)(cx + 20), (float)(ry - 3), (float)(cw - 40), (float)rh, 6.0f * ui, COL_ROW_SEL);
                drawStrokeRoundRect((float)(cx + 20), (float)(ry - 3), (float)(cw - 40), (float)rh, 6.0f * ui, 1.0f, COL_GOLD);
            }
            uint32_t col = (rowIdx == app.menuRow) ? COL_GOLD : COL_TEXT;
            drawText((float)(cx + 36), (float)(ry + 4), label, col, 1.0f * ui, 0);
            if (!val.empty()) {
                drawText((float)(cx + cw - 36), (float)(ry + 4), val, col, 1.0f * ui, 2);
            }
            ry += rowStep;
            ++rowIdx;
        };

        drawMenuRow(I18n::tr(lang, "opponent"), (app.settings.mode == "ai") ? I18n::tr(lang, "vsAI") : I18n::tr(lang, "vsHuman"));
        drawMenuRow(I18n::tr(lang, "level"), I18n::tr(lang, app.settings.level));
        drawMenuRow(I18n::tr(lang, "match"), app.settings.sets == 1 ? I18n::tr(lang, "sets1") : (app.settings.sets == 2 ? I18n::tr(lang, "sets3") : I18n::tr(lang, "sets5")));
        drawMenuRow(I18n::tr(lang, "p1"), app.nameOf(0) + " [X]");
        drawMenuRow(I18n::tr(lang, "p2"), (app.settings.mode == "ai") ? I18n::tr(lang, "computer") : (app.nameOf(1) + " [X]"));
        drawMenuRow(I18n::tr(lang, "guide"), app.settings.guide ? "ON" : "OFF");
        drawMenuRow(I18n::tr(lang, "hints"), app.settings.hints ? "ON" : "OFF");
        std::string scLabel = (app.settings.shotControl == "stick")
                                  ? I18n::tr(lang, "ctrlStick")
                                  : ((app.settings.shotControl == "button")
                                         ? I18n::tr(lang, "ctrlButton")
                                         : I18n::tr(lang, "ctrlBoth"));
        drawMenuRow(I18n::tr(lang, "shotControl"), scLabel);
        drawMenuRow(I18n::tr(lang, "language"), (app.settings.lang == "lv") ? "Latviešu" : "English");
        drawMenuRow(I18n::tr(lang, "sound"), app.settings.sound ? "ON" : "OFF");
        drawMenuRow(I18n::tr(lang, "start"), "");
        drawMenuRow(I18n::tr(lang, "resume"), "");
        drawMenuRow(I18n::tr(lang, "rules"), "");

        std::string credits = (lang == "lv")
            ? "laacz.lv (novuss.didnt.work) · PS3 ports: ciematnieks.lv"
            : "laacz.lv (novuss.didnt.work) · PS3 port: ciematnieks.lv";
        drawText(640.0f, (float)(cy + ch - 18), credits, COL_DIM, 0.85f * ui, 1);
    }

    // 13. In-Game Pause Overlay
    if (app.pauseOpen) {
        drawRect(0.0f, 0.0f, 1280.0f, 720.0f, 0x80000000u);
        int cw = 420, ch = 325;
        int cx = 640 - cw / 2, cy = 360 - ch / 2;
        drawRoundRect((float)cx, (float)cy, (float)cw, (float)ch, 14.0f * ui, COL_CARD);
        drawStrokeRoundRect((float)cx, (float)cy, (float)cw, (float)ch, 14.0f * ui, 2.0f, COL_GOLD);
        drawText(640.0f, (float)(cy + 18), I18n::tr(lang, "pause"), COL_GOLD, 1.8f * ui, 1);

        std::string scoreStr = app.nameOf(0) + "  " + i2s(app.game.sets[0]) + " : " + i2s(app.game.sets[1]) + "  " + app.nameOf(1);
        drawText(640.0f, (float)(cy + 48), scoreStr, COL_DIM, 0.95f * ui, 1);

        int ry = cy + 76;
        int rh = 26;
        int rowStep = 32;
        int rowIdx = 0;

        auto drawPauseRow = [&](const std::string& label, const std::string& val) {
            if (rowIdx == app.pauseRow) {
                drawRoundRect((float)(cx + 24), (float)(ry - 3), (float)(cw - 48), (float)rh, 6.0f * ui, COL_ROW_SEL);
                drawStrokeRoundRect((float)(cx + 24), (float)(ry - 3), (float)(cw - 48), (float)rh, 6.0f * ui, 1.0f, COL_GOLD);
            }
            uint32_t col = (rowIdx == app.pauseRow) ? COL_GOLD : COL_TEXT;
            drawText((float)(cx + 38), (float)(ry + 4), label, col, 1.0f * ui, 0);
            if (!val.empty()) {
                drawText((float)(cx + cw - 38), (float)(ry + 4), val, col, 1.0f * ui, 2);
            }
            ry += rowStep;
            ++rowIdx;
        };

        std::string scLabel = (app.settings.shotControl == "stick")
                                  ? I18n::tr(lang, "ctrlStick")
                                  : ((app.settings.shotControl == "button")
                                         ? I18n::tr(lang, "ctrlButton")
                                         : I18n::tr(lang, "ctrlBoth"));

        drawPauseRow(I18n::tr(lang, "resume"), "");
        drawPauseRow(I18n::tr(lang, "restartSet"), "");
        drawPauseRow(I18n::tr(lang, "rules"), "");
        drawPauseRow(I18n::tr(lang, "shotControl"), scLabel);
        drawPauseRow(I18n::tr(lang, "sound"), app.settings.sound ? "ON" : "OFF");
        drawPauseRow(I18n::tr(lang, "toMenu"), "");

        std::string hint = (lang == "lv") ? "X: apstiprināt · O: turpināt" : "X: select · O: resume";
        drawText(640.0f, (float)(cy + ch - 20), hint, COL_DIM, 0.85f * ui, 1);
    }

    // 14. Rules Overlay
    if (app.rulesOpen) {
        drawRect(0.0f, 0.0f, 1280.0f, 720.0f, 0xB0000000u);
        int cw = 780, ch = 520;
        int cx = 640 - cw / 2, cy = 360 - ch / 2;
        drawRoundRect((float)cx, (float)cy, (float)cw, (float)ch, 14.0f * ui, COL_CARD);
        drawStrokeRoundRect((float)cx, (float)cy, (float)cw, (float)ch, 14.0f * ui, 2.0f, COL_GOLD);
        drawText(640.0f, (float)(cy + 16), I18n::tr(lang, "rulesTitle"), COL_GOLD, 1.5f * ui, 1);

        int clipTop = cy + 46;
        int clipBot = cy + ch - 34;
        int ty = clipTop - (int)(app.rulesScroll * 24);

        for (const auto& sec : I18n::rules(lang)) {
            if (ty >= clipTop - 20 && ty <= clipBot) {
                drawText((float)(cx + 30), (float)ty, sec.heading, COL_GOLD, 1.15f * ui, 0);
            }
            ty += 22;
            auto lines = Render::wrapText(sec.body, cw - 60, 0.9f * ui);
            for (const auto& line : lines) {
                if (ty >= clipTop - 16 && ty <= clipBot) {
                    drawText((float)(cx + 30), (float)ty, line, COL_TEXT, 0.9f * ui, 0);
                }
                ty += 18;
            }
            ty += 12;
        }

        std::string rHint = (lang == "lv") ? "D-Pad / Puļķis: ritināt · O / START: aizvērt" : "D-Pad / Stick: scroll · O / START: close";
        drawText(640.0f, (float)(cy + ch - 20), rHint, COL_DIM, 0.85f * ui, 1);
    }

    // 15. Match Result Overlay
    if (app.resultOpen) {
        drawRect(0.0f, 0.0f, 1280.0f, 720.0f, 0x80000000u);
        int cw = 440, ch = 240;
        int cx = 640 - cw / 2, cy = 360 - ch / 2;
        drawRoundRect((float)cx, (float)cy, (float)cw, (float)ch, 14.0f * ui, COL_CARD);
        drawStrokeRoundRect((float)cx, (float)cy, (float)cw, (float)ch, 14.0f * ui, 2.0f, COL_GOLD);
        drawText(640.0f, (float)(cy + 20), app.resultTitle, COL_GOLD, 1.6f * ui, 1);
        drawText(640.0f, (float)(cy + 60), app.resultScore, COL_TEXT, 1.3f * ui, 1);
        drawText(640.0f, (float)(cy + 96), app.resultText, COL_DIM, 1.0f * ui, 1);

        drawRoundRect(640.0f - 100.0f, (float)(cy + 150), 200.0f, 36.0f, 6.0f * ui, COL_ROW_SEL);
        drawStrokeRoundRect(640.0f - 100.0f, (float)(cy + 150), 200.0f, 36.0f, 6.0f * ui, 1.0f, COL_GOLD);
        drawText(640.0f, (float)(cy + 160), app.resultBtnLabel + " [X]", COL_GOLD, 1.0f * ui, 1);
    }

    // 16. Profiler HUD (R3 to toggle)
    if (app.profiler.enabled) {
        int pw = 280, ph = 96;
        int px = 1280 - padX - pw;
        int py = padY + topH + 8;
        drawRoundRect((float)px, (float)py, (float)pw, (float)ph, 6.0f * ui, 0xD0140E09u);
        drawStrokeRoundRect((float)px, (float)py, (float)pw, (float)ph, 6.0f * ui, 1.0f, COL_GOLD);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "RSX GPU: 60.0 FPS  Tiny3D HW");
        drawText((float)(px + 10), (float)(py + 8), buf, COL_GOLD, 0.85f * ui, 0);

        std::snprintf(buf, sizeof(buf), "Frame: %.2f ms (%.1f fps)", app.profiler.frameTimeMs, app.profiler.fps);
        drawText((float)(px + 10), (float)(py + 26), buf, COL_TEXT, 0.80f * ui, 0);

        std::snprintf(buf, sizeof(buf), "Update: %.2f ms  Render: %.2f ms", app.profiler.updateTimeMs, app.profiler.renderTimeMs);
        drawText((float)(px + 10), (float)(py + 44), buf, COL_DIM, 0.80f * ui, 0);

        std::snprintf(buf, sizeof(buf), "Heap: %.1f MB  Pipeline: RSX Direct", (double)app.profiler.heapBytes / (1024.0 * 1024.0));
        drawText((float)(px + 10), (float)(py + 62), buf, COL_DIM, 0.80f * ui, 0);
    }

    // 17. Hardware VSYNC Flip
    tiny3d_Flip();
}

} // namespace RenderTiny3D
} // namespace Novuss
#endif
