#include "game.hpp"
#include "audio.hpp"
#include "i18n.hpp"
#include "font.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

using namespace Novuss;

static int g_fail = 0, g_checks = 0;
static const char* g_t = "";

#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; \
    std::printf("  [FAIL] %s:%d (%s) %s\n", __FILE__, __LINE__, g_t, #c); } } while (0)
#define CHECK_NEAR(a,b,e) do { ++g_checks; double _a=(a),_b=(b); \
    if (std::fabs(_a-_b)>(e)) { ++g_fail; \
    std::printf("  [FAIL] %s:%d (%s) |%g-%g|>%g\n", __FILE__, __LINE__, g_t, _a, _b, (double)(e)); } } while (0)
#define CHECK_STR(a,b) do { ++g_checks; std::string _a=(a),_b=(b); \
    if (_a!=_b) { ++g_fail; \
    std::printf("  [FAIL] %s:%d (%s) \"%s\"!=\"%s\"\n", __FILE__, __LINE__, g_t, _a.c_str(), _b.c_str()); } } while (0)

static void begin(const char* n) { g_t = n; std::printf("[ RUN  ] %s\n", n); }
static void end() { std::printf("[ DONE ] %s\n", g_t); }

static bool hasMsg(const ResolveOut& o, const std::string& k) {
    for (auto& m : o.msgs) if (m.k == k) return true;
    return false;
}
static int countOn(const Game& g, int owner) {
    int n = 0; for (auto& p : g.pucks) if (p.on && p.owner == owner) ++n; return n;
}
static void clearOwner(Game& g, int owner) {
    for (auto& p : g.pucks) if (p.owner == owner) p.on = false;
}
static Puck* firstOwn(Game& g, int owner) {
    for (auto& p : g.pucks) if (p.owner == owner) return &p; return nullptr;
}

// 1. Initial setup
static void testSetup() {
    begin("initial_setup");
    Game g;
    CHECK(g.pucks.size() == 16u);
    CHECK(countOn(g, 0) == 8);
    CHECK(countOn(g, 1) == 8);
    for (auto& p : g.pucks) {
        CHECK(p.on);
        if (p.owner == 0) CHECK_NEAR(p.y, C::PUCK_R, 1e-6);
        else               CHECK_NEAR(p.y, C::FIELD - C::PUCK_R, 1e-6);
    }
    CHECK(g.sides[0] == 'S');
    CHECK(g.sides[1] == 'N');
    CHECK(g.winner == -1);
    CHECK(g.setNo == 1);
    end();
}

// 2. area / isDark / inOwnStrip
static void testArea() {
    begin("area_visibility");
    CHECK(area('S', MID, C::FIELD - C::RAIL - 5.0, C::PUCK_HOLE_R) == 's');
    CHECK(area('N', MID, C::RAIL + 5.0, C::PUCK_HOLE_R) == 's');
    CHECK(area('S', MID, MID, C::PUCK_HOLE_R) == 'c');
    CHECK(area('S', MID, MID - 200.0, C::PUCK_HOLE_R) == 0);

    Game g;
    Puck& p = g.pucks[0];
    p.x = MID; p.y = MID; p.on = true;
    CHECK(g.isDark(p));
    p.x = 200.0; p.y = 500.0;
    CHECK(!g.isDark(p));
    p.x = MID; p.y = C::FIELD - C::RAIL - 5.0;
    CHECK(g.inOwnStrip(0, p));
    p.y = C::RAIL + 5.0;
    CHECK(!g.inOwnStrip(0, p));
    CHECK(g.inOwnStrip(1, p));
    end();
}

// 3. Placement / aim
static void testPlacement() {
    begin("placement_aim");
    Game g;
    double px = MID, py = C::FIELD - C::RAIL - 5.0;
    for (auto& p : g.pucks)
        if (hyp(p.x - px, p.y - py) < 2.0 * C::PUCK_R + 1.0) p.on = false;
    CHECK(g.canPlace(0, px, py));
    CHECK(!g.canPlace(0, MID, C::RAIL + 5.0));
    CHECK(!g.canPlace(0, MID, MID));

    double ox = 0, oy = 0;
    CHECK(g.slidePlace(0, px, py, ox, oy));
    CHECK(g.canPlace(0, ox, oy));

    Game g2; clearOwner(g2, 0);
    double dx = 0, dy = 0;
    CHECK(g2.defaultPlace(0, dx, dy));
    CHECK(g2.canPlace(0, dx, dy));

    Game g3; clearOwner(g3, 0);
    double sx = MID, sy = C::FIELD - C::RAIL - C::STRIKER_R - 1.0;
    CHECK(g3.canAim(0, sx, sy, 0.0, -1.0));
    CHECK(!g3.canAim(0, sx, sy, 0.0, 1.0));

    // Obstacle hopping check: puck at (500, 945) blocks continuous sliding along baseline.
    // Hopping scan finds free placement spot on the other side.
    Game gHop;
    Puck blockPuck;
    blockPuck.id = 0; blockPuck.owner = 0; blockPuck.on = true;
    blockPuck.x = 500.0; blockPuck.y = 945.0;
    gHop.pucks = {blockPuck};

    // Strikers cannot be placed within STRIKER_R + PUCK_R (37.5 mm) of the puck
    CHECK(!gHop.canPlace(0, 500.0, 945.0));
    CHECK(!gHop.canPlace(0, 520.0, 945.0));
    CHECK(!gHop.canPlace(0, 480.0, 945.0));

    // Clear spots on either side of the blocking puck:
    CHECK(gHop.canPlace(0, 450.0, 945.0));
    CHECK(gHop.canPlace(0, 550.0, 945.0));

    // Striker touches puck on the left at 462.5 (500 - 37.5).
    // Sliding further right towards 490 gets pushed back to 462.5:
    double hox = 0, hoy = 0;
    CHECK(gHop.slidePlace(0, 490.0, 945.0, hox, hoy));
    CHECK_NEAR(hox, 500.0 - (C::STRIKER_R + C::PUCK_R), 0.1);

    // Obstacle hopping: moving right from 462.5 over the blocking puck at 500.
    // The scan steps over the blocked zone (463..537) and hops to the first legal spot >= 537.5:
    bool hopped = false;
    double hopX = 0;
    for (double step = 6.0; step <= 750.0; step += 4.0) {
        double tx = 462.5 + step;
        double ty = 945.0;
        if (gHop.canPlace(0, tx, ty)) {
            hopX = tx;
            hopped = true;
            break;
        }
    }
    CHECK(hopped);
    CHECK(hopX >= 500.0 + C::STRIKER_R + C::PUCK_R);
    end();
}

// 4. Sim physics
static void testSim() {
    begin("sim_physics");
    // Elastic collision
    {
        std::vector<Puck> ps;
        Puck a; a.id = 0; a.owner = 0; a.x = 500; a.y = 500;
        Puck b; b.id = 1; b.owner = 1; b.x = 540; b.y = 500;
        ps.push_back(a); ps.push_back(b);
        Sim s(ps, 400, 500, 1000, 0, 'S');
        s.run(5.0);
        CHECK(s.bodies[1].x > 540.0);
    }
    // Cushion rebound
    {
        std::vector<Puck> ps;
        Puck a; a.id = 0; a.owner = 0; a.x = 900; a.y = 500;
        ps.push_back(a);
        Sim s(ps, 800, 500, 2000, 0, 'S');
        s.run(5.0);
        CHECK(s.bodies[0].x <= C::FIELD - C::RAIL - C::PUCK_R + 1.0);
    }
    // Pocket detection
    {
        std::vector<Puck> ps;
        Puck a; a.id = 0; a.owner = 0;
        a.x = POCKETS[0][0] + 40.0; a.y = POCKETS[0][1] + 40.0;
        ps.push_back(a);
        double dx = POCKETS[0][0] - 200.0, dy = POCKETS[0][1] - 200.0;
        Sim s(ps, 200, 200, dx * 5.0, dy * 5.0, 'S');
        s.run(5.0);
        CHECK(s.log.strikerPocketed || !s.log.pocketed.empty());
    }
    // strikerReturned: empty table, shoot north, rebound off North wall
    {
        std::vector<Puck> ps;
        Sim s(ps, 500, 920, 0, -3800, 'S');
        s.run(10.0);
        CHECK(s.log.strikerLeft);
        CHECK(s.log.strikerReturned);
    }
    // Pocket jumping: high-speed striker (>= 3800) skips pocket hole to balcony
    {
        std::vector<Puck> ps;
        // Shoot striker towards corner (15, 15) crossing through top-left pocket (65, 65) at 4200 speed
        double dirX = 15.0 - 200.0, dirY = 15.0 - 200.0;
        double len = std::sqrt(dirX * dirX + dirY * dirY);
        Sim sFast(ps, 200, 200, (dirX / len) * 4200.0, (dirY / len) * 4200.0, 'S');
        sFast.step(0.04); // advance into pocket zone
        // Fast striker does not pocket while speed >= 3800
        CHECK(!sFast.log.strikerPocketed);

        // Slow striker (< 3800) into pocket gets swallowed
        Sim sSlow(ps, 200, 200, (dirX / len) * 2000.0, (dirY / len) * 2000.0, 'S');
        sSlow.run(2.0);
        CHECK(sSlow.log.strikerPocketed);

        // Multiple high-power shots into balkons with varied angles/seeds produce realistic outcome distribution:
        int numOff = 0, numPocket = 0, numField = 0;
        for (int i = 0; i < 60; ++i) {
            double targetX = 5.0 + (i % 5) * 6.0;  // 5..29 mm
            double targetY = 5.0 + (i / 5) * 2.5;  // 5..32 mm
            double dx = targetX - 300.0, dy = targetY - 300.0;
            double dlen = std::sqrt(dx * dx + dy * dy);
            double speed = 4000.0 + (i % 6) * 100.0; // 4000..4500
            uint32_t seed = 0x12345678u + i * 1013904223u;
            Sim s(ps, 300, 300, (dx / dlen) * speed, (dy / dlen) * speed, 'S', seed);
            s.run(4.0);
            if (s.log.strikerOff) numOff++;
            else if (s.log.strikerPocketed) numPocket++;
            else numField++;
        }
        CHECK(numOff > 0);     // Can fly off table on violent corner impact
        CHECK(numPocket > 0);  // Can fall in pocket on rebound
        CHECK(numField > 0);   // Can return/stay on table
    }
    end();
}

// 5. Resolve rules
static void testResolve() {
    begin("resolve_rules");
    // Valid shot on own light puck at (200,500)
    {
        Game g; clearOwner(g, 0);
        Puck* own = firstOwn(g, 0);
        own->on = true; own->x = 200; own->y = 500;
        for (auto& p : g.pucks) if (p.owner == 1) p.on = false;
        Sim s = g.shoot(200, 920, 0, -2500);
        s.run(5.0);
        ResolveOut o = g.resolve(s);
        CHECK(o.shooter == 0);
        CHECK(!o.penalty);
        CHECK(o.correct);
    }
    // Pocket own puck keeps turn (2 own pucks, pocket 1 into (65,65))
    {
        Game g; clearOwner(g, 0);
        for (auto& p : g.pucks) if (p.owner == 1) p.on = false;
        Puck* a = firstOwn(g, 0); a->on = true; a->x = 65; a->y = 65;
        Puck* b = firstOwn(g, 0); b->on = true; b->x = 500; b->y = 500;
        Sim s = g.shoot(200, 200, -3000, -3000);
        s.run(5.0);
        ResolveOut o = g.resolve(s);
        if (o.ownPot > 0) CHECK(o.keep);
    }
    // pen_oppFirst
    {
        Game g; clearOwner(g, 0);
        Puck* opp = firstOwn(g, 1);
        opp->on = true; opp->x = MID; opp->y = MID + 100.0;
        for (auto& p : g.pucks) if (p.owner == 1 && p.id != opp->id) p.on = false;
        Sim s = g.shoot(MID, C::FIELD - C::RAIL - C::STRIKER_R - 1.0, 0, -3000);
        s.run(5.0);
        ResolveOut o = g.resolve(s);
        CHECK(o.penalty);
        CHECK(hasMsg(o, "pen_oppFirst"));
    }
    // pen_darkDirect + restore
    {
        Game g; clearOwner(g, 0);
        Puck* dark = firstOwn(g, 0);
        dark->on = true; dark->x = MID; dark->y = MID;
        for (auto& p : g.pucks) if (p.id != dark->id) p.on = false;
        Sim s = g.shoot(MID, C::FIELD - C::RAIL - C::STRIKER_R - 1.0, 0, -3000);
        s.run(5.0);
        ResolveOut o = g.resolve(s);
        CHECK(o.penalty);
        CHECK(hasMsg(o, "pen_darkDirect"));
    }
    // 8.1.4 set loss
    {
        Game g; clearOwner(g, 0);
        Puck* own = firstOwn(g, 0);
        own->on = true; own->x = 65; own->y = 65;
        for (auto& p : g.pucks) if (p.id != own->id) p.on = false;
        Sim s = g.shoot(200, 200, -3000, -3000);
        s.run(5.0);
        ResolveOut o = g.resolve(s);
        if (o.ownPot > 0 && countOn(g, 0) == 0) {
            CHECK(o.setResult.has);
            CHECK(o.setResult.winner == 1);
        }
    }
    end();
}

// 6. AI
static void testAI() {
    begin("ai_planner");
    const char* lv[] = {"easy", "medium", "hard"};
    for (auto level : lv) {
        Game g; clearOwner(g, 0);
        ShotResult r = planShotSync(g, level, 12345u);
        CHECK(std::isfinite(r.x) && std::isfinite(r.y));
        CHECK(hyp(r.vx, r.vy) > 0.0);
        AIPlanner p; p.init(g, level, 12345u);
        int guard = 0;
        while (!p.done() && guard++ < 10000) p.step(6);
        CHECK(p.done());
        ShotResult r2 = p.result();
        CHECK(std::isfinite(r2.x) && std::isfinite(r2.y));
        CHECK(hyp(r2.vx, r2.vy) > 0.0);
    }
    end();
}

// 7. Audio
static void testAudio() {
    begin("audio");
    Audio::SoundEngine e;
    e.setEnabled(true);
    CHECK(e.enabled());
    const int N = 512;
    std::vector<float> buf(N * 2, 0.0f);
    e.trigger("hit", 1.0);
    e.mixStereoFloat(buf.data(), N);
    bool any = false;
    for (float s : buf) if (s != 0.0f) { any = true; break; }
    CHECK(any);
    e.stopAll();
    std::vector<int16_t> b16(N * 2, 0);
    e.trigger("cushion", 1.0);
    e.mixStereoS16(b16.data(), N);
    any = false;
    for (int16_t s : b16) if (s != 0) { any = true; break; }
    CHECK(any);
    end();
}

// 8. I18n
static void testI18n() {
    begin("i18n");
    std::string ph = I18n::tr("lv", "placeHint");
    CHECK(!ph.empty());
    CHECK(I18n::tr("lv", "shotControl") == "Sitiena vadība");
    CHECK(I18n::tr("lv", "ctrlBoth") == "Abi (X / R-Stick)");
    CHECK(I18n::tr("en", "shotControl") == "Shot control");
    std::string db = I18n::tr("lv", "debtBadge", {{"n", "3"}});
    CHECK(db.find("3") != std::string::npos);
    CHECK(db.find("{n}") == std::string::npos);
    // Credits in rules
    {
        bool foundCreditsLV = false;
        for (const auto& sec : I18n::rules("lv")) {
            if (sec.body.find("laacz.lv") != std::string::npos &&
                sec.body.find("ciematnieks.lv") != std::string::npos) {
                foundCreditsLV = true;
                break;
            }
        }
        CHECK(foundCreditsLV);

        bool foundCreditsEN = false;
        for (const auto& sec : I18n::rules("en")) {
            if (sec.body.find("laacz.lv") != std::string::npos &&
                sec.body.find("ciematnieks.lv") != std::string::npos) {
                foundCreditsEN = true;
                break;
            }
        }
        CHECK(foundCreditsEN);
    }
    end();
}

// 9. Render
static void testRender() {
    begin("render");
    const char* s = "A\xC4\x81";
    const char* p = s; const char* e = s + std::strlen(s);
    CHECK(Render::nextCodepoint(p, e) == 'A');
    CHECK(Render::nextCodepoint(p, e) == 0x0101);
    CHECK(Render::textWidth("Hello") > 0);
    auto lines = Render::wrapText("one two three four five six seven", 40, 1.0f);
    CHECK(!lines.empty());
    std::vector<uint32_t> fb(64 * 32, 0);
    Render::drawText(fb.data(), 64, 32, 64, 0, 0, "Hi", 0xFFFFFFFF, 1.0f, 0);
    bool any = false;
    for (uint32_t v : fb) if (v != 0) { any = true; break; }
    CHECK(any);
    end();
}

int main() {
    std::printf("=== Novuss tests ===\n");
    testSetup(); testArea(); testPlacement(); testSim();
    testResolve(); testAI(); testAudio(); testI18n(); testRender();
    std::printf("\nChecks: %d  Failures: %d\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
