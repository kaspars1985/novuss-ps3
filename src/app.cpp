#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "i18n.hpp"

namespace Novuss {

namespace {

static std::string i2s(int v) {
    char b[32];
    std::sprintf(b, "%d", v);
    return b;
}

// Simple xorshift32 PRNG.
inline uint32_t xorshift(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Rotate a screen-space vector by viewRot (used to map stick input to table axes).
inline void rotateBy(double ang, double x, double y, double& ox, double& oy) {
    double c = std::cos(ang), s = std::sin(ang);
    ox = x * c - y * s;
    oy = x * s + y * c;
}

// Side-relative rotation for a shooter side.
inline double sideRot(char side) {
    return side == 'S' ? 0.0 : M_PI;
}

// Remove the last UTF-8 codepoint from a string.
inline void popUtf8(std::string& s) {
    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    if (!s.empty()) s.pop_back();
}

// Name editor grid (Latvian, matches render_ui.cpp).
static const char* kGridLV[4][10] = {
    {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"},
    {"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T"},
    {"V", "Z", "\xC4\x80", "\xC4\x8C", "\xC4\x92", "\xC4\xA2", "\xC4\xAA", "\xC4\xB6", "\xC4\xBB", "\xC5\x85"},
    {"\xC5\xA0", "\xC5\xAA", "\xC5\xBD", "1", "2", "3", "_", "DEL", "CLR", "OK"},
};

static const char* kGridLVLower[4][10] = {
    {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"},
    {"k", "l", "m", "n", "o", "p", "q", "r", "s", "t"},
    {"v", "z", "\xC4\x81", "\xC4\x8D", "\xC4\x93", "\xC4\xA3", "\xC4\xAB", "\xC4\xB7", "\xC4\xBC", "\xC5\x86"},
    {"\xC5\xA1", "\xC5\xAB", "\xC5\xBE", "1", "2", "3", "_", "DEL", "CLR", "OK"},
};

// English onscreen keyboard grid (matches render_ui.cpp).
static const char* kGridEN[4][10] = {
    {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"},
    {"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T"},
    {"U", "V", "W", "X", "Y", "Z", "0", "1", "2", "3"},
    {"4", "5", "6", "7", "8", "9", "_", "DEL", "CLR", "OK"},
};

static const char* kGridENLower[4][10] = {
    {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"},
    {"k", "l", "m", "n", "o", "p", "q", "r", "s", "t"},
    {"u", "v", "w", "x", "y", "z", "0", "1", "2", "3"},
    {"4", "5", "6", "7", "8", "9", "_", "DEL", "CLR", "OK"},
};

// Candidate paths for settings.ini (PS3 game directory and desktop/local paths).
static const char* kSettingsPaths[] = {
    "/dev_hdd0/game/NOVU00001/USRDIR/settings.ini",
    "USRDIR/settings.ini",
    "settings.ini"
};

// Trim whitespace from both ends.
static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

} // namespace

void ProfilerStats::update(float dt, float renderMs, float updateMs, uint32_t heap) {
    const float alpha = 0.1f;
    const float beta = 1.0f - alpha;
    if (dt > 0.0f) {
        frameTimeMs = frameTimeMs * beta + (dt * 1000.0f) * alpha;
    }
    if (frameTimeMs > 0.001f) fps = 1000.0f / frameTimeMs;
    renderTimeMs = renderTimeMs * beta + renderMs * alpha;
    updateTimeMs = updateTimeMs * beta + updateMs * alpha;
    heapBytes = heap;
}

App::App() {
    loadSettings();

    mode = settings.mode;
    level = settings.level;
    helpersGuide = settings.guide;
    helpersHints = settings.hints;
    names[0] = settings.p1Name.empty() ? I18n::tr(settings.lang, "p1") : settings.p1Name;
    names[1] = (settings.mode == "ai")
                   ? I18n::tr(settings.lang, "computer")
                   : (settings.p2Name.empty() ? I18n::tr(settings.lang, "p2") : settings.p2Name);

    profiler.enabled = settings.perf;

    audio.setEnabled(settings.sound);
    try {
        audio.loadMenuMusic();
    } catch (...) {
        // non-fatal
    }
    audio.setMenuMusic(true);
    menuOpen = true;
    phase = Phase::Menu;
    // Build a decorative backdrop game.
    backdrop = Game(GameOpts{settings.sets, 0});
}

App::~App() {
    saveSettings();
}

void App::loadSettings() {
    FILE* f = nullptr;
    for (const char* p : kSettingsPaths) {
        f = std::fopen(p, "r");
        if (f) break;
    }
    if (!f) return;

    char buf[512];
    while (std::fgets(buf, sizeof(buf), f)) {
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key.empty()) continue;

        if (key == "lang") {
            if (val == "lv" || val == "en") settings.lang = val;
        } else if (key == "mode") {
            if (val == "ai" || val == "hotseat") settings.mode = val;
        } else if (key == "level") {
            if (val == "easy" || val == "medium" || val == "hard") settings.level = val;
        } else if (key == "sets") {
            int v = std::atoi(val.c_str());
            if (v >= 1 && v <= 3) settings.sets = v;
        } else if (key == "hints") {
            settings.hints = (val == "1" || val == "true");
        } else if (key == "guide") {
            settings.guide = (val == "1" || val == "true");
        } else if (key == "log") {
            settings.log = (val == "1" || val == "true");
        } else if (key == "sound") {
            settings.sound = (val == "1" || val == "true");
        } else if (key == "perf") {
            settings.perf = (val == "1" || val == "true");
        } else if (key == "p1Name") {
            settings.p1Name = val;
        } else if (key == "p2Name") {
            settings.p2Name = val;
        }
    }
    std::fclose(f);
}

void App::saveSettings() {
    FILE* f = nullptr;
    for (const char* p : kSettingsPaths) {
        f = std::fopen(p, "w");
        if (f) break;
    }
    if (!f) return;

    std::fprintf(f, "# Novuss settings\n");
    std::fprintf(f, "lang=%s\n", settings.lang.c_str());
    std::fprintf(f, "mode=%s\n", settings.mode.c_str());
    std::fprintf(f, "level=%s\n", settings.level.c_str());
    std::fprintf(f, "sets=%d\n", settings.sets);
    std::fprintf(f, "hints=%d\n", settings.hints ? 1 : 0);
    std::fprintf(f, "guide=%d\n", settings.guide ? 1 : 0);
    std::fprintf(f, "log=%d\n", settings.log ? 1 : 0);
    std::fprintf(f, "sound=%d\n", settings.sound ? 1 : 0);
    std::fprintf(f, "perf=%d\n", settings.perf ? 1 : 0);
    std::fprintf(f, "p1Name=%s\n", settings.p1Name.c_str());
    std::fprintf(f, "p2Name=%s\n", settings.p2Name.c_str());

    std::fflush(f);
    std::fclose(f);
}

int App::activePadForTurn(const PadState pads[2]) const {
    if (mode == "hotseat") {
        if (game.turn == 1 && pads[1].connected) return 1;
        return 0;
    }
    return 0;
}

std::string App::nameOf(int p) const {
    if (p == 0 && !settings.p1Name.empty()) return settings.p1Name;
    if (p == 1 && mode != "ai" && !settings.p2Name.empty()) return settings.p2Name;
    if (!names[p].empty()) return names[p];
    const char* key;
    if (p == 0) key = "p1";
    else key = (mode == "ai") ? "computer" : "p2";
    return I18n::tr(settings.lang, key);
}

void App::addToast(const std::string& text, const std::string& kind) {
    Toast t;
    t.text = text;
    t.kind = kind;
    t.t = 0;
    toasts.push_back(t);
}

void App::showBanner(const std::string& text) {
    bannerText = text;
    bannerTimer = 1.6;
}

void App::startMatch() {
    uint32_t r = xorshift(rngSeed);
    int first = (r & 1u) ? 1 : 0;

    game = Game(GameOpts{settings.sets, first});
    mode = settings.mode;
    level = settings.level;
    helpersGuide = settings.guide;
    helpersHints = settings.hints;

    names[0] = settings.p1Name.empty() ? I18n::tr(settings.lang, "p1") : settings.p1Name;
    names[1] = (mode == "ai")
                   ? I18n::tr(settings.lang, "computer")
                   : (settings.p2Name.empty() ? I18n::tr(settings.lang, "p2") : settings.p2Name);

    hasLastPlace[0] = hasLastPlace[1] = false;
    effects.clear();
    toasts.clear();
    sim.reset();

    menuOpen = false;
    audio.setMenuMusic(false);
    rulesOpen = false;
    resultOpen = false;

    if (mode == "ai") {
        viewTarget = (game.sides[0] == 'S') ? 0.0 : M_PI;
    } else {
        viewTarget = (game.sides[first] == 'S') ? 0.0 : M_PI;
    }
    viewRot = viewTarget;
    rotAnimActive = false;

    matchLog.clear();
    LogEntry coin;
    coin.k = "coin";
    coin.who = first;
    coin.set = 1;
    matchLog.push_back(coin);

    beginTurn(true);
}

void App::beginTurn(bool fresh) {
    int p = game.turn;

    if (mode == "ai") {
        viewTarget = sideRot(game.sides[0]);
    } else {
        viewTarget = sideRot(game.sides[game.turn]);
    }
    double diffTarget = viewTarget - viewRot;
    while (diffTarget > M_PI) diffTarget -= 2 * M_PI;
    while (diffTarget < -M_PI) diffTarget += 2 * M_PI;
    if (std::fabs(diffTarget) > 0.05 && !rotAnimActive) {
        rotAnimActive = true;
        rotAnimTime = 0.0;
        rotAnimFrom = viewRot;
        rotAnimTo = viewRot + diffTarget;
    }

    // Place striker.
    double sx = 0, sy = 0;
    bool placed = false;
    if (hasLastPlace[p]) {
        if (game.slidePlace(p, lastPlace[p][0], lastPlace[p][1], sx, sy)) {
            placed = true;
        }
    }
    if (!placed) {
        if (game.defaultPlace(p, MID, sx, sy)) {
            placed = true;
        }
    }
    if (!placed) {
        game.slidePlace(p, MID, 945, sx, sy);
    }
    strikerX = sx;
    strikerY = sy;

    hasStriker = true;
    charging = false;
    chargeCancelled = false;
    aimPower = 0;
    aimShown = 0;
    aimAngle = 0;

    flipDir(game.sides[p], std::sin(aimAngle), -std::cos(aimAngle), aimDir[0], aimDir[1]);
    aimValid = game.canAim(p, strikerX, strikerY, aimDir[0], aimDir[1]);

    if (!fresh && mode != "ai") {
        showBanner(I18n::tr(settings.lang, "turnOf", {{"name", nameOf(p)}}));
    }

    if (mode == "ai" && p == 1) {
        phase = Phase::AI;
        aiTimer = 0;
        aiHasShot = false;
        rngSeed += 0x9E3779B9u;
        aiPlanner.init(game.clone(), level, rngSeed);
    } else {
        phase = Phase::Place;
    }
}

void App::fire(double x, double y, double vx, double vy) {
    int p = game.turn;
    double lx = 0, ly = 0;
    flip(game.sides[p], x, y, lx, ly);
    lastPlace[p][0] = lx;
    lastPlace[p][1] = ly;
    hasLastPlace[p] = true;

    double speed = hyp(vx, vy);
    audio.trigger("cue", speed / C::MAX_SPEED);
    rumbleQueue.push_back(RumbleEvent{0, (float)std::min(0.35, speed / C::MAX_SPEED * 0.35), 0.05f});

    sim.reset(new Sim(game.shoot(x, y, vx, vy)));
    simEvents.clear();
    sim->events = &simEvents;

    hasStriker = false;
    charging = false;
    phase = Phase::Moving;
}

void App::finishShot() {
    if (!sim) {
        beginTurn(true);
        return;
    }
    int prev = game.turn;
    ResolveOut out = game.resolve(*sim);

    std::vector<LogEntry> entries = logEntries(out, game.setNo);
    for (const auto& e : entries) matchLog.push_back(e);

    for (const auto& m : out.marks) {
        Effect fx;
        fx.kind = "pulse";
        fx.id = m.id;
        fx.t = 0;
        fx.dur = 0.5;
        effects.push_back(fx);
    }

    sim.reset();

    if (out.setResult.has) {
        phase = Phase::Result;
        showResult(out.setResult, out.shooter);
    } else {
        beginTurn(game.turn == prev);
    }
}

void App::showResult(const SetResult& res, int shooter) {
    const int setNo = game.setNo;

    std::string title, text;
    if (res.replay) {
        title = I18n::tr(settings.lang, "replayTitle");
        text = I18n::tr(settings.lang, res.k);
    } else {
        int w = res.winner;
        bool isLost = (res.k.rfind("lost_", 0) == 0);
        text = I18n::tr(settings.lang, res.k, {{"name", nameOf(isLost ? (1 - shooter) : w)}});
        if (isLost) text = nameOf(shooter) + ": " + text;
        title = I18n::tr(settings.lang, "setWon", {{"name", nameOf(w)}, {"n", i2s(setNo)}});
    }

    game.nextSet(res);

    if (game.winner != -1) {
        LogEntry me;
        me.k = "matchWon";
        me.who = game.winner;
        me.set = setNo;
        matchLog.push_back(me);
        title = I18n::tr(settings.lang, "matchWon", {{"name", nameOf(game.winner)}});
    }

    hasLastPlace[0] = hasLastPlace[1] = false;

    resultTitle = title;
    resultScore = i2s(game.sets[0]) + " : " + i2s(game.sets[1]);
    resultText = text;
    resultBtnLabel = I18n::tr(settings.lang, (game.winner != -1) ? "newMatch" : (res.replay ? "cont" : "nextSet")) + " [X]";
    resultOpen = true;
}

void App::update(double dt, const PadState pads[2]) {
    // Smooth view rotation with cubic Hermite ease-in-out.
    if (rotAnimActive) {
        rotAnimTime += dt;
        double t = std::min(1.0, rotAnimTime / rotAnimDur);
        double s = t * t * (3.0 - 2.0 * t);
        viewRot = rotAnimFrom + (rotAnimTo - rotAnimFrom) * s;
        if (t >= 1.0) {
            viewRot = viewTarget;
            rotAnimActive = false;
        }
    } else {
        viewRot = viewTarget;
    }

    // Advance effects.
    for (auto& e : effects) e.t += dt;
    effects.erase(std::remove_if(effects.begin(), effects.end(),
                                 [](const Effect& e) { return e.t >= e.dur; }),
                  effects.end());

    // Advance toasts.
    for (auto& t : toasts) t.t += dt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [](const Toast& t) { return t.t >= 2.5; }),
                 toasts.end());

    if (bannerTimer > 0) bannerTimer = std::max(0.0, bannerTimer - dt);

    // Edge detection helpers.
    auto pressed = [&](int i, bool PadState::*field) -> bool {
        return pads[i].connected && pads[i].*field && !(prevPads[i].*field);
    };
    auto anyPressed = [&](bool PadState::*field) -> bool {
        return pressed(0, field) || pressed(1, field);
    };

    // Toggle on-screen profiler overlay anytime with R3.
    if (anyPressed(&PadState::r3)) {
        profiler.enabled = !profiler.enabled;
        settings.perf = profiler.enabled;
        saveSettings();
    }

    // ---- Name editor overlay ----
    if (nameEditOpen) {
        if (anyPressed(&PadState::up)) {
            nameEditRow = (nameEditRow + 3) % 4;
        }
        if (anyPressed(&PadState::down)) {
            nameEditRow = (nameEditRow + 1) % 4;
        }
        if (anyPressed(&PadState::left)) {
            nameEditCol = (nameEditCol + 9) % 10;
        }
        if (anyPressed(&PadState::right)) {
            nameEditCol = (nameEditCol + 1) % 10;
        }

        bool lower = !nameEditBuf.empty() && nameEditBuf.back() != ' ';
        bool en = (settings.lang == "en");
        const char* cell;
        if (en) {
            cell = lower ? kGridENLower[nameEditRow][nameEditCol]
                         : kGridEN[nameEditRow][nameEditCol];
        } else {
            cell = lower ? kGridLVLower[nameEditRow][nameEditCol]
                         : kGridLV[nameEditRow][nameEditCol];
        }
        std::string cellStr(cell);

        if (anyPressed(&PadState::cross)) {
            if (cellStr == "OK") {
                if (nameEditPlayer == 0) settings.p1Name = nameEditBuf;
                else settings.p2Name = nameEditBuf;
                names[nameEditPlayer] = nameEditBuf;
                nameEditOpen = false;
                saveSettings();
            } else if (cellStr == "DEL") {
                popUtf8(nameEditBuf);
            } else if (cellStr == "CLR") {
                nameEditBuf.clear();
            } else if (cellStr == "_") {
                nameEditBuf += " ";
            } else {
                nameEditBuf += cellStr;
            }
        }

        if (anyPressed(&PadState::square)) {
            popUtf8(nameEditBuf);
        }

        if (anyPressed(&PadState::start)) {
            if (nameEditPlayer == 0) settings.p1Name = nameEditBuf;
            else settings.p2Name = nameEditBuf;
            names[nameEditPlayer] = nameEditBuf;
            nameEditOpen = false;
            saveSettings();
        }

        if (anyPressed(&PadState::circle)) {
            nameEditOpen = false;
        }

        prevPads[0] = pads[0];
        prevPads[1] = pads[1];
        return;
    }

    // ---- Rules overlay ----
    if (rulesOpen) {
        if (anyPressed(&PadState::up)) rulesScroll = std::max(0.0, rulesScroll - 1.5);
        if (anyPressed(&PadState::down)) rulesScroll = std::min(28.0, rulesScroll + 1.5);
        double ly = 0;
        for (int i = 0; i < 2; ++i) if (pads[i].connected) ly += pads[i].ly;
        if (std::fabs(ly) > 0.2) {
            rulesScroll = clampd(rulesScroll + ly * dt * 8.0, 0.0, 28.0);
        }
        if (anyPressed(&PadState::circle) || anyPressed(&PadState::cross) ||
            anyPressed(&PadState::square) || anyPressed(&PadState::start)) {
            rulesOpen = false;
        }
        prevPads[0] = pads[0];
        prevPads[1] = pads[1];
        return;
    }

    // ---- Result overlay ----
    if (resultOpen) {
        if (anyPressed(&PadState::cross) || anyPressed(&PadState::start)) {
            if (game.winner != -1) {
                resultOpen = false;
                menuOpen = true;
                audio.setMenuMusic(true);
                menuRow = 9; // Highlight 'New match' ('Jauna spēle') row in menu
                phase = Phase::Menu;
            } else {
                resultOpen = false;
                effects.clear();
                toasts.clear();
                beginTurn(true);
                if (mode != "ai" || game.turn == 0) {
                    showBanner(I18n::tr(settings.lang, "turnOf", {{"name", nameOf(game.turn)}}));
                }
            }
        }
        prevPads[0] = pads[0];
        prevPads[1] = pads[1];
        return;
    }

    // ---- Menu navigation ----
    if (menuOpen) {
        if (anyPressed(&PadState::up)) {
            menuRow = (menuRow + 11) % 12;
        }
        if (anyPressed(&PadState::down)) {
            menuRow = (menuRow + 1) % 12;
        }
        bool left = anyPressed(&PadState::left);
        bool right = anyPressed(&PadState::right);
        bool cross = anyPressed(&PadState::cross);
        bool circle = anyPressed(&PadState::circle);

        auto cycle = [&](int dir) {
            switch (menuRow) {
            case 0:
                settings.mode = (settings.mode == "ai") ? "hotseat" : "ai";
                mode = settings.mode;
                break;
            case 1: {
                static const char* lv[] = {"easy", "medium", "hard"};
                int idx = 1;
                for (int i = 0; i < 3; ++i) if (settings.level == lv[i]) idx = i;
                idx = (idx + dir + 3) % 3;
                settings.level = lv[idx];
                break;
            }
            case 2: {
                static const int sets[] = {1, 2, 3};
                int idx = 1;
                for (int i = 0; i < 3; ++i) if (settings.sets == sets[i]) idx = i;
                idx = (idx + dir + 3) % 3;
                settings.sets = sets[idx];
                break;
            }
            case 3: {
                static const char* p1[] = {"", "Kaspars", "J\xC4\x81nis", "M\xC4\x81rti\xC5\x86\xC5\xA1",
                                           "Roberts", "Andris", "L\xC4\xABga", "Anna"};
                int idx = 0;
                for (int i = 0; i < 8; ++i) if (settings.p1Name == p1[i]) idx = i;
                idx = (idx + dir + 8) % 8;
                settings.p1Name = p1[idx];
                names[0] = settings.p1Name.empty() ? I18n::tr(settings.lang, "p1") : settings.p1Name;
                break;
            }
            case 4: {
                settings.mode = "hotseat";
                mode = "hotseat";
                static const char* p2[] = {"", "J\xC4\x81nis", "M\xC4\x81rti\xC5\x86\xC5\xA1", "Kaspars",
                                           "Roberts", "Andris", "L\xC4\xABga", "Anna"};
                int idx = 0;
                for (int i = 0; i < 8; ++i) if (settings.p2Name == p2[i]) idx = i;
                idx = (idx + dir + 8) % 8;
                settings.p2Name = p2[idx];
                names[1] = settings.p2Name.empty() ? I18n::tr(settings.lang, "p2") : settings.p2Name;
                break;
            }
            case 5:
                settings.guide = !settings.guide;
                helpersGuide = settings.guide;
                break;
            case 6:
                settings.hints = !settings.hints;
                helpersHints = settings.hints;
                break;
            case 7:
                settings.lang = (settings.lang == "lv") ? "en" : "lv";
                break;
            case 8:
                settings.sound = !settings.sound;
                audio.setEnabled(settings.sound);
                break;
            default:
                break;
            }
            saveSettings();
        };

        if (left) cycle(-1);
        if (right) cycle(1);

        if (cross) {
            if (menuRow == 3) {
                nameEditOpen = true;
                nameEditPlayer = 0;
                nameEditBuf = nameOf(0);
                nameEditRow = 0;
                nameEditCol = 0;
            } else if (menuRow == 4) {
                settings.mode = "hotseat";
                mode = "hotseat";
                nameEditOpen = true;
                nameEditPlayer = 1;
                nameEditBuf = nameOf(1);
                nameEditRow = 0;
                nameEditCol = 0;
            } else if (menuRow == 9) {
                startMatch();
            } else if (menuRow == 10) {
                if (phase == Phase::Menu) startMatch();
                else {
                    menuOpen = false;
                    audio.setMenuMusic(false);
                }
            } else if (menuRow == 11) {
                rulesOpen = true;
                rulesScroll = 0;
            } else {
                cycle(1);
            }
        }

        if (circle) {
            if (phase != Phase::Menu) {
                menuOpen = false;
                audio.setMenuMusic(false);
            }
        }

        if (anyPressed(&PadState::start)) {
            if (phase != Phase::Menu) {
                menuOpen = !menuOpen;
                audio.setMenuMusic(menuOpen);
            }
        }
        if (anyPressed(&PadState::select) || anyPressed(&PadState::triangle)) {
            settings.log = !settings.log;
            saveSettings();
        }

        prevPads[0] = pads[0];
        prevPads[1] = pads[1];
        return;
    } else {
        if (anyPressed(&PadState::start)) {
            menuOpen = true;
            audio.setMenuMusic(true);
        }
    }

    // ---- Placement / aiming ----
    if (phase == Phase::Place && !menuOpen && !rulesOpen && !resultOpen) {
        int p = game.turn;
        int padIdx = activePadForTurn(pads);
        const PadState& pad = pads[padIdx];

        bool fine = pad.l1 || pad.r1;

        // Movement.
        double mx = 0, my = 0;
        if (pad.up) my -= 1;
        if (pad.down) my += 1;
        if (pad.left) mx -= 1;
        if (pad.right) mx += 1;
        if (std::fabs(pad.rx) > 0.18) mx += pad.rx;
        if (std::fabs(pad.ry) > 0.18) my += pad.ry;

        if (mx != 0 || my != 0) {
            double len = std::sqrt(mx * mx + my * my);
            if (len > 1.0) { mx /= len; my /= len; }
            double speed = fine ? 28.0 : 240.0;
            // Rotate screen-space input into table space.
            double wx, wy;
            rotateBy(-viewRot, mx, my, wx, wy);
            double nx = strikerX + wx * speed * dt;
            double ny = strikerY + wy * speed * dt;
            double ox, oy;
            bool moved = false;
            if (game.slidePlace(p, nx, ny, ox, oy)) {
                if (hyp(ox - strikerX, oy - strikerY) > 0.05) {
                    strikerX = ox;
                    strikerY = oy;
                    moved = true;
                }
            }
            if (!moved) {
                // Obstacle hopping: if direct slide is blocked by pucks on the baseline,
                // scan ahead along the input vector to hop over obstacles to the next legal spot.
                double dlen = hyp(wx, wy);
                if (dlen > 0.1) {
                    double dirX = wx / dlen;
                    double dirY = wy / dlen;
                    bool hopped = false;
                    for (double step = 6.0; step <= 750.0; step += 4.0) {
                        double tx = strikerX + dirX * step;
                        double ty = strikerY + dirY * step;
                        if (game.canPlace(p, tx, ty)) {
                            strikerX = tx;
                            strikerY = ty;
                            hopped = true;
                            break;
                        }
                    }
                    if (!hopped && std::fabs(dirX) > 0.1) {
                        double stepDir = (dirX > 0) ? 1.0 : -1.0;
                        for (double step = 6.0; step <= 750.0; step += 4.0) {
                            double tx = strikerX + stepDir * step;
                            double ty = strikerY;
                            if (game.canPlace(p, tx, ty)) {
                                strikerX = tx;
                                strikerY = ty;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Aim rotation.
        if (std::fabs(pad.lx) > 0.08) {
            double s = pad.lx * (0.30 + 0.70 * pad.lx * pad.lx);
            double rate = fine ? 0.065 : 1.35;
            aimAngle = clampd(aimAngle + s * rate * dt, -1.48, 1.48);
        }
        flipDir(game.sides[p], std::sin(aimAngle), -std::cos(aimAngle), aimDir[0], aimDir[1]);
        aimValid = game.canAim(p, strikerX, strikerY, aimDir[0], aimDir[1]);

        // Rules shortcut.
        if (anyPressed(&PadState::square)) {
            rulesOpen = true;
            rulesScroll = 0;
        }

        // Charging.
        bool chargeHeld = pad.cross || pad.r2;
        if (chargeHeld) {
            if (!charging) {
                charging = true;
                chargeCancelled = false;
                aimPower = 0;
                chargeDir = 1.0;
            }
            aimPower += chargeDir * 0.85 * dt;
            if (aimPower >= 1.0) { aimPower = 1.0; chargeDir = -1.0; }
            if (aimPower <= 0.0) { aimPower = 0.0; chargeDir = 1.0; }
        } else if (charging) {
            // Released.
            if (!chargeCancelled && aimPower >= 0.02) {
                if (aimValid) {
                    double speed = powerToSpeed(aimPower);
                    double vx = aimDir[0] * speed;
                    double vy = aimDir[1] * speed;
                    fire(strikerX, strikerY, vx, vy);
                } else {
                    addToast(I18n::tr(settings.lang, "invalidAim"), "warn");
                }
            }
            charging = false;
            aimPower = 0;
        }

        if (pad.circle && !prevPads[padIdx].circle && charging) {
            chargeCancelled = true;
            charging = false;
            aimPower = 0;
        }

        aimShown = aimPower;
    }

    // ---- Moving ----
    if (phase == Phase::Moving && sim) {
        sim->advance(dt);
        for (const auto& ev : simEvents) {
            if (ev.type == "pocket") {
                Effect fx;
                fx.kind = "fall";
                fx.id = ev.id;
                fx.x = ev.x;
                fx.y = ev.y;
                fx.t = 0;

                int best = 0;
                double bestD = 1e18;
                for (int i = 0; i < 4; ++i) {
                    double dx = POCKETS[i][0] - ev.x;
                    double dy = POCKETS[i][1] - ev.y;
                    double d = dx * dx + dy * dy;
                    if (d < bestD) { bestD = d; best = i; }
                }
                fx.px = POCKETS[best][0];
                fx.py = POCKETS[best][1];
                fx.r = ev.striker ? C::STRIKER_R : C::PUCK_R;
                fx.hole = ev.striker ? C::STRIKER_HOLE_R : C::PUCK_HOLE_R;
                fx.kindIdx = ev.striker ? 2
                    : (ev.id >= 0 && ev.id < (int)game.pucks.size()
                           ? game.pucks[ev.id].owner
                           : 0);
                fx.dur = 0.25;
                effects.push_back(fx);
                audio.trigger("pocket", ev.power);
                rumbleQueue.push_back(RumbleEvent{0, (float)std::min(0.40, ev.power * 0.40), 0.04f});
            } else if (ev.type == "off") {
                audio.trigger("off", ev.power);
            } else if (ev.type == "cushion") {
                audio.trigger("cushion", ev.power);
            } else if (ev.type == "hit") {
                audio.trigger("hit", ev.power);
            }
        }
        simEvents.clear();

        if (sim->atRest()) {
            phase = Phase::Settle;
            settleTimer = 0.35;
        }
    }

    // ---- Settle ----
    if (phase == Phase::Settle) {
        settleTimer -= dt;
        if (settleTimer <= 0) {
            finishShot();
        }
    }

    // ---- AI ----
    if (phase == Phase::AI) {
        aiTimer += dt;
        if (!aiHasShot) {
            if (aiPlanner.step(8) || aiPlanner.done()) {
                aiShot = aiPlanner.result();
                aiHasShot = true;
                aiTimer = 0;
                // Prepare animation.
                aiAnim.fromX = strikerX;
                aiAnim.fromY = strikerY;
                aiAnim.toX = aiShot.x;
                aiAnim.toY = aiShot.y;
                double sp = hyp(aiShot.vx, aiShot.vy);
                if (sp > 1e-6) {
                    aiAnim.dirX = aiShot.vx / sp;
                    aiAnim.dirY = aiShot.vy / sp;
                } else {
                    aiAnim.dirX = 0;
                    aiAnim.dirY = -1;
                }
                aiAnim.power = speedToPower(sp);
                aiAnim.t = 0;
                aiAnim.shot = aiShot;
                phase = Phase::AIAnim;
            }
        }
    }

    if (phase == Phase::AIAnim) {
        aiAnim.t += dt;
        const double tSlide = 0.35;
        const double tAim = 0.35;
        const double tPull = 0.35;
        const double tTotal = tSlide + tAim + tPull;

        double localDirX = 0, localDirY = -1;
        flipDir(game.sides[game.turn], aiAnim.dirX, aiAnim.dirY, localDirX, localDirY);
        double targetAngle = std::atan2(localDirX, -localDirY);
        targetAngle = clampd(targetAngle, -1.48, 1.48);

        if (aiAnim.t <= tSlide) {
            double k = aiAnim.t / tSlide;
            double s = k * k * (3.0 - 2.0 * k); // smoothstep
            strikerX = aiAnim.fromX + (aiAnim.toX - aiAnim.fromX) * s;
            strikerY = aiAnim.fromY + (aiAnim.toY - aiAnim.fromY) * s;
            aimAngle = 0.0;
            aimPower = 0.0;
            aimShown = 0.0;
        } else if (aiAnim.t <= tSlide + tAim) {
            strikerX = aiAnim.toX;
            strikerY = aiAnim.toY;
            double k = (aiAnim.t - tSlide) / tAim;
            double s = k * k * (3.0 - 2.0 * k); // smoothstep
            aimAngle = targetAngle * s;
            aimPower = 0.0;
            aimShown = 0.0;
        } else if (aiAnim.t <= tTotal) {
            strikerX = aiAnim.toX;
            strikerY = aiAnim.toY;
            aimAngle = targetAngle;
            double k = (aiAnim.t - (tSlide + tAim)) / tPull;
            double s = k * k * (3.0 - 2.0 * k);
            aimPower = aiAnim.power * s;
            aimShown = aimPower;
        } else {
            strikerX = aiAnim.toX;
            strikerY = aiAnim.toY;
            aimAngle = targetAngle;
            aimPower = aiAnim.power;
            aimShown = 0.0;
            fire(aiAnim.toX, aiAnim.toY, aiAnim.shot.vx, aiAnim.shot.vy);
        }

        // Always update aimDir so cue stick draws in the current direction
        flipDir(game.sides[game.turn], std::sin(aimAngle), -std::cos(aimAngle), aimDir[0], aimDir[1]);
        aimValid = true;
    }

    prevPads[0] = pads[0];
    prevPads[1] = pads[1];
}

} // namespace Novuss
