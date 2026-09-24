// Analytic polygons. Every expected vertex list and area below was worked out by hand (similar
// triangles and the shoelace formula) before running the code; the derivation is in the comment
// of each case. World is 100 x 100 unless stated.
#include "test_util.h"

using namespace vis;
using namespace tutil;

namespace {

constexpr double kCoordEps = 1e-9;  // rounding of L + t*d is ~1e-14 at this scale
constexpr double kAreaEps = 1e-8;

// Checks both modes: counter-clockwise, simplified polygon equals `expected` (cyclic), area matches.
void expectPolygon(const Scene& scene, Vec2 light, const Poly& expected, double area) {
    for (Mode mode : {Mode::Brute, Mode::Grid}) {
        const Poly raw = query(scene, light, mode);
        INFO("mode " << std::string(mode == Mode::Brute ? "Brute" : "Grid") << " light " << str(light) << "\n raw: "
                     << describe(raw));
        REQUIRE(raw.size() >= 3);
        CHECK(signedArea(raw) > 0.0);  // counter-clockwise
        CHECK(signedArea(raw) == doctest::Approx(area).epsilon(1e-12).scale(1.0));
        const Poly simple = simplify(raw, kCoordEps);
        INFO("simplified: " << describe(simple) << "\n expected:   " << describe(expected));
        CHECK(sameCycle(simple, expected, kCoordEps));
        CHECK(std::fabs(signedArea(simple) - area) < kAreaEps);
    }
}

const Poly kRect{{0, 0}, {100, 0}, {100, 100}, {0, 100}};

}  // namespace

TEST_CASE("empty world: the world rectangle") {
    const Scene s = makeScene(100, 100, {});
    expectPolygon(s, {30, 40}, kRect, 10000.0);
    expectPolygon(s, {99.5, 0.5}, kRect, 10000.0);
}

TEST_CASE("empty world, non-square: area W*H") {
    const Scene s = makeScene(160, 90, {});
    expectPolygon(s, {10, 80}, {{0, 0}, {160, 0}, {160, 90}, {0, 90}}, 14400.0);
}

TEST_CASE("single wall: known shadow area") {
    // L=(50,20), wall (40,50)-(60,50). Rays through the ends: d=(-10,30) and (10,30) reach y=100 at
    // t=8/3, x = 50 -/+ 80/3 = 70/3 and 230/3. Shadow = trapezoid y 50..100, widths 20 and 160/3:
    // (20 + 160/3)/2 * 50 = 5500/3. Visible = 10000 - 5500/3 = 24500/3.
    const Scene s = makeScene(100, 100, {{{40, 50}, {60, 50}}});
    expectPolygon(s, {50, 20},
                  {{0, 0}, {100, 0}, {100, 100}, {230.0 / 3, 100}, {60, 50}, {40, 50}, {70.0 / 3, 100}, {0, 100}},
                  24500.0 / 3);
}

TEST_CASE("two walls sharing a corner: no light leaks through it") {
    // L=(20,50); walls (50,50)-(70,30) and (50,50)-(70,70), a '<' with its apex towards the light.
    // Rays through (70,30)/(70,70): d=(50,-/+20) reach x=100 at t=1.6, y = 18 and 82. Shoelace of the
    // visible polygon gives 16080/2 = 8040 (shadow 1960).
    const Scene s = makeScene(100, 100, {{{50, 50}, {70, 30}}, {{50, 50}, {70, 70}}});
    expectPolygon(s, {20, 50},
                  {{0, 0}, {100, 0}, {100, 18}, {70, 30}, {50, 50}, {70, 70}, {100, 82}, {100, 100}, {0, 100}},
                  8040.0);
    for (Mode mode : {Mode::Brute, Mode::Grid}) {
        const Poly p = query(s, {20, 50}, mode);
        CHECK_FALSE(pointInPolygon(p, {55, 50}));  // inside the '<', behind the apex
        CHECK_FALSE(pointInPolygon(p, {60, 50}));
        CHECK_FALSE(pointInPolygon(p, {90, 50}));
        CHECK(pointInPolygon(p, {40, 50}));
    }
}

TEST_CASE("T-junction, light below the bar: the stem is hidden") {
    // Bar (20,50)-(80,50), stem (50,50)-(50,80), L=(30,20). Through (80,50): d=(50,30) hits x=100 at
    // t=1.4, y=62. Through (20,50): d=(-10,30) hits y=100 at t=8/3, x=10/3. Shoelace: 5703.33.
    const Scene s = makeScene(100, 100, {{{20, 50}, {80, 50}}, {{50, 50}, {50, 80}}});
    expectPolygon(s, {30, 20}, {{0, 0}, {100, 0}, {100, 62}, {80, 50}, {20, 50}, {10.0 / 3, 100}, {0, 100}},
                  17110.0 / 3);
}

TEST_CASE("T-junction, light beside the stem: stem and bar both visible") {
    // Same walls, L=(70,70). Through the stem top (50,80): d=(-20,10) reaches y=100 at x=10. The ray
    // through the foot (50,50) is hit by stem and bar at once (t=1, no gap). Through the bar end (80,50):
    // d=(10,-20) reaches x=100 at y=10. Shoelace of (50,50),(80,50),(100,10),(100,100),(10,100),(50,80)
    // = 6600/2 = 3300.
    const Scene s = makeScene(100, 100, {{{20, 50}, {80, 50}}, {{50, 50}, {50, 80}}});
    expectPolygon(s, {70, 70}, {{50, 50}, {80, 50}, {100, 10}, {100, 100}, {10, 100}, {50, 80}}, 3300.0);
}

TEST_CASE("X crossing: the crossing point is a vertex") {
    // Walls (30,30)-(70,70) and (30,70)-(70,30) cross at (50,50); L=(50,10) sees the lower V. Through
    // (30,30) and (70,30): d=(-/+20,20) reach x=0 / x=100 at t=2.5, y=60. Shoelace = 8600/2 = 4300.
    const Scene s = makeScene(100, 100, {{{30, 30}, {70, 70}}, {{30, 70}, {70, 30}}});
    REQUIRE(s.intersections().size() == 1);
    CHECK(s.intersections()[0].x == doctest::Approx(50.0));
    CHECK(s.intersections()[0].y == doctest::Approx(50.0));
    expectPolygon(s, {50, 10}, {{100, 0}, {100, 60}, {70, 30}, {50, 50}, {30, 30}, {0, 60}, {0, 0}}, 4300.0);
}

TEST_CASE("collinear overlapping walls occlude as their union") {
    // Walls (30,50)-(60,50) and (45,50)-(75,50), L=(50,20); union (30,50)-(75,50). Through (75,50):
    // d=(25,30) reaches x=100 at t=2, y=80. Through (30,50): d=(-20,30) reaches x=0 at t=2.5, y=95.
    // Shoelace of (0,0),(100,0),(100,80),(75,50),(30,50),(0,95) = 12100/2 = 6050.
    const Scene s = makeScene(100, 100, {{{30, 50}, {60, 50}}, {{45, 50}, {75, 50}}});
    expectPolygon(s, {50, 20}, {{0, 0}, {100, 0}, {100, 80}, {75, 50}, {30, 50}, {0, 95}}, 6050.0);
    // Same union, but the overlap is exercised from the other order and from above the walls.
    const Scene r = makeScene(100, 100, {{{45, 50}, {75, 50}}, {{30, 50}, {60, 50}}});
    expectPolygon(r, {50, 20}, {{0, 0}, {100, 0}, {100, 80}, {75, 50}, {30, 50}, {0, 95}}, 6050.0);
}

TEST_CASE("light exactly on a wall interior: it shines to both sides") {
    // The wall is seen end-on or sits at distance zero, so it occludes nothing: full rectangle.
    expectPolygon(makeScene(100, 100, {{{30, 50}, {70, 50}}}), {50, 50}, kRect, 10000.0);
    expectPolygon(makeScene(100, 100, {{{50, 20}, {50, 80}}}), {50, 50}, kRect, 10000.0);
    expectPolygon(makeScene(100, 100, {{{30, 30}, {70, 70}}}), {50, 50}, kRect, 10000.0);
}

TEST_CASE("light exactly on a wall endpoint") {
    expectPolygon(makeScene(100, 100, {{{50, 50}, {80, 50}}}), {50, 50}, kRect, 10000.0);
    expectPolygon(makeScene(100, 100, {{{50, 50}, {80, 90}}}), {50, 50}, kRect, 10000.0);
    expectPolygon(makeScene(100, 100, {{{20, 70}, {50, 50}}}), {50, 50}, kRect, 10000.0);
}

TEST_CASE("light on a world corner") {
    const Scene empty = makeScene(100, 100, {});
    for (Vec2 c : {Vec2{0, 0}, Vec2{100, 0}, Vec2{100, 100}, Vec2{0, 100}}) expectPolygon(empty, c, kRect, 10000.0);

    // L=(0,0), wall (60,20)-(60,80). Through (60,20): d=(60,20) reaches x=100 at y=100/3. Through
    // (60,80): d=(60,80) reaches y=100 at t=1.25, x=75. Shoelace of
    // (0,0),(100,0),(100,100/3),(60,20),(60,80),(75,100),(0,100) = 43300/3 /2 = 21650/3.
    const Scene s = makeScene(100, 100, {{{60, 20}, {60, 80}}});
    expectPolygon(s, {0, 0}, {{0, 0}, {100, 0}, {100, 100.0 / 3}, {60, 20}, {60, 80}, {75, 100}, {0, 100}},
                  21650.0 / 3);
}

TEST_CASE("light on the world boundary") {
    // L=(50,0), wall (35,40)-(65,40). Through the ends d=(-/+15,40) reach y=100 at t=2.5: x = 12.5, 87.5.
    // Shadow trapezoid widths 30 and 75 over 60 = 3150, visible 6850. (L itself is a collinear vertex.)
    const Scene bottom = makeScene(100, 100, {{{35, 40}, {65, 40}}});
    expectPolygon(bottom, {50, 0},
                  {{0, 0}, {100, 0}, {100, 100}, {87.5, 100}, {65, 40}, {35, 40}, {12.5, 100}, {0, 100}}, 6850.0);
    // Mirror image on the left edge: L=(0,50), wall (40,35)-(40,65).
    const Scene left = makeScene(100, 100, {{{40, 35}, {40, 65}}});
    expectPolygon(left, {0, 50},
                  {{0, 0}, {100, 0}, {100, 12.5}, {40, 35}, {40, 65}, {100, 87.5}, {100, 100}, {0, 100}}, 6850.0);
    expectPolygon(makeScene(100, 100, {}), {37.5, 100}, kRect, 10000.0);
}

TEST_CASE("angular wraparound: walls straddling the -pi/pi direction") {
    // L=(50,50): the direction (-1,0) has key +pi, the last in the sorted order.
    // Wall ending exactly on that direction, extending upwards: (20,50)-(20,70). Through (20,70):
    // d=(-30,20) reaches x=0 at t=5/3, y=250/3. Shadow trapezoid widths 20 and 100/3 over 20 = 1600/3.
    expectPolygon(makeScene(100, 100, {{{20, 50}, {20, 70}}}), {50, 50},
                  {{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 250.0 / 3}, {20, 70}, {20, 50}, {0, 50}},
                  10000.0 - 1600.0 / 3);
    // Downwards: (20,30)-(20,50). Through (20,30): reaches x=0 at y=50/3.
    expectPolygon(makeScene(100, 100, {{{20, 30}, {20, 50}}}), {50, 50},
                  {{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 50}, {20, 50}, {20, 30}, {0, 50.0 / 3}},
                  10000.0 - 1600.0 / 3);
    // Straddling it: (20,30)-(20,70). Shadow widths 40 and 200/3 over 20 = 3200/3.
    expectPolygon(makeScene(100, 100, {{{20, 30}, {20, 70}}}), {50, 50},
                  {{0, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 250.0 / 3}, {20, 70}, {20, 30}, {0, 50.0 / 3}},
                  10000.0 - 3200.0 / 3);
}

TEST_CASE("polygons are counter-clockwise and bracket the light") {
    const Scene demo = makeDemoScene();
    for (Mode mode : {Mode::Brute, Mode::Grid}) {
        const Poly p = query(demo, demoLight(), mode);
        CHECK(signedArea(p) > 0.0);
        CHECK(pointInPolygon(p, demoLight() + Vec2{0.5, 0.5}));
        for (Vec2 v : p) {
            CHECK(v.x >= -1e-9);
            CHECK(v.x <= demo.width() + 1e-9);
            CHECK(v.y >= -1e-9);
            CHECK(v.y <= demo.height() + 1e-9);
        }
    }
}

TEST_CASE("non-finite light gives an empty polygon; out-of-world light is clamped") {
    const Scene s = makeScene(100, 100, {});
    VisibilityResult r;
    computeVisibility(s, {std::nan(""), 5}, Mode::Grid, r);
    CHECK(r.polygon.empty());
    computeVisibility(s, {-50, 500}, Mode::Grid, r);
    CHECK(r.light == Vec2{0, 100});
    CHECK(signedArea(r.polygon) == doctest::Approx(10000.0).epsilon(1e-12));
}

// Regression for the atan2-key revision of the contract. L=(0.7,0.3), the wall end (0.6,0.4) and the
// corner (0,1) are collinear in decimal but not in binary. Sorting by atan2 gave both targets the
// same key, the run collapsed to one ray, and the wall end was classified by a rounded cross product:
// the corner (0,1) vanished (area 0.465). Hand-computed: shoelace of
// (0,0),(1,0),(1,1),(0,1),(0.6,0.4),(0.5,0.3),(0,0.3) = 1.53 / 2 = 0.765.
TEST_CASE("regression: targets collinear with the light in decimal but not in binary") {
    const Scene s = makeScene(1.0, 1.0, {{{0.6, 0.4}, {0.5, 0.3}}});
    expectPolygon(s, {0.7, 0.3}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0.6, 0.4}, {0.5, 0.3}, {0, 0.3}}, 0.765);
}
