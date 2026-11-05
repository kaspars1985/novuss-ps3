#include "game.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Novuss {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
Game::Game() { newSet(matchFirst); }

Game::Game(const GameOpts& opts) {
    setsToWin = opts.setsToWin ? opts.setsToWin : 2;
    matchFirst = opts.first ? opts.first : 0;
    sets[0] = 0;
    sets[1] = 0;
    setNo = 1;
    sides[0] = 'S';
    sides[1] = 'N';
    winner = -1;
    newSet(matchFirst);
}

// 1.2: each player's eight pucks in a tight row against the cushion on the
// opponent's side, four on each side of the midline.
void Game::newSet(int firstPlayer) {
    first = firstPlayer;
    turn = firstPlayer;
    series[0] = 0;
    series[1] = 0;
    series[firstPlayer] = 1;
    debt[0] = 0;
    debt[1] = 0;
    firstClear = -1;
    miss[0] = 0;
    miss[1] = 0;
    pre = Pre();
    pucks.clear();
    for (int p = 0; p < 2; p++) {
        for (int k = 0; k < 8; k++) {
            double ox = 0, oy = 0;
            flip(sides[p], MID + (k - 3.5) * 2 * C::PUCK_R, C::PUCK_R, ox, oy);
            Puck pk;
            pk.id = p * 8 + k;
            pk.owner = p;
            pk.x = ox;
            pk.y = oy;
            pk.on = true;
            pucks.push_back(pk);
        }
    }
}

Game Game::clone() const {
    Game g;
    g.setsToWin = setsToWin;
    g.matchFirst = matchFirst;
    g.sets[0] = sets[0];
    g.sets[1] = sets[1];
    g.setNo = setNo;
    g.sides[0] = sides[0];
    g.sides[1] = sides[1];
    g.winner = winner;
    g.first = first;
    g.turn = turn;
    g.series[0] = series[0];
    g.series[1] = series[1];
    g.debt[0] = debt[0];
    g.debt[1] = debt[1];
    g.firstClear = firstClear;
    g.miss[0] = miss[0];
    g.miss[1] = miss[1];
    g.pucks = pucks;
    g.pre = Pre();
    return g;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
int Game::onTable(int p) const {
    int n = 0;
    for (const Puck& k : pucks)
        if (k.on && k.owner == p) n++;
    return n;
}

bool Game::isDark(const Puck& k) const {
    return area(sides[k.owner], k.x, k.y, C::PUCK_HOLE_R) != 0;
}

// Any piece in the band along player p's own cushion: never a valid direct
// first target.
bool Game::inOwnStrip(int p, const Puck& k) const {
    return area(sides[p], k.x, k.y, C::PUCK_HOLE_R) == 's';
}

bool Game::hasLight(int p) const {
    for (const Puck& k : pucks)
        if (k.on && k.owner == p && !isDark(k)) return true;
    return false;
}

bool Game::hasDark(int p) const {
    for (const Puck& k : pucks)
        if (k.on && k.owner == p && isDark(k)) return true;
    return false;
}

// 3.9, 3.10: striker inside the own edge zone, touching no puck.
bool Game::canPlace(int p, double x, double y) const {
    double rx = 0, ry = 0;
    flip(sides[p], x, y, rx, ry);
    if (rx < PLACE.xMin || rx > PLACE.xMax || ry < PLACE.yMin || ry > PLACE.yMax) return false;
    const double m = C::STRIKER_R + C::PUCK_R;
    for (const Puck& k : pucks) {
        if (k.on && (k.x - x) * (k.x - x) + (k.y - y) * (k.y - y) < m * m) return false;
    }
    return true;
}

// Move a requested striker position to the closest legal one by clamping it to
// the zone and sliding it off pucks. Returns false if that fails.
bool Game::slidePlace(int p, double x, double y, double& ox, double& oy) const {
    const char side = sides[p];
    const double m = C::STRIKER_R + C::PUCK_R + 0.01;
    double rx = 0, ry = 0;
    flip(side, x, y, rx, ry);
    for (int it = 0; it < 8; it++) {
        rx = std::max(PLACE.xMin, std::min(PLACE.xMax, rx));
        ry = std::max(PLACE.yMin, std::min(PLACE.yMax, ry));
        double ax = 0, ay = 0;
        flip(side, rx, ry, ax, ay);
        if (canPlace(p, ax, ay)) {
            ox = ax;
            oy = ay;
            return true;
        }
        for (const Puck& k : pucks) {
            if (!k.on) continue;
            double krx = 0, kry = 0;
            flip(side, k.x, k.y, krx, kry);
            const double dx = rx - krx, dy = ry - kry, d = hyp(dx, dy);
            if (d < m) {
                if (d > 1e-6) {
                    rx = krx + dx / d * m;
                    ry = kry + dy / d * m;
                } else {
                    rx = krx + m;
                    ry = kry;
                }
            }
        }
    }
    return false;
}

bool Game::defaultPlace(int p, double prefX, double& ox, double& oy) const {
    const char side = sides[p];
    const double cx = prefX;
    const double rys[] = {945, 925, 965, 905, PLACE.yMax, PLACE.yMin};
    for (double ry : rys) {
        for (double d = 0; d < 740; d += 3) {
            const double rxs[2] = {cx - d, cx + d};
            const int n = d ? 2 : 1;
            for (int i = 0; i < n; i++) {
                const double rx = rxs[i];
                if (rx < PLACE.xMin || rx > PLACE.xMax) continue;
                double ax = 0, ay = 0;
                flip(side, rx, ry, ax, ay);
                if (canPlace(p, ax, ay)) {
                    ox = ax;
                    oy = ay;
                    return true;
                }
            }
        }
    }
    return false;
}

// 3.32: the cue lies over the player's own cushion, clear of its inner corners,
// so the stroke points away from the player.
bool Game::canAim(int p, double x, double y, double dx, double dy) const {
    const char side = sides[p];
    double rx = 0, ry = 0, ddx = 0, ddy = 0;
    flip(side, x, y, rx, ry);
    flipDir(side, dx, dy, ddx, ddy);
    if (ddy >= 0) return false;
    const double xh = rx - ddx * (C::FIELD - ry) / -ddy;
    return xh > C::CUE_MARGIN && xh < C::FIELD - C::CUE_MARGIN;
}

bool Game::isFree(double x, double y, int ignore) const {
    const double r = C::PUCK_R;
    if (x < r || x > C::FIELD - r || y < r || y > C::FIELD - r) return false;
    for (int i = 0; i < 4; i++) {
        if (hyp(x - POCKETS[i][0], y - POCKETS[i][1]) < C::POCKET_R + 2) return false;
    }
    for (const Puck& k : pucks) {
        if (k.on && k.id != ignore &&
            (k.x - x) * (k.x - x) + (k.y - y) * (k.y - y) < 4 * r * r - 1e-6)
            return false;
    }
    return true;
}

void Game::freeSpot(double x, double y, int ignore, double& ox, double& oy) const {
    if (isFree(x, y, ignore)) {
        ox = x;
        oy = y;
        return;
    }
    for (double rad = 1; rad < 1000; rad += 1) {
        const int steps = std::max(8, (int)std::ceil(2 * M_PI * rad / 2));
        for (int i = 0; i < steps; i++) {
            const double a = 2 * M_PI * i / steps;
            const double px = x + std::cos(a) * rad;
            const double py = y + std::sin(a) * rad;
            if (isFree(px, py, ignore)) {
                ox = px;
                oy = py;
                return;
            }
        }
    }
    ox = x;
    oy = y;
}

// 3.20: a puck that left the table goes to the centre of the circle; if taken,
// to where the cross line on the side it fell meets the circle; then the other
// three such points; then halfway between the centre and those points.
void Game::offSpot(char wall, double& ox, double& oy) const {
    double d[2];
    switch (wall) {
        case 'N': d[0] = 0; d[1] = -1; break;
        case 'S': d[0] = 0; d[1] = 1; break;
        case 'W': d[0] = -1; d[1] = 0; break;
        case 'E': d[0] = 1; d[1] = 0; break;
        default: d[0] = 0; d[1] = 0; break;
    }
    const double dirs[4][2] = {
        {d[0], d[1]},
        {d[1], -d[0]},
        {-d[1], d[0]},
        {-d[0], -d[1]},
    };
    const double rr = C::CIRCLE_R - HALF_LINE - C::PUCK_R;
    double cands[9][2];
    int nc = 0;
    cands[nc][0] = MID;
    cands[nc][1] = MID;
    nc++;
    for (int i = 0; i < 4; i++) {
        cands[nc][0] = MID + dirs[i][0] * rr;
        cands[nc][1] = MID + dirs[i][1] * rr;
        nc++;
    }
    for (int i = 0; i < 4; i++) {
        cands[nc][0] = MID + dirs[i][0] * rr / 2;
        cands[nc][1] = MID + dirs[i][1] * rr / 2;
        nc++;
    }
    for (int i = 0; i < nc; i++) {
        if (isFree(cands[i][0], cands[i][1], -1)) {
            ox = cands[i][0];
            oy = cands[i][1];
            return;
        }
    }
    freeSpot(MID, MID, -1, ox, oy);
}

// 7.3: penalty puck on the edge-zone midline, tight against the cushion on the
// opponent's side; if taken, as close to the midline as possible. Ties go to
// the side with fewer of the penalised player's pucks, then fewer pucks in
// total, then the side the shot was taken from.
void Game::penaltySpot(int p, bool hasShotX, double shotX, double& ox, double& oy) const {
    const char side = sides[p];
    const double R = C::PUCK_R;
    auto at = [&](double rx, double& ax, double& ay) -> bool {
        flip(side, rx, R, ax, ay);
        return isFree(ax, ay, -1);
    };
    for (double d = 0; d <= MID - R; d += 0.5) {
        double lx = 0, ly = 0, rx = 0, ry = 0;
        const bool left = at(MID - d, lx, ly);
        const bool right = d ? at(MID + d, rx, ry) : false;
        if (left && right) {
            auto count = [&](bool own, int& l, int& r) {
                l = 0;
                r = 0;
                for (const Puck& k : pucks) {
                    if (!k.on || (own && k.owner != p)) continue;
                    double krx = 0, kry = 0;
                    flip(side, k.x, k.y, krx, kry);
                    if (krx < MID) l++;
                    else if (krx > MID) r++;
                }
            };
            int ml, mr, al, ar;
            count(true, ml, mr);
            count(false, al, ar);
            if (ml != mr) {
                if (ml < mr) { ox = lx; oy = ly; }
                else { ox = rx; oy = ry; }
                return;
            }
            if (al != ar) {
                if (al < ar) { ox = lx; oy = ly; }
                else { ox = rx; oy = ry; }
                return;
            }
            if (hasShotX && shotX > MID) { ox = rx; oy = ry; }
            else { ox = lx; oy = ly; }
            return;
        }
        if (left) { ox = lx; oy = ly; return; }
        if (right) { ox = rx; oy = ry; return; }
    }
    double ax = 0, ay = 0;
    flip(side, MID, R, ax, ay);
    freeSpot(ax, ay, -1, ox, oy);
}

int Game::placePenalties(int p, int n, bool hasShotX, double shotX, std::vector<Mark>* marks) {
    int placed = 0;
    for (Puck& k : pucks) {
        if (placed >= n) break;
        if (k.owner != p || k.on) continue;
        double sx = 0, sy = 0;
        penaltySpot(p, hasShotX, shotX, sx, sy);
        k.x = sx;
        k.y = sy;
        k.on = true;
        placed++;
        if (marks) marks->push_back(Mark{k.id, "penalty"});
    }
    return placed;
}

// Debt pucks (2.15, 7.4-7.6) are placed as soon as pocketed pucks are available.
int Game::placeDebt(int p, bool hasShotX, double shotX, std::vector<Mark>* marks) {
    const int n = placePenalties(p, debt[p], hasShotX, shotX, marks);
    debt[p] -= n;
    return n;
}

void Game::passTurn() {
    turn = 1 - turn;
    series[turn]++;
}

// ---------------------------------------------------------------------------
// Shoot
// ---------------------------------------------------------------------------
Sim Game::shoot(double x, double y, double vx, double vy) {
    const int p = turn, q = 1 - p;
    pre = Pre();
    pre.valid = true;
    for (const Puck& k : pucks) {
        if (!k.on) continue;
        PrePuck pp;
        pp.x = k.x;
        pp.y = k.y;
        pp.owner = k.owner;
        pp.dark = isDark(k);
        pp.myArea = area(sides[p], k.x, k.y, C::PUCK_HOLE_R);
        pp.oppArea = area(sides[q], k.x, k.y, C::PUCK_HOLE_R);
        pre.pucks[k.id] = pp;
    }
    pre.light[0] = hasLight(0);
    pre.light[1] = hasLight(1);
    pre.dark = hasDark(p);
    pre.on[0] = onTable(0);
    pre.on[1] = onTable(1);
    double sx = 0, sy = 0;
    flip(sides[p], x, y, sx, sy);
    pre.shotX = sx;

    std::vector<Puck> live;
    for (const Puck& k : pucks)
        if (k.on) live.push_back(k);
    return Sim(live, x, y, vx, vy, sides[p]);
}

// ---------------------------------------------------------------------------
// Resolve
// ---------------------------------------------------------------------------
ResolveOut Game::resolve(Sim& sim) {
    const int p = turn, q = 1 - p;
    const char side = sides[p];
    const Pre& preR = pre;
    const SimLog& log = sim.log;

    ResolveOut out;
    out.shooter = p;

    auto msg = [&](const std::string& k) {
        ShotMsg m;
        m.k = k;
        out.msgs.push_back(m);
    };
    auto msgN = [&](const std::string& k, int n) {
        ShotMsg m;
        m.k = k;
        m.hasN = true;
        m.n = n;
        out.msgs.push_back(m);
    };
    auto own = [&](int id) { return preR.pucks.at(id).owner == p; };
    const char far = side == 'S' ? 'N' : 'S';
    bool farBefore = false;
    for (char c : log.cushionsBefore)
        if (c == far) { farBefore = true; break; }
    bool rebound = farBefore;
    if (!rebound) {
        for (char c : log.cushionsBefore)
            if (c == 'W' || c == 'E') { rebound = true; break; }
    }
    const std::vector<int>& fc = log.firstContact;
    const bool light = preR.light[p];

    // Was it a correct stroke (2.8, 4.1-4.3)?
    std::string reason;
    bool hasReason = false;
    std::vector<int> zoneHit;
    if (fc.empty()) {
        if (!light && farBefore) out.correct = true; // 2.8.4, 4.4
        else {
            reason = light ? "noContact" : "onlyDark"; // 2.8 not met / 7.1.6
            hasReason = true;
        }
    } else {
        bool hitsOpp = false;
        for (int id : fc)
            if (!own(id)) { hitsOpp = true; break; }
        if (!rebound) {
            // 3.10, 4.1: a direct stroke may not first touch any piece in the own
            // edge zone or an own dark puck in the centre circle.
            for (int id : fc) {
                const PrePuck& pp = preR.pucks.at(id);
                if (pp.myArea == 's' || (pp.owner == p && pp.dark)) zoneHit.push_back(id);
            }
            if (hitsOpp) { reason = "oppFirst"; hasReason = true; } // 7.1.2
            else if (!zoneHit.empty()) { reason = "darkDirect"; hasReason = true; } // 7.1.8
            else out.correct = true; // 2.8.3
        } else if (light) {
            if (hitsOpp) { reason = "oppFirstRebound"; hasReason = true; } // 7.1.3
            else out.correct = true; // 4.2, 4.3.1
        } else {
            bool allCircle = true;
            for (int id : fc)
                if (preR.pucks.at(id).myArea != 'c') { allCircle = false; break; }
            if (farBefore || (!hitsOpp && allCircle)) out.correct = true; // 2.8.4, 2.8.5, 4.2
            else { reason = "onlyDark"; hasReason = true; } // 7.1.6
        }
    }

    std::map<int, Body> fin;
    for (const Body& b : sim.bodies)
        if (!b.striker) fin[b.id] = b;
    std::set<int> offIds;
    for (const auto& o : log.off) offIds.insert(o.id);
    auto moved = [&](int id) -> bool {
        const Body& b = fin.at(id);
        const PrePuck& s = preR.pucks.at(id);
        return !b.on || hyp(b.x - s.x, b.y - s.y) > 0.5;
    };

    std::string lost;
    bool hasLost = false;
    // 8.1.4: direct first contact in the own dark area, and other pieces moved too.
    if (!zoneHit.empty()) {
        for (const auto& kv : fin) {
            const int id = kv.first;
            if (std::find(fc.begin(), fc.end(), id) != fc.end()) continue;
            if (moved(id)) { lost = "814"; hasLost = true; break; }
        }
    }

    std::set<int> restore;
    if (!out.correct && !hasLost) {
        // 7.1.8: the piece moved by the forbidden direct contact goes back.
        for (int id : zoneHit)
            if (moved(id)) restore.insert(id);
        // 4.8, 7.1.11, 8.1.1: own pucks knocked out of the own dark area.
        std::vector<int> knocked;
        for (const auto& kv : fin) {
            const int id = kv.first;
            const Body& b = kv.second;
            const PrePuck& s = preR.pucks.at(id);
            if (s.owner == p && s.dark && moved(id) &&
                (!b.on || area(side, b.x, b.y, C::PUCK_HOLE_R) == 0))
                knocked.push_back(id);
        }
        if (knocked.size() >= 2) { lost = "811"; hasLost = true; }
        else for (int id : knocked) restore.insert(id);
        // 4.6, 4.8, 7.1.11: opponent pucks knocked off the table or driven into
        // the opponent's dark area are restored - unless they only moved within
        // the band, or from the centre circle into the band. Pocketed ones stay
        // pocketed (4.9).
        for (const auto& kv : fin) {
            const int id = kv.first;
            const Body& b = kv.second;
            if (own(id) || !moved(id)) continue;
            if (offIds.count(id)) { restore.insert(id); continue; }
            if (!b.on) continue;
            const char a0 = preR.pucks.at(id).oppArea;
            const char a1 = area(sides[q], b.x, b.y, C::PUCK_HOLE_R);
            if (a1 && !(a1 == 's' && a0) && !(a1 == 'c' && a0 == 'c'))
                restore.insert(id);
        }
    }

    out.penalty = !out.correct || log.strikerPocketed || log.strikerOff;
    if (hasReason) msg("pen_" + reason);
    if (log.strikerPocketed) msg("pen_strikerPocket"); // 7.1.4
    if (log.strikerOff) msg("pen_strikerOff"); // 7.1.5

    // Bring the table up to date.
    for (Puck& k : pucks) {
        auto it = fin.find(k.id);
        if (it == fin.end()) continue;
        const Body& b = it->second;
        if (!b.on) k.on = false;
        else { k.x = b.x; k.y = b.y; }
    }
    if (!hasLost) {
        for (int id : restore) {
            Puck& k = pucks[id];
            const PrePuck& s = preR.pucks.at(id);
            k.on = false;
            double fx = 0, fy = 0;
            freeSpot(s.x, s.y, -1, fx, fy);
            k.x = fx;
            k.y = fy;
            k.on = true;
            out.marks.push_back(Mark{id, "restore"});
        }
        for (const auto& o : log.off) {
            if (restore.count(o.id)) continue;
            Puck& k = pucks[o.id];
            double fx = 0, fy = 0;
            offSpot(o.wall, fx, fy);
            k.x = fx;
            k.y = fy;
            k.on = true;
            out.marks.push_back(Mark{o.id, "off"});
        }
        if (!restore.empty()) msgN("restored", (int)restore.size());
    }

    std::vector<int> pot;
    for (int id : log.pocketed)
        if (!restore.count(id)) pot.push_back(id);
    int ownPot = 0;
    for (int id : pot)
        if (own(id)) ownPot++;
    out.ownPot = ownPot;
    out.oppPot = (int)pot.size() - ownPot;
    int offLeft = 0;
    for (const auto& o : log.off)
        if (!restore.count(o.id)) offLeft++;
    const bool returned = log.strikerReturned && !preR.dark; // 3.15, 6.1.5
    out.keep = !out.penalty && !hasLost && out.ownPot > 0 && out.oppPot == 0 && offLeft == 0 && !returned;

    if (out.ownPot) msgN("pocketed", out.ownPot);
    if (out.oppPot) msgN("oppPocketed", out.oppPot); // 6.1.2
    if (offLeft) msgN("off", offLeft); // 3.20, 6.1.3
    if (returned) msg("returned");
    if (!out.penalty && !hasLost && !out.ownPot) msg("noOwn"); // 6.1.1

    if (hasLost) {
        msg("lost_" + lost);
        out.setResult.has = true;
        out.setResult.winner = q;
        out.setResult.k = "lost_" + lost;
        return out;
    }

    const int ownOn = onTable(p), oppOn = onTable(q);
    const int ownB = preR.on[p], oppB = preR.on[q];
    if (ownB && !ownOn && oppB && !oppOn) {
        // 12.5: both last pucks in one stroke: replay, or a lost set if it
        // earned a penalty.
        out.setResult.has = true;
        if (out.penalty) {
            out.setResult.winner = q;
            out.setResult.k = "lost_bothLast";
        } else {
            out.setResult.replay = true;
            out.setResult.swap = false;
            out.setResult.k = "replay_bothLast";
        }
    } else if (ownB && !ownOn) {
        if (!out.keep) {
            out.setResult.has = true;
            out.setResult.winner = q;
            out.setResult.k = "lost_lastFoul"; // 12.4 note
        } else if (debt[p]) {
            const int n = placeDebt(p, true, preR.shotX, &out.marks); // 7.5
            msgN("debtPlaced", n);
        } else if (p == first && series[p] == 1 && series[q] == 0) {
            firstClear = p; // 12.4
            msg("firstClear");
            passTurn();
        } else if (firstClear == q) {
            out.setResult.has = true;
            out.setResult.replay = true;
            out.setResult.swap = true;
            out.setResult.k = "replay_bothClear"; // 12.4
        } else {
            out.setResult.has = true;
            out.setResult.winner = p;
            out.setResult.k = "win"; // 12.3
        }
    } else if (oppB && !oppOn && !debt[q]) {
        out.setResult.has = true;
        out.setResult.winner = q;
        out.setResult.k = "win_oppLast";
    } else {
        if (oppB && !oppOn) msgN("debtPlacedOpp", placeDebt(q, false, 0, &out.marks));
        if (!out.keep) {
            if (firstClear == q) {
                out.setResult.has = true;
                out.setResult.winner = q;
                out.setResult.k = "win_firstClear"; // 12.4
            } else {
                const int owed = (out.penalty ? 1 : 0) + debt[p];
                const int n = placePenalties(p, owed, true, preR.shotX, &out.marks);
                debt[p] = owed - n;
                if (out.penalty) msg(n ? "penaltyPlaced" : "debt"); // 7.3, 7.4
                else if (n) msgN("debtPlaced", n);
                passTurn();
            }
        }
    }

    // 12.6: both players down to dark pucks and each missing three times.
    if (!out.setResult.has) {
        if (!preR.light[0] && !preR.light[1]) {
            miss[p] = !fc.empty() ? 0 : miss[p] + 1;
            if (miss[0] >= 3 && miss[1] >= 3) {
                out.setResult.has = true;
                out.setResult.replay = true;
                out.setResult.swap = false;
                out.setResult.k = "replay_stuck";
            }
        } else {
            miss[0] = 0;
            miss[1] = 0;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Next set
// ---------------------------------------------------------------------------
void Game::nextSet(const SetResult& res) {
    if (res.replay) {
        if (res.swap) {
            std::swap(sides[0], sides[1]);
            newSet(1 - first);
        } else {
            newSet(first);
        }
        return;
    }
    sets[res.winner]++;
    if (sets[res.winner] >= setsToWin) {
        winner = res.winner;
        return;
    }
    setNo++;
    std::swap(sides[0], sides[1]); // 12.2
    newSet(setNo % 2 ? matchFirst : 1 - matchFirst);
}

// ---------------------------------------------------------------------------
// Log entries
// ---------------------------------------------------------------------------
std::vector<LogEntry> logEntries(const ResolveOut& out, int setNo) {
    std::vector<LogEntry> list;
    for (const ShotMsg& m : out.msgs) {
        if (m.k.rfind("lost_", 0) == 0) continue; // reported with the set result
        LogEntry e;
        e.k = m.k;
        e.who = out.shooter;
        e.set = setNo;
        if (m.hasN) {
            e.hasN = true;
            e.n = m.n;
        }
        list.push_back(e);
    }
    if (out.setResult.has) {
        LogEntry e;
        e.k = "set";
        e.hasRes = true;
        e.res = out.setResult;
        e.hasShooter = true;
        e.shooter = out.shooter;
        e.set = setNo;
        list.push_back(e);
    }
    return list;
}

} // namespace Novuss
