#include "game.hpp"

#include <cmath>
#include <limits>

namespace Novuss {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
const double POCKETS[4][2] = {
    {C::POCKET_C, C::POCKET_C},
    {C::FIELD - C::POCKET_C, C::POCKET_C},
    {C::POCKET_C, C::FIELD - C::POCKET_C},
    {C::FIELD - C::POCKET_C, C::FIELD - C::POCKET_C},
};
const double MID = C::FIELD / 2;
const double HALF_LINE = C::LINE_W / 2;
const double ZONE_LINE = C::FIELD - C::ZONE;


const Place PLACE = {
    C::ZONE + HALF_LINE + C::STRIKER_HOLE_R + 0.01,
    C::FIELD - C::ZONE - HALF_LINE - C::STRIKER_HOLE_R - 0.01,
    ZONE_LINE + HALF_LINE + C::STRIKER_HOLE_R + 0.01,
    C::FIELD - C::STRIKER_R,
};

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
double hyp(double x, double y) { return std::sqrt(x * x + y * y); }

void flip(char side, double x, double y, double& ox, double& oy) {
    if (side == 'S') {
        ox = x;
        oy = y;
    } else {
        ox = C::FIELD - x;
        oy = C::FIELD - y;
    }
}

void flipDir(char side, double dx, double dy, double& ox, double& oy) {
    if (side == 'S') {
        ox = dx;
        oy = dy;
    } else {
        ox = -dx;
        oy = -dy;
    }
}

char area(char side, double x, double y, double holeR) {
    double rx, ry;
    flip(side, x, y, rx, ry);
    (void)rx;
    if (ry - holeR > ZONE_LINE + HALF_LINE) return 's';
    if (hyp(x - MID, y - MID) + holeR < C::CIRCLE_R - HALF_LINE) return 'c';
    return 0;
}

double segDist(double ax, double ay, double bx, double by, double px, double py) {
    const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    double t = l2 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    if (t < 0) t = 0;
    else if (t > 1) t = 1;
    return hyp(ax + dx * t - px, ay + dy * t - py);
}

double powerToSpeed(double p) {
    return C::MIN_SPEED + (C::MAX_SPEED - C::MIN_SPEED) * std::pow(p, 1.5);
}

double speedToPower(double v) {
    double r = (v - C::MIN_SPEED) / (C::MAX_SPEED - C::MIN_SPEED);
    if (r < 0) r = 0;
    return std::pow(r, 1.0 / 1.5);
}

// ---------------------------------------------------------------------------
// Ray casting
// ---------------------------------------------------------------------------
RayHit castRay(const std::vector<Puck>& pucks, double ox, double oy,
               double dx, double dy, double radius) {
    RayHit best;
    best.t = std::numeric_limits<double>::infinity();
    best.index = -1;

    const double len = hyp(dx, dy);
    if (len <= 0) return best;
    const double ux = dx / len, uy = dy / len;

    // Pucks.
    for (size_t i = 0; i < pucks.size(); ++i) {
        const Puck& k = pucks[i];
        if (!k.on) continue;
        const double cx = k.x - ox, cy = k.y - oy;
        const double proj = cx * ux + cy * uy;
        if (proj < 0) continue;
        const double perp2 = cx * cx + cy * cy - proj * proj;
        const double rr = radius + C::PUCK_R;
        if (perp2 > rr * rr) continue;
        const double t = proj - std::sqrt(rr * rr - perp2);
        if (t >= 0 && t < best.t) {
            best.t = t;
            best.index = static_cast<int>(i);
        }
    }

    // Cushion walls (striker radius from each edge).
    const double lo = C::STRIKER_R;
    const double hi = C::FIELD - C::STRIKER_R;
    if (ux != 0) {
        double t = (ux > 0 ? (hi - ox) : (lo - ox)) / ux;
        if (t >= 0 && t < best.t) {
            const double y = oy + uy * t;
            if (y >= lo && y <= hi) {
                best.t = t;
                best.index = -1;
            }
        }
    }
    if (uy != 0) {
        double t = (uy > 0 ? (hi - oy) : (lo - oy)) / uy;
        if (t >= 0 && t < best.t) {
            const double x = ox + ux * t;
            if (x >= lo && x <= hi) {
                best.t = t;
                best.index = -1;
            }
        }
    }

    return best;
}

// ---------------------------------------------------------------------------
// Sim
// ---------------------------------------------------------------------------
Sim::Sim(const std::vector<Puck>& pucks, double sx, double sy, double svx, double svy,
         char shooterSide, uint32_t seed) {
    side = shooterSide;
    rng = seed ? seed : (static_cast<uint32_t>(sx * 1000.0 + sy * 10.0 + svx * 7.0 + svy * 3.0) ^ 0x9E3779B9u);
    if (!rng) rng = 0x12345678u;
    t = 0;
    acc = 0;
    strikerLaunchSpeed = hyp(svx, svy);
    strikerBalkonsCorner = -1;
    strikerBalkonsDest = 0;

    striker.id = -1;
    striker.owner = -1;
    striker.x = sx;
    striker.y = sy;
    striker.vx = svx;
    striker.vy = svy;
    striker.r = C::STRIKER_R;
    striker.inv = 1.0 / C::STRIKER_M;
    striker.on = true;
    striker.striker = true;
    striker.pocket = -1;

    bodies.clear();
    bodies.reserve(pucks.size() + 1);
    bodies.push_back(striker);
    for (const Puck& k : pucks) {
        Body b;
        b.id = k.id;
        b.owner = k.owner;
        b.x = k.x;
        b.y = k.y;
        b.vx = 0;
        b.vy = 0;
        b.r = C::PUCK_R;
        b.inv = 1.0 / C::PUCK_M;
        b.on = true;
        b.striker = false;
        b.pocket = -1;
        bodies.push_back(b);
    }

    log = SimLog();
    events = nullptr;
}

void Sim::emit(const std::string& type, const Body& b, double power) {
    if (!events) return;
    SimEvent e;
    e.type = type;
    e.x = b.x;
    e.y = b.y;
    e.power = power;
    e.striker = b.striker;
    e.id = b.id;
    e.t = t;
    events->push_back(e);
}

void Sim::cushion(Body& b, char wall, double vn, char axis, double lim) {
    if (axis == 'x') b.x = lim;
    else b.y = lim;

    if (vn > 0) {
        if (!b.striker && vn > C::JUMP_SPEED) {
            b.on = false;
            b.vx = 0;
            b.vy = 0;
            SimLog::OffEntry oe;
            oe.id = b.id;
            oe.wall = wall;
            log.off.push_back(oe);
            emit("off", b, vn);
            return;
        }

        // Striker in corner balkons region: chance to fly off, pocket, or rebound
        if (b.striker && strikerBalkonsCorner != -1) {
            int k = strikerBalkonsCorner;
            double cx = (POCKETS[k][0] < 500.0) ? 0.0 : 1000.0;
            double cy = (POCKETS[k][1] < 500.0) ? 0.0 : 1000.0;
            double diagX = (cx == 0.0) ? 0.70710678 : -0.70710678;
            double diagY = (cy == 0.0) ? 0.70710678 : -0.70710678;
            double sp = hyp(b.vx, b.vy);
            double ux = (sp > 0) ? b.vx / sp : 0;
            double uy = (sp > 0) ? b.vy / sp : 0;
            double intoApex = -(ux * diagX + uy * diagY);
            if (intoApex < 0.0) intoApex = 0.0;

            double powerNorm = (strikerLaunchSpeed - 3000.0) / 1500.0;
            if (powerNorm < 0.0) powerNorm = 0.0;
            if (powerNorm > 1.0) powerNorm = 1.0;

            double offProb = 0.06 + 0.14 * powerNorm + 0.10 * intoApex;
            double pocketProb = 0.18 + 0.10 * (1.0 - powerNorm);

            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            double roll = (rng & 0xFFFFu) / 65536.0;

            if (roll < offProb) {
                b.on = false;
                b.vx = 0;
                b.vy = 0;
                log.strikerOff = true;
                strikerBalkonsDest = 3;
                emit("off", b, vn);
                return;
            } else if (roll < offProb + pocketProb) {
                strikerBalkonsDest = 2; // pocket
            } else {
                strikerBalkonsDest = 1; // return safely
            }

            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            double rollAngle = ((rng & 0xFFFFu) / 65536.0 - 0.5) * 0.15;
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            double rollRest = 0.85 + 0.25 * ((rng & 0xFFFFu) / 65536.0);

            double e = C::E_CUSHION * rollRest;
            if (axis == 'x') {
                b.vx = -b.vx * e;
                b.vy = b.vy * e;
            } else {
                b.vx = b.vx * e;
                b.vy = -b.vy * e;
            }
            double cosA = std::cos(rollAngle), sinA = std::sin(rollAngle);
            double nvx = b.vx * cosA - b.vy * sinA;
            double nvy = b.vx * sinA + b.vy * cosA;
            b.vx = nvx;
            b.vy = nvy;

            emit("cushion", b, vn);
            goto recordCushion;
        }

        if (axis == 'x') b.vx = -b.vx * C::E_CUSHION;
        else b.vy = -b.vy * C::E_CUSHION;
        emit("cushion", b, vn);
    }

recordCushion:
    if (b.striker && !log.hasFirstContact) {
        bool found = false;
        for (char w : log.cushionsBefore) {
            if (w == wall) { found = true; break; }
        }
        if (!found) log.cushionsBefore.push_back(wall);
    }
}

void Sim::step(double dt) {
    const size_t n = bodies.size();
    t += dt;

    for (size_t i = 0; i < n; ++i) {
        Body& b = bodies[i];
        if (!b.on || (b.vx == 0 && b.vy == 0)) continue;
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        const double sp = hyp(b.vx, b.vy);
        const double ns = sp - (C::DECEL + C::DAMP * sp) * dt;
        if (ns <= 0) {
            b.vx = 0;
            b.vy = 0;
        } else {
            const double k = ns / sp;
            b.vx *= k;
            b.vy *= k;
        }
    }

    std::vector<int> contacts;
    bool hasContacts = false;

    for (size_t i = 0; i < n; ++i) {
        Body& a = bodies[i];
        if (!a.on) continue;
        for (size_t j = i + 1; j < n; ++j) {
            Body& b = bodies[j];
            if (!b.on) continue;
            const double dx = b.x - a.x, dy = b.y - a.y;
            const double rs = a.r + b.r;
            const double d2 = dx * dx + dy * dy;
            if (d2 >= rs * rs - 1e-6) continue;
            double d = std::sqrt(d2);
            if (d == 0) d = 1e-9;
            const double nx = dx / d, ny = dy / d;
            const double inv = a.inv + b.inv;
            const double corr = (rs - d) / inv;
            a.x -= nx * corr * a.inv;
            a.y -= ny * corr * a.inv;
            b.x += nx * corr * b.inv;
            b.y += ny * corr * b.inv;
            const double vn = (b.vx - a.vx) * nx + (b.vy - a.vy) * ny;
            if (vn < 0) {
                const double J = -(1 + C::E_PUCK) * vn / inv;
                a.vx -= J * a.inv * nx;
                a.vy -= J * a.inv * ny;
                b.vx += J * b.inv * nx;
                b.vy += J * b.inv * ny;
                emit("hit", a.striker ? a : b, -vn);

                if (a.striker && strikerBalkonsCorner != -1 && strikerBalkonsDest == 0) {
                    double powerNorm = (strikerLaunchSpeed - 3000.0) / 1500.0;
                    if (powerNorm < 0.0) powerNorm = 0.0;
                    if (powerNorm > 1.0) powerNorm = 1.0;
                    double offProb = 0.05 + 0.10 * powerNorm;
                    double pocketProb = 0.20 + 0.10 * (1.0 - powerNorm);
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    double roll = (rng & 0xFFFFu) / 65536.0;
                    if (roll < offProb) {
                        a.on = false;
                        a.vx = 0; a.vy = 0;
                        log.strikerOff = true;
                        strikerBalkonsDest = 3;
                        emit("off", a, -vn);
                    } else if (roll < offProb + pocketProb) {
                        strikerBalkonsDest = 2;
                    } else {
                        strikerBalkonsDest = 1;
                    }
                }
            }
            if (a.striker && !log.hasFirstContact) {
                contacts.push_back(b.id);
                hasContacts = true;
            }
        }
    }
    if (hasContacts) {
        log.firstContact = contacts;
        log.hasFirstContact = true;
    }

    for (size_t i = 0; i < n; ++i) {
        Body& b = bodies[i];
        if (!b.on) continue;
        const double r = b.r, max = C::FIELD - r;
        if (b.x < r) cushion(b, 'W', -b.vx, 'x', r);
        else if (b.x > max) cushion(b, 'E', b.vx, 'x', max);
        if (!b.on) continue;
        if (b.y < r) cushion(b, 'N', -b.vy, 'y', r);
        else if (b.y > max) cushion(b, 'S', b.vy, 'y', max);
        if (!b.on) continue;
        for (int k = 0; k < 4; ++k) {
            const double px = b.x - POCKETS[k][0], py = b.y - POCKETS[k][1];
            if (px * px + py * py < C::POCKET_R * C::POCKET_R) {
                if (b.striker) {
                    double sp = hyp(b.vx, b.vy);
                    double cx = (POCKETS[k][0] < 500.0) ? 0.0 : 1000.0;
                    double cy = (POCKETS[k][1] < 500.0) ? 0.0 : 1000.0;
                    bool inward = (b.vx * (cx - b.x) + b.vy * (cy - b.y) > 0.0);
                    bool isHighPower = (strikerLaunchSpeed >= 3000.0 || sp >= 2400.0);

                    if (isHighPower) {
                        if (strikerBalkonsCorner != k) {
                            if (inward) {
                                strikerBalkonsCorner = k;
                                strikerBalkonsDest = 0;
                                continue;
                            }
                        } else {
                            if (strikerBalkonsDest != 2 && sp >= 150.0) {
                                continue;
                            }
                        }
                    }
                }
                b.on = false;
                emit("pocket", b, hyp(b.vx, b.vy));
                b.vx = 0;
                b.vy = 0;
                b.pocket = k;
                if (b.striker) log.strikerPocketed = true;
                else log.pocketed.push_back(b.id);
                break;
            }
        }
    }

    // Clear balkonsCorner once striker is safely away from pocket hole
    if (bodies[0].on && strikerBalkonsCorner != -1) {
        const Body& st = bodies[0];
        double px = st.x - POCKETS[strikerBalkonsCorner][0];
        double py = st.y - POCKETS[strikerBalkonsCorner][1];
        if (px * px + py * py >= (C::POCKET_R + 15.0) * (C::POCKET_R + 15.0)) {
            strikerBalkonsCorner = -1;
            strikerBalkonsDest = 0;
        }
    }

    // 3.15: once out of the edge zone, the striker must not slide back in.
    Body& s = bodies[0];
    if (s.on) {
        double rx, ry;
        flip(side, s.x, s.y, rx, ry);
        (void)rx;
        if (!log.strikerLeft) {
            if (ry < ZONE_LINE - HALF_LINE - C::STRIKER_HOLE_R) log.strikerLeft = true;
        } else if (ry > ZONE_LINE + HALF_LINE + C::STRIKER_HOLE_R) {
            log.strikerReturned = true;
        }
    }
    striker = s;
}

bool Sim::atRest() const {
    for (const Body& b : bodies) {
        if (b.on && (b.vx != 0 || b.vy != 0)) return false;
    }
    return true;
}

void Sim::advance(double dt) {
    acc += dt;
    while (acc >= C::DT) {
        acc -= C::DT;
        if (atRest()) {
            acc = 0;
            break;
        }
        step(C::DT);
    }
}

void Sim::run(double maxT) {
    const double end = t + (maxT > 0 ? maxT : 60.0);
    while (!atRest() && t < end) step(C::DT);
    if (!atRest()) {
        for (Body& b : bodies) {
            b.vx = 0;
            b.vy = 0;
        }
    }
}

} // namespace Novuss
