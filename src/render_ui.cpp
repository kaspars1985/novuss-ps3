#include "app.hpp"
#include "render_internal.hpp"
#include "font.hpp"
#include "i18n.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Novuss {

using namespace Render;

namespace {

static std::string i2s(int v) { char b[32]; std::sprintf(b, "%d", v); return b; }

const uint32_t COL_GOLD = 0xFFE8C070u;
const uint32_t COL_TEXT = 0xFFF0E6D8u;
const uint32_t COL_DIM = 0xFF9A8C7Au;
const uint32_t COL_RED = 0xFFB03030u;
const uint32_t COL_BLACK = 0xFF202020u;
const uint32_t COL_CARD = 0xF01A120Cu;
const uint32_t COL_CARD_EDGE = 0xFF3A2A1Eu;
const uint32_t COL_ROW_SEL = 0xFF3A2A1Eu;

void drawPuckDots(uint32_t* fb, int w, int h, int stride, int x, int y, int count,
                  uint32_t color, float ui) {
    double step = 12.0 * ui;
    double rad = 4.0 * ui;
    for (int i = 0; i < count; ++i) {
        fillCircleAA(fb, w, h, stride, x + i * step, y, rad, color);
    }
}

void drawBadge(uint32_t* fb, int w, int h, int stride, int x, int y, int bw, int bh,
               const std::string& name, int pucks, int debt, bool active, uint32_t accent,
               float ui) {
    int rad = (int)(8 * ui);
    fillRoundRect(fb, w, h, stride, x, y, bw, bh, rad, 0xE0201610u);
    if (active) strokeRoundRect(fb, w, h, stride, x, y, bw, bh, rad, 2, COL_GOLD);
    else strokeRoundRect(fb, w, h, stride, x, y, bw, bh, rad, 1, COL_CARD_EDGE);
    fillCircleAA(fb, w, h, stride, x + 18 * ui, y + bh * 0.5, 8.0 * ui, accent);
    drawText(fb, w, h, stride, x + (int)(32 * ui), y + (int)(6 * ui), name, COL_TEXT, ui, 0);
    drawPuckDots(fb, w, h, stride, x + (int)(34 * ui), y + bh - (int)(12 * ui), pucks, accent, ui);
    if (debt > 0) {
        std::string d = "-" + i2s(debt);
        drawText(fb, w, h, stride, x + bw - (int)(8 * ui), y + (int)(6 * ui), d, 0xFFFF8080u, ui, 2);
    }
}

} // namespace

void App::render(uint32_t* fb, int w, int h, int stride) {
    const std::string& lang = settings.lang;

    // UI scale: 1.5x on 1080p-class displays, 1.0x otherwise.
    float ui = (h >= 950) ? 1.5f : 1.0f;

    // TV overscan safe-area padding.
    int padX = std::max((int)(40 * ui), (int)(w * 0.045));
    int padY = std::max((int)(28 * ui), (int)(h * 0.045));

    // 1. Table + pieces.
    int topH = (int)(52 * ui);
    int hudX = padX, hudY = padY, hudW = w - 2 * padX, hudH = topH;
    int logW = (int)(380 * ui);
    int px = padX, py = hudY + topH + (int)(8 * ui), pw = logW,
        ph = h - py - padY - (int)(26 * ui);
    int leftMargin = (settings.log && !menuOpen) ? (padX + logW + (int)(16 * ui)) : padX;
    int rightMargin = padX;
    int availW = w - leftMargin - rightMargin;
    int availH = h - (hudY + topH + (int)(10 * ui)) - (padY + (int)(28 * ui));
    ViewTransform v;
    v.scale = std::min(availW, availH) / 1110.0;
    v.cx = leftMargin + availW * 0.5;
    v.cy = hudY + topH + (int)(6 * ui) + availH * 0.5;
    v.viewRot = viewRot;
    Render::renderTableAndPieces(*this, fb, w, h, stride, v);

    // 2. Top HUD.
    fillRoundRect(fb, w, h, stride, hudX, hudY, hudW, hudH, (int)(10 * ui), 0xE018100Au);
    int badgeW = (int)(200 * ui), badgeH = (int)(40 * ui);
    int badgeY = hudY + (int)(6 * ui);
    drawBadge(fb, w, h, stride, hudX + (int)(10 * ui), badgeY, badgeW, badgeH, nameOf(0),
              game.onTable(0), game.debt[0], game.turn == 0, COL_RED, ui);
    drawBadge(fb, w, h, stride, hudX + hudW - (int)(10 * ui) - badgeW, badgeY, badgeW, badgeH,
              nameOf(1), game.onTable(1), game.debt[1], game.turn == 1, COL_BLACK, ui);

    std::string score = nameOf(0) + "  " + i2s(game.sets[0]) + " : " + i2s(game.sets[1]) + "  " + nameOf(1);
    drawText(fb, w, h, stride, w / 2, hudY + (int)(4 * ui), score, COL_GOLD, 1.35f * ui, 1);
    std::string setInfo = I18n::tr(lang, "setInfo",
        {{"n", i2s(game.setNo)}, {"w", i2s(game.setsToWin)}});
    drawText(fb, w, h, stride, w / 2, hudY + (int)(23 * ui), setInfo, COL_DIM, 1.0f * ui, 1);
    std::string status = (phase == Phase::AI || phase == Phase::AIAnim)
        ? I18n::tr(lang, "thinking", {{"name", nameOf(game.turn)}})
        : I18n::tr(lang, "yourShot", {{"name", nameOf(game.turn)}});
    drawText(fb, w, h, stride, w / 2, hudY + (int)(37 * ui), status, COL_TEXT, 1.0f * ui, 1);

    // 3. Game log panel.
    if (settings.log && !menuOpen) {
        fillRoundRect(fb, w, h, stride, px, py, pw, ph, (int)(10 * ui), 0xE0140E09u);
        strokeRoundRect(fb, w, h, stride, px, py, pw, ph, (int)(10 * ui), 1, COL_CARD_EDGE);
        drawText(fb, w, h, stride, px + (int)(12 * ui), py + (int)(10 * ui),
                 I18n::tr(lang, "logTitle"), COL_GOLD, 1.0f * ui, 0);
        int ty = py + (int)(34 * ui);
        int lineStep = (int)(16 * ui);
        int start = std::max(0, (int)matchLog.size() - 14);
        for (int i = start; i < (int)matchLog.size(); ++i) {
            const LogEntry& e = matchLog[i];
            std::string key = e.hasRes ? e.res.k : e.k;
            int who = e.hasRes ? (e.res.winner >= 0 ? e.res.winner : e.shooter) : e.who;
            std::string line = I18n::tr(lang, key,
                {{"name", nameOf(who)}, {"n", i2s(e.n)}});
            auto wrapped = wrapText(line, pw - (int)(24 * ui), 1.0f * ui);
            for (auto& ln : wrapped) {
                if (ty > py + ph - (int)(18 * ui)) break;
                drawText(fb, w, h, stride, px + (int)(12 * ui), ty, ln, COL_TEXT, 1.0f * ui, 0);
                ty += lineStep;
            }
            ty += (int)(4 * ui);
            if (ty > py + ph - (int)(18 * ui)) break;
        }
    }

    // 4. Bottom control hint inside a subtle pill (safe from TV overscan).
    {
        std::string hint = I18n::tr(lang, "placeHint");
        int hintY = h - padY - (int)(18 * ui);
        int tw = textWidth(hint, 1.0f * ui);
        int pillW = tw + (int)(28 * ui);
        int pillH = (int)(22 * ui);
        int pillX = w / 2 - pillW / 2;
        int pillY = hintY - (int)(4 * ui);
        fillRoundRect(fb, w, h, stride, pillX, pillY, pillW, pillH, (int)(11 * ui), 0x90000000u);
        drawText(fb, w, h, stride, w / 2, hintY, hint, COL_DIM, 1.0f * ui, 1);
    }

    // 5. Banner + toasts.
    if (bannerTimer > 0 && !bannerText.empty()) {
        int bw = textWidth(bannerText, 1.5f * ui) + (int)(40 * ui);
        int bh = (int)(40 * ui);
        int bx = w / 2 - bw / 2, by = hudY + hudH + (int)(16 * ui);
        fillRoundRect(fb, w, h, stride, bx, by, bw, bh, (int)(10 * ui), 0xF02A1E12u);
        strokeRoundRect(fb, w, h, stride, bx, by, bw, bh, (int)(10 * ui), 2, COL_GOLD);
        drawText(fb, w, h, stride, w / 2, by + (int)(12 * ui), bannerText, COL_GOLD, 1.5f * ui, 1);
    }
    int toastY = h - padY - (int)(60 * ui);
    int toastH = (int)(26 * ui);
    int toastStep = (int)(32 * ui);
    for (int i = (int)toasts.size() - 1; i >= 0 && i >= (int)toasts.size() - 4; --i) {
        const Toast& t = toasts[i];
        int tw = textWidth(t.text, 1.0f * ui) + (int)(24 * ui);
        int tx = w / 2 - tw / 2;
        fillRoundRect(fb, w, h, stride, tx, toastY, tw, toastH, (int)(8 * ui), 0xE0201610u);
        drawText(fb, w, h, stride, w / 2, toastY + (int)(6 * ui), t.text, COL_TEXT, 1.0f * ui, 1);
        toastY -= toastStep;
    }

    // 6. Menu overlay.
    if (menuOpen) {
        int cw = (int)(520 * ui), ch = (int)(470 * ui);
        int cx = w / 2 - cw / 2, cy = h / 2 - ch / 2;
        fillRoundRect(fb, w, h, stride, cx, cy, cw, ch, (int)(14 * ui), COL_CARD);
        strokeRoundRect(fb, w, h, stride, cx, cy, cw, ch, (int)(14 * ui), 2, COL_GOLD);
        drawText(fb, w, h, stride, w / 2, cy + (int)(16 * ui), "NOVUSS", COL_GOLD, 2.0f * ui, 1);
        drawText(fb, w, h, stride, w / 2, cy + (int)(52 * ui), I18n::tr(lang, "tagline"),
                 COL_DIM, 1.0f * ui, 1);

        std::vector<std::pair<std::string, std::string>> rows = {
            {I18n::tr(lang, "opponent"), settings.mode == "ai" ? I18n::tr(lang, "vsAI") : I18n::tr(lang, "vsHuman")},
            {I18n::tr(lang, "level"), I18n::tr(lang, settings.level)},
            {I18n::tr(lang, "match"), settings.sets == 1 ? I18n::tr(lang, "sets1") : (settings.sets == 2 ? I18n::tr(lang, "sets3") : I18n::tr(lang, "sets5"))},
            {I18n::tr(lang, "p1"), nameOf(0) + " [X]"},
            {I18n::tr(lang, "p2"), (settings.mode == "ai" ? I18n::tr(lang, "computer") : (nameOf(1) + " [X]"))},
            {I18n::tr(lang, "guide"), settings.guide ? "ON" : "OFF"},
            {I18n::tr(lang, "hints"), settings.hints ? "ON" : "OFF"},
            {I18n::tr(lang, "language"), settings.lang == "lv" ? "Latviešu" : "English"},
            {I18n::tr(lang, "sound"), settings.sound ? "ON" : "OFF"},
            {I18n::tr(lang, "start"), ""},
            {I18n::tr(lang, "resume"), ""},
            {I18n::tr(lang, "rules"), ""},
        };
        int ry = cy + (int)(76 * ui);
        int rh = (int)(26 * ui);
        int rowStep = (int)(29 * ui);
        for (int i = 0; i < (int)rows.size(); ++i) {
            if (i == menuRow) {
                fillRoundRect(fb, w, h, stride, cx + (int)(20 * ui), ry - (int)(3 * ui),
                              cw - (int)(40 * ui), rh, (int)(6 * ui), COL_ROW_SEL);
                strokeRoundRect(fb, w, h, stride, cx + (int)(20 * ui), ry - (int)(3 * ui),
                                cw - (int)(40 * ui), rh, (int)(6 * ui), 1, COL_GOLD);
            }
            uint32_t col = (i == menuRow) ? COL_GOLD : COL_TEXT;
            drawText(fb, w, h, stride, cx + (int)(36 * ui), ry + (int)(4 * ui),
                     rows[i].first, col, 1.0f * ui, 0);
            if (!rows[i].second.empty())
                drawText(fb, w, h, stride, cx + cw - (int)(36 * ui), ry + (int)(4 * ui),
                         rows[i].second, col, 1.0f * ui, 2);
            ry += rowStep;
        }

        std::string credits = (lang == "lv")
            ? "laacz.lv (novuss.didnt.work) · PS3 ports: ciematnieks.lv"
            : "laacz.lv (novuss.didnt.work) · PS3 port: ciematnieks.lv";
        drawText(fb, w, h, stride, w / 2, cy + ch - (int)(18 * ui), credits, COL_DIM, 0.85f * ui, 1);
    }

    // 7. Rules overlay (drawn after menu so it renders on top).
    if (rulesOpen) {
        fillRect(fb, w, h, stride, 0, 0, w, h, 0xB0000000u);
        int cw = std::min(w - 2 * padX, (int)(780 * ui));
        int ch = std::min(h - 2 * padY, (int)(520 * ui));
        int cx = w / 2 - cw / 2, cy = h / 2 - ch / 2;
        fillRoundRect(fb, w, h, stride, cx, cy, cw, ch, (int)(14 * ui), COL_CARD);
        strokeRoundRect(fb, w, h, stride, cx, cy, cw, ch, (int)(14 * ui), 2, COL_GOLD);
        drawText(fb, w, h, stride, w / 2, cy + (int)(16 * ui), I18n::tr(lang, "rulesTitle"),
                 COL_GOLD, 1.5f * ui, 1);

        int clipTop = cy + (int)(46 * ui);
        int clipBot = cy + ch - (int)(34 * ui);
        int ty = clipTop - (int)(rulesScroll * 24 * ui);
        int headStep = (int)(20 * ui);
        int lineStep = (int)(16 * ui);
        int secGap = (int)(12 * ui);
        const auto& secs = I18n::rules(lang);
        for (const auto& s : secs) {
            if (ty >= clipTop && ty + headStep <= clipBot)
                drawText(fb, w, h, stride, cx + (int)(24 * ui), ty, s.heading, COL_GOLD, 1.0f * ui, 0);
            ty += headStep;
            auto lines = wrapText(s.body, cw - (int)(64 * ui), 1.0f * ui);
            for (auto& ln : lines) {
                if (ty >= clipTop && ty + lineStep <= clipBot)
                    drawText(fb, w, h, stride, cx + (int)(24 * ui), ty, ln, COL_TEXT, 1.0f * ui, 0);
                ty += lineStep;
            }
            ty += secGap;
        }

        // Scrollbar track on the right edge of the card.
        int sbW = (int)(6 * ui);
        int sbX = cx + cw - (int)(14 * ui);
        int sbTop = clipTop;
        int sbBot = clipBot;
        int sbH = sbBot - sbTop;
        if (sbH > 0) {
            fillRoundRect(fb, w, h, stride, sbX, sbTop, sbW, sbH, sbW / 2, 0x40FFFFFFu);
            int thumbH = std::max((int)(24 * ui), sbH / 4);
            int thumbY = sbTop + (int)(rulesScroll * (sbH - thumbH));
            if (thumbY < sbTop) thumbY = sbTop;
            if (thumbY + thumbH > sbBot) thumbY = sbBot - thumbH;
            fillRoundRect(fb, w, h, stride, sbX, thumbY, sbW, thumbH, sbW / 2, COL_GOLD);
        }

        std::string footer = (lang == "lv")
            ? "Krusts Augšā/Lejā: ritināt · X / O: aizvērt"
            : "D-Pad Up/Down: scroll · X / O: close";
        drawText(fb, w, h, stride, w / 2, cy + ch - (int)(24 * ui), footer, COL_DIM, 1.0f * ui, 1);
    }

    // 8. Result overlay.
    if (resultOpen) {
        int cw = (int)(460 * ui), ch = (int)(260 * ui);
        int cx = w / 2 - cw / 2, cy = h / 2 - ch / 2;
        fillRoundRect(fb, w, h, stride, cx, cy, cw, ch, (int)(14 * ui), COL_CARD);
        strokeRoundRect(fb, w, h, stride, cx, cy, cw, ch, (int)(14 * ui), 2, COL_GOLD);
        drawText(fb, w, h, stride, w / 2, cy + (int)(24 * ui), resultTitle, COL_GOLD, 1.5f * ui, 1);
        drawText(fb, w, h, stride, w / 2, cy + (int)(64 * ui), resultScore, COL_TEXT, 1.0f * ui, 1);
        auto lines = wrapText(resultText, cw - (int)(48 * ui), 1.0f * ui);
        int ty = cy + (int)(96 * ui);
        int lineStep = (int)(16 * ui);
        for (auto& ln : lines) {
            drawText(fb, w, h, stride, w / 2, ty, ln, COL_DIM, 1.0f * ui, 1);
            ty += lineStep;
        }
        int bw = textWidth(resultBtnLabel, 1.0f * ui) + (int)(40 * ui);
        int bh = (int)(36 * ui);
        int bx = w / 2 - bw / 2, by = cy + ch - (int)(56 * ui);
        fillRoundRect(fb, w, h, stride, bx, by, bw, bh, (int)(8 * ui), 0xFF3A2A1Eu);
        strokeRoundRect(fb, w, h, stride, bx, by, bw, bh, (int)(8 * ui), 2, COL_GOLD);
        drawText(fb, w, h, stride, w / 2, by + (int)(10 * ui), resultBtnLabel, COL_GOLD, 1.0f * ui, 1);
    }

    // 9. On-screen name editor overlay.
    if (nameEditOpen) {
        fillRect(fb, w, h, stride, 0, 0, w, h, 0xB0000000u);
        int cw = (int)(520 * ui), ch = (int)(320 * ui);
        int cx = w / 2 - cw / 2, cy = h / 2 - ch / 2;
        fillRoundRect(fb, w, h, stride, cx, cy, cw, ch, (int)(14 * ui), COL_CARD);
        strokeRoundRect(fb, w, h, stride, cx, cy, cw, ch, (int)(14 * ui), 2, COL_GOLD);
        drawText(fb, w, h, stride, w / 2, cy + (int)(14 * ui),
                 I18n::tr(lang, nameEditPlayer == 0 ? "p1" : "p2"), COL_GOLD, 1.5f * ui, 1);

        // Input box.
        int ibW = cw - (int)(48 * ui);
        int ibH = (int)(30 * ui);
        int ibX = cx + (int)(24 * ui);
        int ibY = cy + (int)(48 * ui);
        fillRoundRect(fb, w, h, stride, ibX, ibY, ibW, ibH, (int)(6 * ui), 0xFF0E0906u);
        strokeRoundRect(fb, w, h, stride, ibX, ibY, ibW, ibH, (int)(6 * ui), 1, COL_CARD_EDGE);
        drawText(fb, w, h, stride, ibX + (int)(10 * ui), ibY + (int)(7 * ui),
                 nameEditBuf + "_", COL_GOLD, 1.0f * ui, 0);

        // 4x10 key grid (Latvian / English).
        static const char* kGridLV[4][10] = {
            {"A","B","C","D","E","F","G","H","I","J"},
            {"K","L","M","N","O","P","Q","R","S","T"},
            {"V","Z","\xC4\x80","\xC4\x8C","\xC4\x92","\xC4\xA2","\xC4\xAA","\xC4\xB6","\xC4\xBB","\xC5\x85"},
            {"\xC5\xA0","\xC5\xAA","\xC5\xBD","1","2","3","_","DEL","CLR","OK"}
        };
        static const char* kGridEN[4][10] = {
            {"A","B","C","D","E","F","G","H","I","J"},
            {"K","L","M","N","O","P","Q","R","S","T"},
            {"U","V","W","X","Y","Z","0","1","2","3"},
            {"4","5","6","7","8","9","_","DEL","CLR","OK"}
        };
        const char* const (*activeGrid)[10] = (lang == "en") ? kGridEN : kGridLV;
        int gridX = cx + (int)(24 * ui);
        int gridY = cy + (int)(90 * ui);
        int cellW = (cw - (int)(48 * ui)) / 10;
        int cellH = (int)(30 * ui);
        int cellGap = (int)(2 * ui);
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 10; ++c) {
                int kx = gridX + c * cellW;
                int ky = gridY + r * (cellH + cellGap);
                bool sel = (r == nameEditRow && c == nameEditCol);
                fillRoundRect(fb, w, h, stride, kx + cellGap, ky, cellW - 2 * cellGap, cellH,
                              (int)(4 * ui), sel ? COL_ROW_SEL : 0xFF1A120Cu);
                if (sel)
                    strokeRoundRect(fb, w, h, stride, kx + cellGap, ky, cellW - 2 * cellGap, cellH,
                                    (int)(4 * ui), 2, COL_GOLD);
                drawText(fb, w, h, stride, kx + cellW / 2, ky + (int)(7 * ui),
                         activeGrid[r][c], sel ? COL_GOLD : COL_TEXT, 1.0f * ui, 1);
            }
        }

        std::string kbHint = (lang == "en")
            ? "X: select · []: delete · START: save · O: cancel"
            : "X: izvēlēties · []: dzēst · START: saglabāt · O: atcelt";
        drawText(fb, w, h, stride, w / 2, cy + ch - (int)(22 * ui), kbHint, COL_DIM, 1.0f * ui, 1);
    }
}

} // namespace Novuss
