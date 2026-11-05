#include "game.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace Novuss {

namespace {

// ---------------------------------------------------------------------------
// Deterministic PRNG (xorshift32) producing uniform doubles in (0, 1).
// ---------------------------------------------------------------------------
class Rng {
public:
    explicit Rng(uint32_t seed) {
        state_ = seed ? seed : 0x9E3779B9u;
        // Warm up a few times so nearby seeds diverge.
        for (int i = 0; i < 8; i++) next();
    }

    uint32_t next() {
        uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state_ = x;
        return x;
    }

    // Uniform in (0, 1) — never exactly 0 or 1.
    double uniform() {
        // 24 bits of mantissa precision.
        uint32_t v = next() >> 8; // 0..2^24-1
        return (static_cast<double>(v) + 0.5) / 16777216.0;
    }

private:
    uint32_t state_;
};

// ---------------------------------------------------------------------------
// Level parameters (reference/game.js LEVELS).
// ---------------------------------------------------------------------------
struct Level {
    double ang;   // degrees
    double pow;   // fraction
    int sims;
    int robust;
};

Level levelFor(const std::string& name) {
    if (name == "easy") return Level{1.6, 0.08, 50, 0};
    if (name == "hard") return Level{0.2, 0.015, 240, 4};
    return Level{1.1, 0.06, 130, 3}; // medium (default)
}

// ---------------------------------------------------------------------------
// Gaussian sample via Box–Muller, matching the JS implementation.
// ---------------------------------------------------------------------------
double gauss(Rng& rng) {
    double u = 0;
    while (u == 0) u = rng.uniform();
    return std::sqrt(-2.0 * std::log(u)) * std::cos(2.0 * M_PI * rng.uniform());
}

// ---------------------------------------------------------------------------
// Distance from (x, y) to the nearest pocket.
// ---------------------------------------------------------------------------
double nearestPocket(double x, double y) {
    double m = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 4; i++) {
        double d = hyp(x - POCKETS[i][0], y - POCKETS[i][1]);
        if (d < m) m = d;
    }
    return m;
}

// ---------------------------------------------------------------------------
// Score a resolved outcome from the shooter's perspective.
// ---------------------------------------------------------------------------
double scoreOutcome(const Game& g, const ResolveOut& o, int p) {
    double s = 0;
    if (o.setResult.has) {
        if (!o.setResult.replay) s += (o.setResult.winner == p) ? 1e6 : -1e6;
    }
    s += 1000.0 * o.ownPot - 900.0 * o.oppPot;
    if (o.penalty) s -= 2500.0;
    if (o.keep) s += 1500.0;
    for (const Puck& k : g.pucks) {
        if (!k.on || g.isDark(k)) continue;
        double near = std::max(0.0, 1.0 - nearestPocket(k.x, k.y) / 700.0);
        s += (k.owner == p) ? 40.0 * near : -30.0 * near;
    }
    return s;
}

// ---------------------------------------------------------------------------
// A single candidate shot in side-relative coordinates.
// ---------------------------------------------------------------------------
struct Cand {
    double rx = 0, ry = 0;
    double dx = 0, dy = 0;
    double speed = 0;
    double score = 0;
    double h = 0; // heuristic (aimed shots only)
};

// ---------------------------------------------------------------------------
// The planner state shared between planShotSync and AIPlanner.
// ---------------------------------------------------------------------------
struct Planner {
    Game game;
    Level L;
    Rng rng;
    int p = 0;
    char side = 'S';
    double R = 0;

    struct Piece {
        Puck k;
        double x = 0, y = 0;
    };
    std::vector<Piece> pieces;
    std::vector<std::pair<double, double>> pockets;
    std::vector<std::pair<double, double>> places;
    std::vector<Cand> cands;

    // Fallback shot if no legal placement exists.
    bool fallback = false;
    ShotResult fallbackShot;

    // Search state.
    int evalIndex = 0;
    bool sorted = false;
    bool robustDone = false;
    int robustIdx = 0;
    double bestMean = -std::numeric_limits<double>::infinity();
    int bestIdx = 0;
    bool finished = false;
    ShotResult finalShot;

    Planner(const Game& g, const std::string& level, uint32_t seed)
        : game(g.clone()), L(levelFor(level)), rng(seed) {
        p = game.turn;
        side = game.sides[p];
        R = C::STRIKER_R + C::PUCK_R;

        for (const Puck& k : game.pucks) {
            if (!k.on) continue;
            double ox, oy;
            flip(side, k.x, k.y, ox, oy);
            Piece pc;
            pc.k = k;
            pc.x = ox;
            pc.y = oy;
            pieces.push_back(pc);
        }
        for (int i = 0; i < 4; i++) {
            double ox, oy;
            flip(side, POCKETS[i][0], POCKETS[i][1], ox, oy);
            pockets.emplace_back(ox, oy);
        }

        // Candidate striker placements.
        const double rys[] = {880, 905, 930, 955, PLACE.yMax};
        for (double ry : rys) {
            for (double rx = PLACE.xMin; rx <= PLACE.xMax; rx += 20) {
                double ax, ay;
                flip(side, rx, ry, ax, ay);
                if (game.canPlace(p, ax, ay)) places.emplace_back(rx, ry);
            }
        }

        if (places.empty()) {
            fallback = true;
            double ax = 0, ay = 0;
            if (!game.defaultPlace(p, ax, ay)) {
                flip(side, MID, 945, ax, ay);
            }
            double dx, dy;
            flipDir(side, 0, -1, dx, dy);
            fallbackShot.x = ax;
            fallbackShot.y = ay;
            fallbackShot.vx = dx * 1500.0;
            fallbackShot.vy = dy * 1500.0;
            finished = true;
            finalShot = fallbackShot;
            return;
        }

        buildCandidates();
    }

    void buildCandidates() {
        // Aimed shots: each own light puck towards each pocket.
        std::vector<Cand> aimed;
        for (const Piece& t : pieces) {
            if (t.k.owner != p || game.isDark(t.k)) continue;
            for (const auto& pk : pockets) {
                double px = pk.first, py = pk.second;
                double tx = px - t.x, ty = py - t.y;
                double td = hyp(tx, ty);
                if (td < 1e-9) continue;
                double ux = tx / td, uy = ty / td;
                double gx = t.x - ux * R, gy = t.y - uy * R;
                bool blocked = false;
                for (const Piece& o : pieces) {
                    if (&o == &t) continue;
                    if (segDist(t.x, t.y, px, py, o.x, o.y) < 2 * C::PUCK_R - 0.5) {
                        blocked = true;
                        break;
                    }
                }
                for (const auto& pl : places) {
                    double sx = pl.first, sy = pl.second;
                    double dx = gx - sx, dy = gy - sy;
                    double dd = hyp(dx, dy);
                    if (dy >= 0) continue;
                    if (dd < 1e-9) continue;
                    double vx = dx / dd, vy = dy / dd;
                    double cut = vx * ux + vy * uy;
                    if (cut < 0.35) continue;
                    bool blockedPath = false;
                    for (const Piece& o : pieces) {
                        if (&o == &t) continue;
                        if (segDist(sx, sy, gx, gy, o.x, o.y) < R) {
                            blockedPath = true;
                            break;
                        }
                    }
                    if (blockedPath) continue;
                    double a = C::DECEL * 1.3;
                    double vp = std::sqrt(350.0 * 350.0 + 2.0 * a * std::max(0.0, td - 35.0));
                    double vsi = vp / (cut * (1.0 + C::E_PUCK) * C::STRIKER_M /
                                       (C::STRIKER_M + C::PUCK_M));
                    double speed = std::sqrt(vsi * vsi + 2.0 * a * dd);
                    Cand c;
                    c.rx = sx;
                    c.ry = sy;
                    c.dx = vx;
                    c.dy = vy;
                    c.speed = speed;
                    c.h = 2.0 * cut - dd / 1500.0 - td / 1500.0 - (blocked ? 1.5 : 0.0);
                    aimed.push_back(c);
                }
            }
        }
        std::stable_sort(aimed.begin(), aimed.end(),
                         [](const Cand& a, const Cand& b) { return a.h > b.h; });

        int nAimed = static_cast<int>(std::floor(L.sims * 0.7 / 3.0));
        int used = 0;
        for (size_t i = 0; i < aimed.size() && used < nAimed; i++) {
            const Cand& c = aimed[i];
            bool dup = false;
            for (const Cand& o : cands) {
                if (std::fabs(o.rx - c.rx) < 15 && std::fabs(o.ry - c.ry) < 15 &&
                    o.dx * c.dx + o.dy * c.dy > 0.9995) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            used++;
            const double mults[] = {0.9, 1.15, 1.45};
            for (double m : mults) {
                Cand cc = c;
                cc.speed = c.speed * m;
                cands.push_back(cc);
            }
        }
        while (static_cast<int>(cands.size()) < L.sims) {
            int pi = static_cast<int>(rng.uniform() * places.size());
            if (pi >= static_cast<int>(places.size())) pi = static_cast<int>(places.size()) - 1;
            const auto& pl = places[pi];
            double ang = (rng.uniform() * 2.0 - 1.0) * 1.25;
            Cand c;
            c.rx = pl.first;
            c.ry = pl.second;
            c.dx = std::sin(ang);
            c.dy = -std::cos(ang);
            c.speed = 500.0 + rng.uniform() * 4300.0;
            cands.push_back(c);
        }
    }

    double evaluate(const Cand& c) {
        Game g = game.clone();
        double ax, ay, dx, dy;
        flip(side, c.rx, c.ry, ax, ay);
        flipDir(side, c.dx, c.dy, dx, dy);
        if (!g.canPlace(p, ax, ay) || !g.canAim(p, ax, ay, dx, dy)) {
            return -std::numeric_limits<double>::infinity();
        }
        double v = std::max(C::MIN_SPEED, std::min(C::MAX_SPEED, c.speed));
        Sim sim = g.shoot(ax, ay, dx * v, dy * v);
        sim.run();
        ResolveOut o = g.resolve(sim);
        return scoreOutcome(g, o, p);
    }

    Cand jitter(const Cand& c, double s) {
        double ang = gauss(rng) * L.ang * M_PI / 180.0 * s;
        double cs = std::cos(ang), sn = std::sin(ang);
        Cand r;
        r.rx = c.rx;
        r.ry = c.ry;
        r.dx = c.dx * cs - c.dy * sn;
        r.dy = c.dx * sn + c.dy * cs;
        r.speed = c.speed * (1.0 + gauss(rng) * L.pow * s);
        return r;
    }

    // Run one unit of work. Returns true when the whole plan is finished.
    bool stepOnce() {
        if (finished) return true;

        // Phase 1: evaluate all candidates.
        if (evalIndex < static_cast<int>(cands.size())) {
            cands[evalIndex].score = evaluate(cands[evalIndex]);
            evalIndex++;
            return false;
        }

        // Sort once after all evaluations.
        if (!sorted) {
            std::stable_sort(cands.begin(), cands.end(),
                             [](const Cand& a, const Cand& b) { return a.score > b.score; });
            sorted = true;
            bestIdx = 0;
            if (!L.robust) {
                finish();
                return true;
            }
            return false;
        }

        // Phase 2: robustness check over the top 6.
        if (!robustDone) {
            int limit = std::min<int>(6, static_cast<int>(cands.size()));
            if (robustIdx >= limit) {
                robustDone = true;
                finish();
                return true;
            }
            const Cand& c = cands[robustIdx];
            if (c.score == -std::numeric_limits<double>::infinity()) {
                robustIdx++;
                return false;
            }
            // Evaluate one jittered sample per step.
            static thread_local int sub = 0;
            static thread_local double sum = 0;
            static thread_local int curIdx = -1;
            if (curIdx != robustIdx) {
                curIdx = robustIdx;
                sub = 0;
                sum = c.score;
            }
            if (sub < L.robust) {
                sum += evaluate(jitter(c, 1.0));
                sub++;
                return false;
            }
            double mean = sum / (L.robust + 1);
            if (mean > bestMean) {
                bestMean = mean;
                bestIdx = robustIdx;
            }
            robustIdx++;
            return false;
        }

        finish();
        return true;
    }

    void finish() {
        if (finished) return;
        const Cand& best = cands[bestIdx];

        // Execute with the level's hand error; fall back to the exact shot if illegal.
        Cand shot = jitter(best, 1.0);
        double ax, ay, dx, dy;
        flip(side, shot.rx, shot.ry, ax, ay);
        flipDir(side, shot.dx, shot.dy, dx, dy);
        if (!game.canAim(p, ax, ay, dx, dy)) {
            shot = best;
            flip(side, shot.rx, shot.ry, ax, ay);
            flipDir(side, shot.dx, shot.dy, dx, dy);
        }
        double v = std::max(C::MIN_SPEED, std::min(C::MAX_SPEED, shot.speed));
        finalShot.x = ax;
        finalShot.y = ay;
        finalShot.vx = dx * v;
        finalShot.vy = dy * v;
        finished = true;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Synchronous planner.
// ---------------------------------------------------------------------------
ShotResult planShotSync(const Game& game, const std::string& level, uint32_t seed) {
    Planner pl(game, level, seed);
    if (pl.finished) return pl.finalShot;
    while (!pl.stepOnce()) {
        // keep going
    }
    return pl.finalShot;
}

// ---------------------------------------------------------------------------
// Incremental planner.
// ---------------------------------------------------------------------------
struct AIPlanner::Impl {
    std::unique_ptr<Planner> planner;
};

AIPlanner::AIPlanner() : impl_(new Impl()) {}

AIPlanner::~AIPlanner() { delete impl_; }

void AIPlanner::init(const Game& game, const std::string& level, uint32_t seed) {
    impl_->planner.reset(new Planner(game, level, seed));
}

bool AIPlanner::step(int budgetEvals) {
    if (!impl_->planner) return true;
    Planner& pl = *impl_->planner;
    if (pl.finished) return true;
    for (int i = 0; i < budgetEvals; i++) {
        if (pl.stepOnce()) return true;
    }
    return pl.finished;
}

bool AIPlanner::done() const {
    return !impl_->planner || impl_->planner->finished;
}

ShotResult AIPlanner::result() const {
    if (!impl_->planner) return ShotResult{};
    return impl_->planner->finalShot;
}

} // namespace Novuss
