#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Novuss {

// ---------------------------------------------------------------------------
// Constants (LNF "Sacensību inventārs"). All double precision, identical to
// reference/game.js.
// ---------------------------------------------------------------------------
struct C {
    static constexpr double FIELD = 1000.0;
    static constexpr double RAIL = 45.0;
    static constexpr double ZONE = 130.0;
    static constexpr double LINE_W = 1.0;
    static constexpr double CIRCLE_R = 125.0;
    static constexpr double POCKET_R = 50.0;
    static constexpr double POCKET_C = 65.0;
    static constexpr double STRIKER_R = 22.25;
    static constexpr double STRIKER_HOLE_R = 5.0;
    static constexpr double STRIKER_M = 22.0;
    static constexpr double PUCK_R = 15.0;
    static constexpr double PUCK_HOLE_R = 4.0;
    static constexpr double PUCK_M = 5.0;

    static constexpr double E_PUCK = 0.9;
    static constexpr double E_CUSHION = 0.75;
    static constexpr double DECEL = 950.0;
    static constexpr double DAMP = 0.18;
    static constexpr double JUMP_SPEED = 6500.0;
    static constexpr double MIN_SPEED = 150.0;
    static constexpr double MAX_SPEED = 4500.0;
    static constexpr double DT = 0.0005;
    static constexpr double CUE_MARGIN = 10.0;
};

extern const double POCKETS[4][2];
extern const double MID;
extern const double HALF_LINE;
extern const double ZONE_LINE;

struct Place {
    double xMin;
    double xMax;
    double yMin;
    double yMax;
};
extern const Place PLACE;

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
double hyp(double x, double y);

// Rotate between absolute and side-relative coordinates.
void flip(char side, double x, double y, double& ox, double& oy);
void flipDir(char side, double dx, double dy, double& ox, double& oy);

// 'strip', 'circle' or 0 (none).
char area(char side, double x, double y, double holeR);

double segDist(double ax, double ay, double bx, double by, double px, double py);

double powerToSpeed(double p);
double speedToPower(double v);

// Cast a ray from (ox,oy) in direction (dx,dy) against pucks. Returns hit
// distance t (>=0) and the hit puck index (or -1 if none). If no hit, t is
// +infinity.
struct RayHit {
    double t;
    int index;
};
RayHit castRay(const std::vector<struct Puck>& pucks, double ox, double oy,
               double dx, double dy, double radius);

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------
struct Body {
    int id = 0;
    int owner = 0;
    double x = 0, y = 0;
    double vx = 0, vy = 0;
    double r = 0;
    double inv = 0;
    bool on = true;
    bool striker = false;
    int pocket = -1;
};

struct SimEvent {
    std::string type;
    double x = 0, y = 0;
    double power = 0;
    bool striker = false;
    int id = 0;
    double t = 0;
};

struct SimLog {
    std::vector<char> cushionsBefore;
    bool hasFirstContact = false;
    std::vector<int> firstContact;
    std::vector<int> pocketed;
    struct OffEntry { int id; char wall; };
    std::vector<OffEntry> off;
    bool strikerPocketed = false;
    bool strikerOff = false;
    bool strikerLeft = false;
    bool strikerReturned = false;
};

struct Sim {
    char side = 'S';
    double t = 0;
    double acc = 0;
    Body striker;
    std::vector<Body> bodies;
    SimLog log;
    std::vector<SimEvent>* events = nullptr;
    uint32_t rng = 0x12345678u;
    double strikerLaunchSpeed = 0;
    int strikerBalkonsCorner = -1;
    int strikerBalkonsDest = 0; // 0=undecided, 1=return safely, 2=pocket, 3=off

    Sim(const std::vector<Puck>& pucks, double sx, double sy, double svx, double svy,
        char shooterSide, uint32_t seed = 0);

    void emit(const std::string& type, const Body& b, double power);
    void cushion(Body& b, char wall, double vn, char axis, double lim);
    void step(double dt);
    bool atRest() const;
    void advance(double dt);
    void run(double maxT = 60.0);
};

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------
struct Puck {
    int id = 0;
    int owner = 0;
    double x = 0, y = 0;
    bool on = true;
};

struct Mark {
    int id = 0;
    std::string kind;
};

struct SetResult {
    bool has = false;
    bool replay = false;
    bool swap = false;
    int winner = -1;
    std::string k;
};

struct ShotMsg {
    std::string k;
    bool hasN = false;
    int n = 0;
};

struct ResolveOut {
    int shooter = 0;
    std::vector<ShotMsg> msgs;
    std::vector<Mark> marks;
    bool keep = false;
    bool penalty = false;
    bool correct = false;
    SetResult setResult;
    int ownPot = 0;
    int oppPot = 0;
};

struct LogEntry {
    std::string k;
    int who = 0;
    int set = 0;
    bool hasN = false;
    int n = 0;
    bool hasRes = false;
    SetResult res;
    bool hasShooter = false;
    int shooter = 0;
};

struct GameOpts {
    int setsToWin = 2;
    int first = 0;
};

class Game {
public:
    int setsToWin = 2;
    int matchFirst = 0;
    int sets[2] = {0, 0};
    int setNo = 1;
    char sides[2] = {'S', 'N'};
    int winner = -1; // -1 = none
    int first = 0;
    int turn = 0;
    int series[2] = {0, 0};
    int debt[2] = {0, 0};
    int firstClear = -1; // -1 = null
    int miss[2] = {0, 0};
    std::vector<Puck> pucks;

    // Snapshot taken at shoot() time.
    struct PrePuck {
        double x = 0, y = 0;
        int owner = 0;
        bool dark = false;
        char myArea = 0;
        char oppArea = 0;
    };
    struct Pre {
        bool valid = false;
        std::map<int, PrePuck> pucks;
        bool light[2] = {false, false};
        bool dark = false;
        int on[2] = {0, 0};
        double shotX = 0;
    };
    Pre pre;

    Game();
    explicit Game(const GameOpts& opts);

    void newSet(int firstPlayer);
    Game clone() const;

    int onTable(int p) const;
    bool isDark(const Puck& k) const;
    bool inOwnStrip(int p, const Puck& k) const;
    bool hasLight(int p) const;
    bool hasDark(int p) const;

    bool canPlace(int p, double x, double y) const;
    bool slidePlace(int p, double x, double y, double& ox, double& oy) const;
    bool defaultPlace(int p, double prefX, double& ox, double& oy) const;
    bool defaultPlace(int p, double& ox, double& oy) const { return defaultPlace(p, MID, ox, oy); }

    bool canAim(int p, double x, double y, double dx, double dy) const;

    bool isFree(double x, double y, int ignore) const;
    void freeSpot(double x, double y, int ignore, double& ox, double& oy) const;

    void offSpot(char wall, double& ox, double& oy) const;
    void penaltySpot(int p, bool hasShotX, double shotX, double& ox, double& oy) const;

    int placePenalties(int p, int n, bool hasShotX, double shotX, std::vector<Mark>* marks);
    int placeDebt(int p, bool hasShotX, double shotX, std::vector<Mark>* marks);

    void passTurn();

    Sim shoot(double x, double y, double vx, double vy);

    ResolveOut resolve(Sim& sim);

    void nextSet(const SetResult& res);
};

std::vector<LogEntry> logEntries(const ResolveOut& out, int setNo);

// ---------------------------------------------------------------------------
// AI
// ---------------------------------------------------------------------------
struct ShotResult {
    double x = 0, y = 0, vx = 0, vy = 0;
};

ShotResult planShotSync(const Game& game, const std::string& level, uint32_t seed);

class AIPlanner {
public:
    AIPlanner();
    ~AIPlanner();

    void init(const Game& game, const std::string& level, uint32_t seed);
    // Run up to `budgetEvals` evaluations. Returns true when finished.
    bool step(int budgetEvals = 6);
    bool done() const;
    ShotResult result() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace Novuss
