#include "render_internal.hpp"
#include <cstring>
#include <vector>
#include <cmath>
#include <algorithm>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"
#pragma GCC diagnostic pop

namespace Novuss {
namespace Render {

using std::min;
using std::max;

static inline double hyp(double x, double y) { return std::sqrt(x * x + y * y); }

static bool loadCustomBoard(std::vector<uint32_t>& tex, int size) {
    static const char* kPaths[] = {
        "/dev_hdd0/game/NOVU00001/USRDIR/board.png",
        "USRDIR/board.png",
        "assets/board.png",
        "assets/board.jpg",
        nullptr
    };
    int w = 0, h = 0, ch = 0;
    stbi_uc* data = nullptr;
    for (int i = 0; kPaths[i]; ++i) {
        data = stbi_load(kPaths[i], &w, &h, &ch, 4);
        if (data) break;
    }
    if (!data) return false;

    tex.resize((size_t)size * size);
    float stepX = (float)(w - 1) / (float)(size > 1 ? size - 1 : 1);
    float stepY = (float)(h - 1) / (float)(size > 1 ? size - 1 : 1);
    for (int y = 0; y < size; ++y) {
        float srcY = y * stepY;
        int y0 = (int)srcY;
        int y1 = std::min(y0 + 1, h - 1);
        float fy = srcY - y0;
        for (int x = 0; x < size; ++x) {
            float srcX = x * stepX;
            int x0 = (int)srcX;
            int x1 = std::min(x0 + 1, w - 1);
            float fx = srcX - x0;

            const stbi_uc* p00 = data + ((size_t)y0 * w + x0) * 4;
            const stbi_uc* p10 = data + ((size_t)y0 * w + x1) * 4;
            const stbi_uc* p01 = data + ((size_t)y1 * w + x0) * 4;
            const stbi_uc* p11 = data + ((size_t)y1 * w + x1) * 4;

            float w00 = (1.0f - fx) * (1.0f - fy);
            float w10 = fx * (1.0f - fy);
            float w01 = (1.0f - fx) * fy;
            float w11 = fx * fy;

            uint32_t r = (uint32_t)(p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11 + 0.5f);
            uint32_t g = (uint32_t)(p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11 + 0.5f);
            uint32_t b = (uint32_t)(p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11 + 0.5f);

            tex[(size_t)y * size + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    stbi_image_free(data);
    return true;
}

// ---------------------------------------------------------------------------
// DiscSprite: pre-rendered disc image for the 3 standard piece kinds.
// ---------------------------------------------------------------------------
struct DiscSprite {
    int size = 0, half = 0;
    double r = 0;
    std::vector<uint32_t> px;
};

// ---------------------------------------------------------------------------
// evalDiscPixel: shared pixel shader for a disc (puck or striker).
// Returns (outA << 24) | outRGB, or 0 inside the open hole.
// ---------------------------------------------------------------------------
static uint32_t evalDiscPixel(double dx, double dy, double r, double holeR, int kindIdx, double alphaScale) {
    if (r <= 0.0) return 0u;
    double d = std::sqrt(dx * dx + dy * dy);
    if (d < holeR - 0.5) return 0u;

    // 3-stop gradient colors.
    uint32_t c0, c1, c2;
    if (kindIdx == 0) { c0 = 0xFFEC5C62u; c1 = 0xFFC3282Fu; c2 = 0xFF7C1217u; }
    else if (kindIdx == 1) { c0 = 0xFF686868u; c1 = 0xFF282828u; c2 = 0xFF0B0B0Bu; }
    else { c0 = 0xFFFBEDCFu; c1 = 0xFFE6CB93u; c2 = 0xFFB38750u; }

    // Radial gradient (top -> bottom).
    double t = (dy + r) / (2.0 * r);
    if (t < 0.0) t = 0.0; if (t > 1.0) t = 1.0;
    uint32_t col;
    if (t < 0.5) col = lerpColor(c0, c1, t * 2.0);
    else col = lerpColor(c1, c2, (t - 0.5) * 2.0);

    // Striker wood-grain arcs.
    if (kindIdx == 2) {
        double ang = std::atan2(dy, dx);
        double grain = std::sin(ang * 9.0 + d * 0.35);
        if (grain > 0.55) col = lerpColor(col, 0xFF8A5A2Eu, (grain - 0.55) * 0.6);
    }

    // Smooth 0.62*r ring: specular on pucks, groove on striker.
    double ringR = 0.62 * r;
    double ringW = max(1.0, r * 0.06);
    double ringD = std::fabs(d - ringR);
    if (ringD < ringW) {
        double k = 1.0 - ringD / ringW;
        if (kindIdx == 2) col = lerpColor(col, 0xFF785023u, k * 0.85);
        else col = lerpColor(col, 0xFFFFFFFFu, k * 0.55);
    }

    // Smooth outer rim.
    double rimW = max(1.0, r * 0.08);
    double rimD = std::fabs(d - (r - 0.5));
    if (rimD < rimW) {
        double k = 1.0 - rimD / rimW;
        col = lerpColor(col, 0xFF000000u, k * 0.45);
    }

    // Smooth hole rim (annular shadow).
    if (holeR > 0.5) {
        double hw = max(1.0, holeR * 0.35);
        double hd = d - holeR;
        if (hd >= 0.0 && hd < hw) {
            double k = 1.0 - hd / hw;
            col = lerpColor(col, 0xFF000000u, k * 0.55);
        }
    }

    // Outer edge coverage.
    double cov = r + 0.5 - d;
    if (cov <= 0.0) return 0u;
    if (cov > 1.0) cov = 1.0;

    double a = alphaScale * cov;
    if (a <= 0.0) return 0u;
    if (a > 1.0) a = 1.0;
    uint32_t outA = (uint32_t)(a * 255.0 + 0.5);
    if (outA == 0u) return 0u;
    return (outA << 24) | (col & 0x00FFFFFFu);
}

// ---------------------------------------------------------------------------
// buildDiscSprite: pre-renders a disc into a sprite buffer.
// ---------------------------------------------------------------------------
static void buildDiscSprite(DiscSprite& sp, double r, double holeR, int kindIdx) {
    int half = (int)std::ceil(r * 1.18 + 3.0);
    int size = half * 2 + 1;
    sp.size = size;
    sp.half = half;
    sp.r = r;
    sp.px.assign((size_t)size * size, 0u);
    for (int iy = 0; iy < size; ++iy)
        for (int ix = 0; ix < size; ++ix)
            sp.px[(size_t)iy * size + ix] = evalDiscPixel(ix - half, iy - half, r, holeR, kindIdx, 1.0);
}

// ---------------------------------------------------------------------------
// drawDisc: renders a single disc directly into the framebuffer.
// ---------------------------------------------------------------------------
static void drawDisc(uint32_t* fb, int w, int h, int stride, double sx, double sy,
                     double r, double holeR, int kindIdx, float alpha) {
    if (r <= 0.0) return;
    double a = max(0.0, min(1.0, (double)alpha));
    int rInt = (int)std::ceil(r + 1.0);
    int cx = (int)std::floor(sx);
    int cy = (int)std::floor(sy);
    int y0 = max(0, cy - rInt), y1 = min(h - 1, cy + rInt);
    int x0 = max(0, cx - rInt), x1 = min(w - 1, cx + rInt);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            uint32_t c = evalDiscPixel(x + 0.5 - sx, y + 0.5 - sy, r, holeR, kindIdx, a);
            if (c != 0) blendPixel(fb, w, h, stride, x, y, c);
        }
}

// ---------------------------------------------------------------------------
// blitDiscSprite: blits a pre-rendered disc sprite centered at (sx, sy).
// ---------------------------------------------------------------------------
static void blitDiscSprite(uint32_t* fb, int w, int h, int stride, double sx, double sy, const DiscSprite& sp) {
    if (sp.size <= 0) return;
    int cx = (int)std::floor(sx + 0.5);
    int cy = (int)std::floor(sy + 0.5);
    for (int iy = 0; iy < sp.size; ++iy) {
        int py = cy - sp.half + iy;
        if (py < 0 || py >= h) continue;
        for (int ix = 0; ix < sp.size; ++ix) {
            int px = cx - sp.half + ix;
            if (px < 0 || px >= w) continue;
            uint32_t c = sp.px[(size_t)iy * sp.size + ix];
            if (c != 0) blendPixel(fb, w, h, stride, px, py, c);
        }
    }
}

// ---------------------------------------------------------------------------
// buildBoardTexture: Pre-renders the complete Novuss board with realistic wood textures
// into a square texture buffer. Generated once when scale changes.
// ---------------------------------------------------------------------------
static void buildBoardTexture(std::vector<uint32_t>& tex, int size, double scale) {
    if (loadCustomBoard(tex, size)) return;

    tex.assign((size_t)size * size, 0x00000000u);
    uint32_t* buf = tex.data();

    const double boardExtent = 1090.0; // from -45 to 1045
    const double boardOrigin = -45.0;
    const double invSize = boardExtent / (double)size;

    // Coordinates transform lambda: board (bx, by) -> texture pixel (tx, ty)
    auto toTex = [&](double bx, double by, double& tx, double& ty) {
        tx = (bx - boardOrigin) * (double)size / boardExtent;
        ty = (by - boardOrigin) * (double)size / boardExtent;
    };

    // 1. Wood texture generation per pixel.
    for (int iy = 0; iy < size; ++iy) {
        double by = boardOrigin + (iy + 0.5) * invSize;
        uint32_t* row = buf + (size_t)iy * size;

        for (int ix = 0; ix < size; ++ix) {
            double bx = boardOrigin + (ix + 0.5) * invSize;

            // Check if outside table outer boundary [-45, 1045]
            if (bx < -45.0 || bx > 1045.0 || by < -45.0 || by > 1045.0) {
                row[ix] = 0x00000000u;
                continue;
            }

            // Check if on cushion rails (outer 45mm border)
            bool isRail = (bx < 0.0 || bx > 1000.0 || by < 0.0 || by > 1000.0);

            if (isRail) {
                // Mahogany hardwood rails with directional grain
                // North/South rails have horizontal grain along X
                // West/East rails have vertical grain along Y
                bool isHorizontal = (by < 0.0 || by > 1000.0);
                double primaryAxis = isHorizontal ? bx : by;
                double secondaryAxis = isHorizontal ? by : bx;

                // Rich mahogany wood grain formula
                double grain = std::sin(primaryAxis * 0.14 + std::sin(secondaryAxis * 0.45) * 2.2);
                double fineGrain = std::sin(primaryAxis * 0.40 + std::sin(secondaryAxis * 0.90) * 1.5);
                double g = 0.50 + 0.35 * grain + 0.15 * fineGrain;
                if (g < 0.0) g = 0.0; if (g > 1.0) g = 1.0;

                uint32_t wood = lerpColor(0xFF632B15u, 0xFF3F170Au, g);

                // Outer border bevel highlight / shadow
                double distOuter = std::min(std::min(bx - (-45.0), 1045.0 - bx),
                                            std::min(by - (-45.0), 1045.0 - by));
                if (distOuter < 4.0) {
                    double tEdge = distOuter / 4.0;
                    if (by < 500.0 || bx < 500.0) {
                        // Top/left outer highlight
                        wood = lerpColor(0xFF884020u, wood, tEdge);
                    } else {
                        // Bottom/right outer shadow
                        wood = lerpColor(0xFF220C05u, wood, tEdge);
                    }
                }

                // Inner cushion edge highlight (along playing field border 0 and 1000)
                double distInner = std::min(std::min(std::fabs(bx), std::fabs(bx - 1000.0)),
                                            std::min(std::fabs(by), std::fabs(by - 1000.0)));
                if (distInner < 3.0) {
                    double tInner = distInner / 3.0;
                    wood = lerpColor(0xFF200C06u, wood, tInner);
                }

                row[ix] = wood;
            } else {
                // Birch plywood playing surface (saplāksnis / finieris)
                // Natural Baltic birch: longitudinal grain fibers running parallel to Y axis
                double grain = std::sin(by * 0.085 + std::sin(bx * 0.018) * 3.6 + std::sin((bx + by) * 0.004) * 4.5);
                double fiber = std::sin(by * 0.38 + std::sin(bx * 0.12) * 1.8);
                double g = 0.50 + 0.34 * grain + 0.16 * fiber;
                if (g < 0.0) g = 0.0; if (g > 1.0) g = 1.0;

                uint32_t wood = lerpColor(0xFFF6EBD5u, 0xFFE5CEABu, g);

                // Overhead soft lighting vignette centered at (500, 450)
                double dLight = hyp(bx - 500.0, by - 450.0);
                double lightT = dLight / 750.0;
                if (lightT > 1.0) lightT = 1.0;
                wood = lerpColor(wood, 0xFFD8BE96u, lightT * 0.25);

                // Perimeter cushion inner drop shadow (14mm)
                double distCushion = std::min(std::min(bx, 1000.0 - bx), std::min(by, 1000.0 - by));
                if (distCushion < 15.0) {
                    double tSh = 1.0 - (distCushion / 15.0);
                    uint32_t shadowCol = 0xFF2A1608u;
                    wood = lerpColor(wood, shadowCol, tSh * 0.38);
                }

                row[ix] = wood;
            }
        }
    }

    // 2. Corner miter joint lines (45-degree angle seams at corners).
    {
        double ax, ay, bx, by;
        double miterW = max(1.0, 1.2 * scale);
        // Top-left
        toTex(-45, -45, ax, ay); toTex(0, 0, bx, by);
        drawLineAA(buf, size, size, size, ax, ay, bx, by, miterW, 0x80180802u);
        // Top-right
        toTex(1045, -45, ax, ay); toTex(1000, 0, bx, by);
        drawLineAA(buf, size, size, size, ax, ay, bx, by, miterW, 0x80180802u);
        // Bottom-left
        toTex(-45, 1045, ax, ay); toTex(0, 1000, bx, by);
        drawLineAA(buf, size, size, size, ax, ay, bx, by, miterW, 0x80180802u);
        // Bottom-right
        toTex(1045, 1045, ax, ay); toTex(1000, 1000, bx, by);
        drawLineAA(buf, size, size, size, ax, ay, bx, by, miterW, 0x80180802u);
    }

    // 3. Exact LNF lines on the wood surface.
    {
        uint32_t lineCol = 0xFF1B1209u;
        double thick = std::max(1.2, 1.1 * scale);
        static const double segs[10][4] = {
            {130, 0, 130, 1000}, {870, 0, 870, 1000},
            {0, 130, 1000, 130}, {0, 870, 1000, 870},
            {500, 0, 500, 130}, {500, 870, 500, 1000},
            {0, 500, 130, 500}, {870, 500, 1000, 500},
            {375, 500, 625, 500}, {500, 375, 500, 625}
        };
        for (int i = 0; i < 10; ++i) {
            double ax, ay, bx, by;
            toTex(segs[i][0], segs[i][1], ax, ay);
            toTex(segs[i][2], segs[i][3], bx, by);
            drawLineAA(buf, size, size, size, ax, ay, bx, by, thick, lineCol);
        }
        double ccx, ccy;
        toTex(500, 500, ccx, ccy);
        drawCircleRingAA(buf, size, size, size, ccx, ccy, C::CIRCLE_R * scale, thick, lineCol);
    }

    // 4. Corner pockets.
    for (int i = 0; i < 4; ++i) {
        double px, py;
        toTex(POCKETS[i][0], POCKETS[i][1], px, py);
        double pr = C::POCKET_R * scale;
        // Deep hole
        fillCircleAA(buf, size, size, size, px, py, pr, 0xFF0A0604u);
        // Antique brass collar ring
        drawCircleRingAA(buf, size, size, size, px, py, pr, max(1.2, 1.8 * scale), 0x77C6A05Au);
        // Inner shadow depth ring
        drawCircleRingAA(buf, size, size, size, px, py, pr - max(1.0, 1.5 * scale), max(1.0, 1.2 * scale), 0x99000000u);
    }
}

// ---------------------------------------------------------------------------
// renderTableAndPieces
// ---------------------------------------------------------------------------
void renderTableAndPieces(const App& app, uint32_t* fb, int w, int h, int stride, const ViewTransform& v) {
    const Game& g = app.game;

    // Static baked board texture buffer (rebuilt ONLY when scale changes).
    static std::vector<uint32_t> s_boardTex;
    static int s_boardSize = 0;
    static double s_boardScale = 0.0;
    static DiscSprite s_discs[3];
    static double s_discScale = 0.0;

    int boardSize = (int)std::ceil(1090.0 * v.scale);
    if (boardSize < 64) boardSize = 64;

    bool rebuildBoard = (s_boardSize != boardSize || std::fabs(s_boardScale - v.scale) > 1e-4);
    if (rebuildBoard) {
        buildBoardTexture(s_boardTex, boardSize, v.scale);
        s_boardSize = boardSize;
        s_boardScale = v.scale;
    }

    // Disc sprites: rebuild only when scale changes.
    if (std::fabs(s_discScale - v.scale) > 1e-4) {
        buildDiscSprite(s_discs[0], C::PUCK_R * v.scale, C::PUCK_HOLE_R * v.scale, 0);
        buildDiscSprite(s_discs[1], C::PUCK_R * v.scale, C::PUCK_HOLE_R * v.scale, 1);
        buildDiscSprite(s_discs[2], C::STRIKER_R * v.scale, C::STRIKER_HOLE_R * v.scale, 2);
        s_discScale = v.scale;
    }

    // 1. Vertical background gradient (room ambiance).
    for (int y = 0; y < h; ++y) {
        double t = (h > 1) ? (double)y / (double)(h - 1) : 0.0;
        uint32_t bgCol = lerpColor(0xFF16100Bu, 0xFF0B0806u, t);
        blendSpan(fb + (size_t)y * stride, 0, w, bgCol);
    }

    // 2. Table shadow on floor.
    {
        double shDist = 12.0 * v.scale;
        int rx = (int)(v.cx - (double)boardSize * 0.5);
        int ry = (int)(v.cy - (double)boardSize * 0.5 + shDist);
        int rad = (int)(12.0 * v.scale);
        fillRoundRect(fb, w, h, stride, rx, ry, boardSize, boardSize, rad, 0x55000000u);
    }

    // 3. Render the board.
    const double PI = M_PI;
    bool rotZero = std::fabs(v.viewRot) < 1e-4;
    bool rotPi = std::fabs(std::fabs(v.viewRot) - PI) < 1e-4;

    int bx0 = (int)std::floor(v.cx - (double)boardSize * 0.5 + 0.5);
    int by0 = (int)std::floor(v.cy - (double)boardSize * 0.5 + 0.5);

    if (rotZero) {
        // Direct blit: 0° rotation (standard South player view)
        int yStart = max(0, by0);
        int yEnd = min(h, by0 + boardSize);
        int xStart = max(0, bx0);
        int xEnd = min(w, bx0 + boardSize);
        int spanW = xEnd - xStart;
        if (spanW > 0) {
            for (int y = yStart; y < yEnd; ++y) {
                int ty = y - by0;
                int tx = xStart - bx0;
                std::memcpy(&fb[(size_t)y * stride + xStart],
                            &s_boardTex[(size_t)ty * boardSize + tx],
                            (size_t)spanW * sizeof(uint32_t));
            }
        }
    } else if (rotPi) {
        // Direct blit: 180° rotation (standard North player view)
        int yStart = max(0, by0);
        int yEnd = min(h, by0 + boardSize);
        int xStart = max(0, bx0);
        int xEnd = min(w, bx0 + boardSize);
        for (int y = yStart; y < yEnd; ++y) {
            int ty = boardSize - 1 - (y - by0);
            uint32_t* row = fb + (size_t)y * stride;
            const uint32_t* srcRow = &s_boardTex[(size_t)ty * boardSize];
            for (int x = xStart; x < xEnd; ++x) {
                int tx = boardSize - 1 - (x - bx0);
                row[x] = srcRow[tx];
            }
        }
    } else {
        // Smooth rotation animation: fast fixed-point affine scanline mapper
        double halfDiag = (double)boardSize * 0.70710678 + 2.0;
        int yStart = max(0, (int)std::floor(v.cy - halfDiag));
        int yEnd = min(h, (int)std::ceil(v.cy + halfDiag) + 1);
        int xStart = max(0, (int)std::floor(v.cx - halfDiag));
        int xEnd = min(w, (int)std::ceil(v.cx + halfDiag) + 1);

        double cosA = std::cos(v.viewRot);
        double sinA = std::sin(v.viewRot);
        double halfSize = (double)boardSize * 0.5;

        int32_t stepU = (int32_t)(cosA * 65536.0);
        int32_t stepV = (int32_t)(-sinA * 65536.0);

        for (int y = yStart; y < yEnd; ++y) {
            uint32_t* row = fb + (size_t)y * stride;
            double dy = (double)y + 0.5 - v.cy;
            double dx0 = (double)xStart + 0.5 - v.cx;

            double u0 = dx0 * cosA + dy * sinA + halfSize;
            double v0 = -dx0 * sinA + dy * cosA + halfSize;

            int32_t curU = (int32_t)(u0 * 65536.0);
            int32_t curV = (int32_t)(v0 * 65536.0);

            for (int x = xStart; x < xEnd; ++x) {
                int tu = curU >> 16;
                int tv = curV >> 16;
                if ((unsigned)tu < (unsigned)boardSize && (unsigned)tv < (unsigned)boardSize) {
                    uint32_t c = s_boardTex[(size_t)tv * boardSize + tu];
                    if (c != 0) row[x] = c;
                }
                curU += stepU;
                curV += stepV;
            }
        }
    }

    double sx, sy;

    // 4. Shooter placement highlight.
    if (app.phase == Phase::Place) {
        char side = g.sides[g.turn];
        double y0 = (side == 'S') ? 870.0 : 0.0;
        double y1 = (side == 'S') ? 1000.0 : 130.0;
        double ax, ay, bx, by;
        v.toScreen(130, y0, ax, ay);
        v.toScreen(870, y1, bx, by);
        int rx = (int)std::min(ax, bx);
        int ry = (int)std::min(ay, by);
        int rw = (int)std::fabs(bx - ax);
        int rh = (int)std::fabs(by - ay);
        fillRect(fb, w, h, stride, rx, ry, rw, rh, 0x14D4AF37u);
    }

    // 5. Pucks & Striker.
    if (app.sim) {
        for (const Body& b : app.sim->bodies) {
            if (!b.on) continue;
            v.toScreen(b.x, b.y, sx, sy);
            int kindIdx = b.striker ? 2 : (b.id >= 0 && b.id < (int)g.pucks.size() ? g.pucks[b.id].owner : 0);
            blitDiscSprite(fb, w, h, stride, sx, sy, s_discs[kindIdx]);
        }
    } else {
        for (const Puck& k : g.pucks) {
            if (!k.on) continue;
            v.toScreen(k.x, k.y, sx, sy);
            blitDiscSprite(fb, w, h, stride, sx, sy, s_discs[k.owner]);
            if (app.helpersHints && app.phase == Phase::Place && k.owner == g.turn && g.isDark(k)) {
                drawCircleRingAA(fb, w, h, stride, sx, sy, (C::PUCK_R + 3.5) * v.scale,
                                 std::max(1.5, 2.0 * v.scale), 0xCC3F7FC9u);
            }
        }
        if (app.hasStriker) {
            v.toScreen(app.strikerX, app.strikerY, sx, sy);
            blitDiscSprite(fb, w, h, stride, sx, sy, s_discs[2]);
        }
    }

    // 6. Effects (fall, pulse).
    for (const auto& e : app.effects) {
        double t = e.t / e.dur;
        if (t > 1.0) t = 1.0;
        if (e.kind == "pulse") {
            double r = (C::PUCK_R + 18.0 * t) * v.scale;
            double a = 1.0 - t;
            int id = e.id;
            double px = 500, py = 500;
            if (id >= 0 && id < (int)g.pucks.size()) {
                px = g.pucks[id].x; py = g.pucks[id].y;
            }
            v.toScreen(px, py, sx, sy);
            drawCircleRingAA(fb, w, h, stride, sx, sy, r, max(1.0, 2.0 * v.scale),
                             ((uint32_t)(a * 180.0) << 24) | 0x00FFD700u);
        } else if (e.kind == "fall") {
            double k = 1.0 - t;
            double curX = e.x + (e.px - e.x) * (1.0 - k * k);
            double curY = e.y + (e.py - e.y) * (1.0 - k * k);
            v.toScreen(curX, curY, sx, sy);
            drawDisc(fb, w, h, stride, sx, sy, e.r * k * v.scale, e.hole * k * v.scale,
                     e.kindIdx, (float)(k * 0.9));
        }
    }

    // 7. Aiming guide & ghost striker (Human and AI aim preview).
    if ((app.phase == Phase::Place || app.phase == Phase::AIAnim) && app.helpersGuide && app.hasStriker && !app.sim) {
        double dx = app.aimDir[0], dy = app.aimDir[1];
        RayHit hit = castRay(g.pucks, app.strikerX, app.strikerY, dx, dy, C::STRIKER_R);
        double gx = app.strikerX + dx * hit.t;
        double gy = app.strikerY + dy * hit.t;

        uint32_t aimCol = 0xFFFFF2D0u; // Bright ivory/gold by default
        if (hit.index >= 0) {
            const Puck& k = g.pucks[hit.index];
            bool ok = (k.owner == g.turn && !g.isDark(k) && !g.inOwnStrip(g.turn, k));
            aimCol = ok ? 0xFF2ECC71u : 0xFFE74C3Cu; // Crisp green if legal target, red if foul!
        } else if (!app.aimValid) {
            aimCol = 0xFFE74C3Cu; // Red warning when cue angle violates cushion line!
        }

        double startX = app.strikerX + dx * C::STRIKER_R;
        double startY = app.strikerY + dy * C::STRIKER_R;
        double sStartX, sStartY, agx, agy;
        v.toScreen(startX, startY, sStartX, sStartY);
        v.toScreen(gx, gy, agx, agy);

        double totalLen = hyp(agx - sStartX, agy - sStartY);
        if (totalLen > 2.0) {
            double ux = (agx - sStartX) / totalLen, uy = (agy - sStartY) / totalLen;
            double dashOn = 8.0 * v.scale, dashOff = 6.0 * v.scale;
            double pos = 0.0;
            while (pos < totalLen) {
                double segEnd = std::min(pos + dashOn, totalLen);
                double x0 = sStartX + ux * pos, y0 = sStartY + uy * pos;
                double x1 = sStartX + ux * segEnd, y1 = sStartY + uy * segEnd;
                // Dark drop shadow for razor-sharp visibility on light birch wood
                drawLineAA(fb, w, h, stride, x0 + 1.0, y0 + 1.0, x1 + 1.0, y1 + 1.0,
                           max(1.5, 2.8 * v.scale), 0x66000000u);
                drawLineAA(fb, w, h, stride, x0, y0, x1, y1,
                           max(1.5, 2.2 * v.scale), aimCol);
                pos = segEnd + dashOff;
            }
        }

        // Ghost striker ring.
        drawCircleRingAA(fb, w, h, stride, agx + 1.0, agy + 1.0, C::STRIKER_R * v.scale,
                         max(1.5, 2.8 * v.scale), 0x66000000u);
        drawCircleRingAA(fb, w, h, stride, agx, agy, C::STRIKER_R * v.scale,
                         max(1.5, 2.2 * v.scale), aimCol);

        // Deflection preview if aiming at a puck.
        if (hit.index >= 0) {
            const Puck& k = g.pucks[hit.index];
            double ddx = k.x - gx, ddy = k.y - gy;
            double dl = hyp(ddx, ddy);
            if (dl > 1e-6) {
                ddx /= dl; ddy /= dl;
                double ex2 = k.x + ddx * 130.0;
                double ey2 = k.y + ddy * 130.0;
                double ax2, ay2, bx2, by2;
                v.toScreen(k.x, k.y, ax2, ay2);
                v.toScreen(ex2, ey2, bx2, by2);
                drawLineAA(fb, w, h, stride, ax2 + 1.0, ay2 + 1.0, bx2 + 1.0, by2 + 1.0,
                           max(1.5, 2.4 * v.scale), 0x66000000u);
                drawLineAA(fb, w, h, stride, ax2, ay2, bx2, by2,
                           max(1.5, 1.9 * v.scale), aimCol);
            }
            // Mark target puck with gold ring.
            double tx, ty;
            v.toScreen(k.x, k.y, tx, ty);
            drawCircleRingAA(fb, w, h, stride, tx, ty, (C::PUCK_R + 3.0) * v.scale,
                             max(1.5, 2.2 * v.scale), 0xFFD4AF37u);
        }
    }

    // 8. Cue stick & power indicator.
    if ((app.phase == Phase::Place || app.phase == Phase::AIAnim) && app.hasStriker) {
        double dx = app.aimDir[0], dy = app.aimDir[1];
        double pull = 8.0 + 36.0 * app.aimShown;
        double length = 320.0;

        double bx0 = app.strikerX - dx * (C::STRIKER_R + pull);
        double by0 = app.strikerY - dy * (C::STRIKER_R + pull);
        double bx1 = app.strikerX - dx * (C::STRIKER_R + pull + length);
        double by1 = app.strikerY - dy * (C::STRIKER_R + pull + length);

        double sx0, sy0, sx1, sy1;
        v.toScreen(bx0, by0, sx0, sy0);
        v.toScreen(bx1, by1, sx1, sy1);

        // Cue body shadow.
        double shOff = 4.0 * v.scale;
        drawLineAA(fb, w, h, stride, sx0, sy0 + shOff, sx1, sy1 + shOff,
                   max(1.5, 4.0 * v.scale), 0x44000000u);

        // Cue stick: tapered body (wooden cue, red-tinted if invalid aim).
        uint32_t woodShaft = app.aimValid ? 0xFFC69C6Du : 0xFFB85042u;
        uint32_t woodHighlight = app.aimValid ? 0xFFE6C39Au : 0xFFD98274u;
        drawLineAA(fb, w, h, stride, sx0, sy0, sx1, sy1,
                   max(1.5, 3.4 * v.scale), woodShaft);
        drawLineAA(fb, w, h, stride, sx0, sy0, sx1, sy1,
                   max(1.0, 1.7 * v.scale), woodHighlight);

        // White cue tip.
        double tipX = app.strikerX - dx * (C::STRIKER_R + pull - 4.0);
        double tipY = app.strikerY - dy * (C::STRIKER_R + pull - 4.0);
        double tipX2 = app.strikerX - dx * (C::STRIKER_R + pull);
        double tipY2 = app.strikerY - dy * (C::STRIKER_R + pull);
        double stx0, sty0, stx1, sty1;
        v.toScreen(tipX, tipY, stx0, sty0);
        v.toScreen(tipX2, tipY2, stx1, sty1);
        drawLineAA(fb, w, h, stride, stx0, sty0, stx1, sty1,
                   max(1.5, 3.5 * v.scale), 0xFFFFFFFFu);

        // Power arc around striker when charging.
        if (app.aimShown > 0.005) {
            double gx, gy;
            v.toScreen(app.strikerX, app.strikerY, gx, gy);
            double ringR = (C::STRIKER_R + 8.0) * v.scale;
            double fill = app.aimShown;
            double arcR = ringR;
            int steps = 48;
            int filled = (int)(steps * fill);
            for (int i = 0; i < filled; ++i) {
                double a0 = -M_PI * 0.5 + (double)i / steps * 2.0 * M_PI;
                double a1 = -M_PI * 0.5 + (double)(i + 1) / steps * 2.0 * M_PI;
                double x0 = gx + std::cos(a0) * arcR;
                double y0 = gy + std::sin(a0) * arcR;
                double x1 = gx + std::cos(a1) * arcR;
                double y1 = gy + std::sin(a1) * arcR;
                drawLineAA(fb, w, h, stride, x0, y0, x1, y1, max(1.5, 3.0 * v.scale), 0xFFE6A23Cu);
            }

            // Vertical power bar.
            double barW = 6.0 * v.scale, barH = 60.0 * v.scale;
            double bx = gx + 30.0 * v.scale, by = gy - barH * 0.5;
            fillRect(fb, w, h, stride, (int)bx, (int)by, (int)barW, (int)barH, 0x88000000u);
            int fh = (int)(barH * fill);
            fillRect(fb, w, h, stride, (int)bx, (int)(by + barH - fh), (int)barW, fh, 0xFFD84332u);
        }
    }
}

} // namespace Render
} // namespace Novuss
