#pragma once
#include "app.hpp"
#include "font.hpp"
#include "i18n.hpp"
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

namespace Novuss {
namespace Render {

struct ViewTransform {
    double cx, cy, scale, viewRot;
    void toScreen(double bx, double by, double& sx, double& sy) const {
        double dx = bx - MID;
        double dy = by - MID;
        double c = std::cos(viewRot), s = std::sin(viewRot);
        double rx = dx * c - dy * s;
        double ry = dx * s + dy * c;
        sx = cx + rx * scale;
        sy = cy + ry * scale;
    }
};

inline uint32_t lerpColor(uint32_t a, uint32_t b, double t) {
    if (t <= 0.0) return a;
    if (t >= 1.0) return b;
    int aa = (int)((a >> 24) & 0xFF), ar = (int)((a >> 16) & 0xFF);
    int ag = (int)((a >> 8) & 0xFF), ab = (int)(a & 0xFF);
    int ba = (int)((b >> 24) & 0xFF), br = (int)((b >> 16) & 0xFF);
    int bg = (int)((b >> 8) & 0xFF), bb = (int)(b & 0xFF);
    int ra = aa + (int)((ba - aa) * t + 0.5);
    int rr = ar + (int)((br - ar) * t + 0.5);
    int rg = ag + (int)((bg - ag) * t + 0.5);
    int rb = ab + (int)((bb - ab) * t + 0.5);
    if (ra < 0) ra = 0; if (ra > 255) ra = 255;
    if (rr < 0) rr = 0; if (rr > 255) rr = 255;
    if (rg < 0) rg = 0; if (rg > 255) rg = 255;
    if (rb < 0) rb = 0; if (rb > 255) rb = 255;
    return ((uint32_t)ra << 24) | ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | (uint32_t)rb;
}

inline void blendSpan(uint32_t* row, int x0, int x1, uint32_t argb) {
    if (x1 <= x0) return;
    uint32_t a = (argb >> 24) & 0xFF;
    if (a == 0) return;
    if (a == 255) {
        for (int x = x0; x < x1; ++x) row[x] = argb;
        return;
    }
    uint32_t sr = (argb >> 16) & 0xFF;
    uint32_t sg = (argb >> 8) & 0xFF;
    uint32_t sb = argb & 0xFF;
    for (int x = x0; x < x1; ++x) {
        uint32_t d = row[x];
        uint32_t dr = (d >> 16) & 0xFF;
        uint32_t dg = (d >> 8) & 0xFF;
        uint32_t db = d & 0xFF;
        uint32_t nr = dr + (((sr - dr) * a + 128u) >> 8);
        uint32_t ng = dg + (((sg - dg) * a + 128u) >> 8);
        uint32_t nb = db + (((sb - db) * a + 128u) >> 8);
        row[x] = 0xFF000000u | (nr << 16) | (ng << 8) | nb;
    }
}

inline void blendPixel(uint32_t* fb, int w, int h, int stride, int x, int y, uint32_t argb) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    uint32_t a = (argb >> 24) & 0xFF;
    if (a == 0) return;
    uint32_t* p = fb + (size_t)y * (size_t)stride + (size_t)x;
    if (a == 255) { *p = argb; return; }
    uint32_t d = *p;
    uint32_t sr = (argb >> 16) & 0xFF;
    uint32_t sg = (argb >> 8) & 0xFF;
    uint32_t sb = argb & 0xFF;
    uint32_t dr = (d >> 16) & 0xFF;
    uint32_t dg = (d >> 8) & 0xFF;
    uint32_t db = d & 0xFF;
    uint32_t nr = dr + (((sr - dr) * a + 128u) >> 8);
    uint32_t ng = dg + (((sg - dg) * a + 128u) >> 8);
    uint32_t nb = db + (((sb - db) * a + 128u) >> 8);
    *p = 0xFF000000u | (nr << 16) | (ng << 8) | nb;
}

inline void fillRect(uint32_t* fb, int w, int h, int stride, int rx, int ry, int rw, int rh, uint32_t argb) {
    if (rw < 0) { rx += rw; rw = -rw; }
    if (rh < 0) { ry += rh; rh = -rh; }
    if (rw == 0 || rh == 0) return;
    int x0 = std::max(0, rx);
    int x1 = std::min(w, rx + rw);
    int y0 = std::max(0, ry);
    int y1 = std::min(h, ry + rh);
    if (x1 <= x0 || y1 <= y0) return;
    for (int y = y0; y < y1; ++y) {
        uint32_t* row = fb + (size_t)y * (size_t)stride;
        blendSpan(row, x0, x1, argb);
    }
}

inline void fillRoundRect(uint32_t* fb, int w, int h, int stride, int rx, int ry, int rw, int rh, int rad, uint32_t argb) {
    if (rw < 0) { rx += rw; rw = -rw; }
    if (rh < 0) { ry += rh; rh = -rh; }
    if (rw == 0 || rh == 0) return;
    if (rad < 0) rad = 0;
    int maxRad = std::min(rw, rh) / 2;
    if (rad > maxRad) rad = maxRad;
    if (rad == 0) { fillRect(fb, w, h, stride, rx, ry, rw, rh, argb); return; }
    int x0 = std::max(0, rx);
    int x1 = std::min(w, rx + rw);
    int y0 = std::max(0, ry);
    int y1 = std::min(h, ry + rh);
    if (x1 <= x0 || y1 <= y0) return;
    double r2 = (double)rad * (double)rad;
    for (int y = y0; y < y1; ++y) {
        int dy = 0;
        if (y < ry + rad) dy = (ry + rad) - y;
        else if (y >= ry + rh - rad) dy = y - (ry + rh - rad - 1);
        int dx = 0;
        if (dy > 0) {
            double rem = r2 - (double)dy * (double)dy;
            if (rem < 0.0) rem = 0.0;
            dx = rad - (int)std::sqrt(rem);
        }
        int sx0 = std::max(x0, rx + dx);
        int sx1 = std::min(x1, rx + rw - dx);
        if (sx1 <= sx0) continue;
        uint32_t* row = fb + (size_t)y * (size_t)stride;
        blendSpan(row, sx0, sx1, argb);
    }
}

inline void strokeRoundRect(uint32_t* fb, int w, int h, int stride, int rx, int ry, int rw, int rh, int rad, int thick, uint32_t argb) {
    if (rw < 0) { rx += rw; rw = -rw; }
    if (rh < 0) { ry += rh; rh = -rh; }
    if (rw == 0 || rh == 0) return;
    if (thick <= 0) return;
    if (rad < 0) rad = 0;
    int maxRad = std::min(rw, rh) / 2;
    if (rad > maxRad) rad = maxRad;
    int x0 = std::max(0, rx);
    int x1 = std::min(w, rx + rw);
    int y0 = std::max(0, ry);
    int y1 = std::min(h, ry + rh);
    if (x1 <= x0 || y1 <= y0) return;
    double r2 = (double)rad * (double)rad;
    for (int y = y0; y < y1; ++y) {
        int dy = 0;
        if (y < ry + rad) dy = (ry + rad) - y;
        else if (y >= ry + rh - rad) dy = y - (ry + rh - rad - 1);
        int ox = 0;
        if (dy > 0) {
            double rem = r2 - (double)dy * (double)dy;
            if (rem < 0.0) rem = 0.0;
            ox = rad - (int)std::sqrt(rem);
        }
        int outerL = std::max(x0, rx + ox);
        int outerR = std::min(x1, rx + rw - ox);
        if (outerR <= outerL) continue;
        uint32_t* row = fb + (size_t)y * (size_t)stride;
        if (y < ry + thick || y >= ry + rh - thick) {
            blendSpan(row, outerL, outerR, argb);
        } else {
            int ix = thick;
            if (dy > 0 && rad > thick) {
                double rIn = (double)rad - (double)thick;
                double remIn = rIn * rIn - (double)dy * (double)dy;
                ix = (remIn > 0.0) ? (rad - (int)std::sqrt(remIn)) : rad;
            }
            int leftL = std::max(x0, rx + ox);
            int leftR = std::min(x1, rx + ix);
            if (leftR > leftL) blendSpan(row, leftL, leftR, argb);
            int rightL = std::max(x0, rx + rw - ix);
            int rightR = std::min(x1, rx + rw - ox);
            if (rightR > rightL) blendSpan(row, rightL, rightR, argb);
        }
    }
}

inline void drawLineImpl(uint32_t* fb, int w, int h, int stride, double x0, double y0, double x1, double y1, double thick, double dashLen, double gapLen, uint32_t argb) {
    double dx = x1 - x0;
    double dy = y1 - y0;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) return;
    double ux = dx / len;
    double uy = dy / len;
    double half = thick * 0.5;
    uint32_t baseA = (argb >> 24) & 0xFF;
    uint32_t rgb = argb & 0x00FFFFFFu;
    double period = dashLen + gapLen;

    auto evalPixel = [&](int x, int y) {
        double px = (double)x + 0.5 - x0;
        double py = (double)y + 0.5 - y0;
        double proj = px * ux + py * uy;
        if (proj < -half || proj > len + half) return;
        if (dashLen > 0.0 && period > 0.0 && std::fmod(std::max(0.0, proj), period) > dashLen) return;
        double perp = std::fabs(px * (-uy) + py * ux);
        double cov = half + 0.5 - perp;
        if (cov <= 0.0) return;
        if (cov > 1.0) cov = 1.0;
        uint32_t na = (uint32_t)((double)baseA * cov + 0.5);
        blendPixel(fb, w, h, stride, x, y, rgb | (na << 24));
    };

    if (std::fabs(dx) >= std::fabs(dy)) {
        int minx = std::max(0, (int)std::floor(std::min(x0, x1) - half - 1.0));
        int maxx = std::min(w - 1, (int)std::ceil(std::max(x0, x1) + half + 1.0));
        double slope = dy / dx;
        double span = (half + 1.5) * len / std::fabs(dx);
        for (int x = minx; x <= maxx; ++x) {
            double yc = y0 + ((double)x + 0.5 - x0) * slope;
            int miny = std::max(0, (int)std::floor(yc - span));
            int maxy = std::min(h - 1, (int)std::ceil(yc + span));
            for (int y = miny; y <= maxy; ++y) evalPixel(x, y);
        }
    } else {
        int miny = std::max(0, (int)std::floor(std::min(y0, y1) - half - 1.0));
        int maxy = std::min(h - 1, (int)std::ceil(std::max(y0, y1) + half + 1.0));
        double slope = dx / dy;
        double span = (half + 1.5) * len / std::fabs(dy);
        for (int y = miny; y <= maxy; ++y) {
            double xc = x0 + ((double)y + 0.5 - y0) * slope;
            int minx = std::max(0, (int)std::floor(xc - span));
            int maxx = std::min(w - 1, (int)std::ceil(xc + span));
            for (int x = minx; x <= maxx; ++x) evalPixel(x, y);
        }
    }
}

inline void drawLineAA(uint32_t* fb, int w, int h, int stride, double x0, double y0, double x1, double y1, double thick, uint32_t argb) {
    drawLineImpl(fb, w, h, stride, x0, y0, x1, y1, thick, 0.0, 0.0, argb);
}

inline void fillCircleAA(uint32_t* fb, int w, int h, int stride, double cx, double cy, double r, uint32_t argb) {
    if (r <= 0.0) return;
    int y0 = (int)std::floor(cy - r - 1.0);
    int y1 = (int)std::ceil(cy + r + 1.0);
    int x0 = (int)std::floor(cx - r - 1.0);
    int x1 = (int)std::ceil(cx + r + 1.0);
    if (y1 < 0 || y0 >= h || x1 < 0 || x0 >= w) return;
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    if (x0 < 0) x0 = 0;
    if (x1 > w) x1 = w;
    double rOut = r + 0.5;
    double rOut2 = rOut * rOut;
    double rIn = std::max(0.0, r - 0.5);
    double rIn2 = rIn * rIn;
    uint32_t baseA = (argb >> 24) & 0xFF;
    uint32_t rgb = argb & 0x00FFFFFFu;
    for (int y = y0; y < y1; ++y) {
        double dy = (double)y + 0.5 - cy;
        double remOut = rOut2 - dy * dy;
        if (remOut <= 0.0) continue;
        double dxOut = std::sqrt(remOut);
        int ix0 = std::max(x0, (int)std::ceil(cx - dxOut - 0.5));
        int ix1 = std::min(x1, (int)std::floor(cx + dxOut - 0.5) + 1);
        if (ix1 <= ix0) continue;
        uint32_t* row = fb + (size_t)y * (size_t)stride;
        double remIn = rIn2 - dy * dy;
        int in0 = ix0, in1 = ix0;
        if (remIn > 0.0) {
            double dxIn = std::sqrt(remIn);
            in0 = std::max(0, (int)std::ceil(cx - dxIn - 0.5));
            in1 = std::min(w, (int)std::floor(cx + dxIn - 0.5) + 1);
            if (in1 < in0) in1 = in0;
            int s0 = std::max(ix0, in0);
            int s1 = std::min(ix1, in1);
            if (s1 > s0) blendSpan(row, s0, s1, argb);
        }
        for (int x = ix0; x < ix1; ++x) {
            if (x >= in0 && x < in1) continue;
            double px = (double)x + 0.5 - cx;
            double d = std::sqrt(px * px + dy * dy);
            double cov = rOut - d;
            if (cov <= 0.0) continue;
            if (cov > 1.0) cov = 1.0;
            uint32_t na = (uint32_t)((double)baseA * cov + 0.5);
            if (na == 0) continue;
            blendPixel(fb, w, h, stride, x, y, rgb | (na << 24));
        }
    }
}

inline void drawCircleRingAA(uint32_t* fb, int w, int h, int stride, double cx, double cy, double r, double thick, uint32_t argb) {
    if (r <= 0.0 || thick <= 0.0) return;
    double half = thick * 0.5;
    double rOut = r + half;
    double rIn = r - half;
    if (rIn < 0.0) rIn = 0.0;
    int y0 = (int)std::floor(cy - rOut - 1.0);
    int y1 = (int)std::ceil(cy + rOut + 1.0);
    int x0 = (int)std::floor(cx - rOut - 1.0);
    int x1 = (int)std::ceil(cx + rOut + 1.0);
    if (y1 < 0 || y0 >= h || x1 < 0 || x0 >= w) return;
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    if (x0 < 0) x0 = 0;
    if (x1 > w) x1 = w;
    double rOut2 = rOut * rOut;
    double rCore = std::max(0.0, rIn - 0.5);
    double rCore2 = rCore * rCore;
    uint32_t baseA = (argb >> 24) & 0xFF;
    uint32_t rgb = argb & 0x00FFFFFFu;
    for (int y = y0; y < y1; ++y) {
        double dy = (double)y + 0.5 - cy;
        double remOut = rOut2 - dy * dy;
        if (remOut <= 0.0) continue;
        double dxOut = std::sqrt(remOut);
        int ix0 = std::max(x0, (int)std::ceil(cx - dxOut - 0.5));
        int ix1 = std::min(x1, (int)std::floor(cx + dxOut - 0.5) + 1);
        if (ix1 <= ix0) continue;
        int in0 = ix0, in1 = ix0;
        double remCore = rCore2 - dy * dy;
        if (remCore > 0.0) {
            double dxCore = std::sqrt(remCore);
            in0 = (int)std::ceil(cx - dxCore);
            in1 = (int)std::floor(cx + dxCore);
        }
        for (int x = ix0; x < ix1; ++x) {
            if (x > in0 && x < in1) continue;
            double px = (double)x + 0.5 - cx;
            double d = std::sqrt(px * px + dy * dy);
            double cov = std::min(rOut + 0.5 - d, d - (rIn - 0.5));
            if (cov <= 0.0) continue;
            if (cov > 1.0) cov = 1.0;
            uint32_t na = (uint32_t)((double)baseA * cov + 0.5);
            if (na == 0) continue;
            blendPixel(fb, w, h, stride, x, y, rgb | (na << 24));
        }
    }
}

void renderTableAndPieces(const App& app, uint32_t* fb, int w, int h, int stride, const ViewTransform& v);

} // namespace Render
} // namespace Novuss
