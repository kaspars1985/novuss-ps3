#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio.hpp"
#include "game.hpp"

namespace Novuss {

// ---------------------------------------------------------------------------
// Controller state (one per pad).
// ---------------------------------------------------------------------------
struct PadState {
    bool connected = false;
    float lx = 0, ly = 0, rx = 0, ry = 0; // [-1, 1]
    bool up = false, down = false, left = false, right = false;
    bool cross = false, circle = false, square = false, triangle = false;
    bool l1 = false, r1 = false, l2 = false, r2 = false;
    bool start = false, select = false;
};

// ---------------------------------------------------------------------------
// Rumble request queued by the app and drained by the platform layer.
// ---------------------------------------------------------------------------
struct RumbleEvent {
    int pad = 0;
    float strength = 0;
    float duration = 0;
};

// ---------------------------------------------------------------------------
// Persistent user settings.
// ---------------------------------------------------------------------------
struct Settings {
    std::string lang = "en";
    std::string mode = "ai"; // "ai" or "hotseat"
    std::string level = "medium"; // "easy", "medium", "hard"
    int sets = 2; // 1, 2, 3
    bool hints = true;
    bool guide = true;
    bool log = true;
    bool sound = true;
    std::string p1Name = "";
    std::string p2Name = "";
};

// ---------------------------------------------------------------------------
// High-level game phase.
// ---------------------------------------------------------------------------
enum class Phase { Menu, Place, Moving, Settle, AI, AIAnim, Result, Rules };

// ---------------------------------------------------------------------------
// Visual effect (puck fall / pocket pulse).
// ---------------------------------------------------------------------------
struct Effect {
    std::string kind; // "fall" or "pulse"
    int id = -1;
    double x = 0, y = 0, px = 0, py = 0;
    double t = 0, dur = 0;
    double r = 0, hole = 0;
    int kindIdx = 0;
};

// ---------------------------------------------------------------------------
// Transient toast message.
// ---------------------------------------------------------------------------
struct Toast {
    std::string text;
    std::string kind;
    double t = 0;
};

// ---------------------------------------------------------------------------
// App: owns game state, controller handling, UI overlays and rendering.
// ---------------------------------------------------------------------------
class App {
public:
    Settings settings;
    Game game;
    Game backdrop;
    Phase phase = Phase::Menu;
    Phase prevPhaseBeforeOverlay = Phase::Menu;
    bool menuOpen = true;
    bool rulesOpen = false;
    bool resultOpen = false;
    int menuRow = 0; // 0: Opponent (AI/2P), 1: Level (Easy/Med/Hard), 2: Match (1/3/5), 3: P1 Name, 4: P2 Name, 5: Guide, 6: Hints, 7: Language (LV/EN), 8: Sound, 9: Start Match, 10: Resume, 11: Rules
    bool nameEditOpen = false;
    int nameEditPlayer = 0;
    std::string nameEditBuf;
    int nameEditRow = 0;
    int nameEditCol = 0;
    double rulesScroll = 0;
    std::string mode = "ai";
    std::string level = "medium";
    std::string names[2] = {"", ""};
    bool helpersGuide = true;
    bool helpersHints = true;
    bool hasStriker = false;
    double strikerX = 500, strikerY = 945;
    double aimAngle = 0; // relative to shooter forward direction, in radians (-PI/2 .. +PI/2)
    double aimDir[2] = {0, -1};
    double aimPower = 0;
    double aimShown = 0;
    bool aimValid = true;
    bool charging = false;
    double chargeDir = 1.0;
    bool chargeCancelled = false;
    bool hasLastPlace[2] = {false, false};
    double lastPlace[2][2] = {{0, 0}, {0, 0}};
    std::unique_ptr<Sim> sim;
    std::vector<SimEvent> simEvents;
    AIPlanner aiPlanner;
    double aiTimer = 0;
    bool aiHasShot = false;
    ShotResult aiShot;
    struct AIAnim {
        double fromX, fromY, toX, toY, dirX, dirY, power, t;
        ShotResult shot;
    } aiAnim;
    double settleTimer = 0;
    double viewRot = 0, viewTarget = 0;
    bool rotAnimActive = false;
    double rotAnimTime = 0;
    double rotAnimDur = 0.65;
    double rotAnimFrom = 0;
    double rotAnimTo = 0;
    std::vector<Effect> effects;
    std::vector<Toast> toasts;
    std::string bannerText;
    double bannerTimer = 0;
    std::vector<LogEntry> matchLog;
    std::string resultTitle, resultScore, resultText, resultBtnLabel;
    Audio::SoundEngine audio;
    std::vector<RumbleEvent> rumbleQueue;
    uint32_t rngSeed = 0x12345678u;
    PadState prevPads[2];

    App();
    ~App();

    void loadSettings();
    void saveSettings();

    void startMatch();
    void beginTurn(bool fresh);
    void fire(double x, double y, double vx, double vy);
    void finishShot();
    void showResult(const SetResult& res, int shooter);
    void update(double dt, const PadState pads[2]);
    void render(uint32_t* fb, int width, int height, int stridePixels);

    int activePadForTurn(const PadState pads[2]) const;
    std::string nameOf(int p) const;
    void addToast(const std::string& text, const std::string& kind = "");
    void showBanner(const std::string& text);
};

} // namespace Novuss
