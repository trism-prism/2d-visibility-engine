#include "test_util.h"

using namespace vis;
using namespace tutil;

namespace {

struct Hit {
    double tMinus = kInf;
    double tPlus = kInf;
};

Hit hit(Vec2 light, Vec2 d, Vec2 a, Vec2 b) {
    Hit h;
    rayHit(makeRay(light, light + d), {a, b}, h.tMinus, h.tPlus);
    return h;
}

}  // namespace

TEST_CASE("rayHit: case table") {
    const Vec2 L{0, 0};
    const Vec2 d{10, 0};

    SUBCASE("collinear with the ray: no hit, whatever the extent") {
        const Hit h = hit(L, d, {5, 0}, {8, 0});
        CHECK(h.tMinus == kInf);
        CHECK(h.tPlus == kInf);
        const Hit h2 = hit(L, d, {-5, 0}, {20, 0});
        CHECK(h2.tMinus == kInf);
        CHECK(h2.tPlus == kInf);
    }
    SUBCASE("both endpoints strictly on one side: no hit") {
        CHECK(hit(L, d, {5, 1}, {9, 3}).tPlus == kInf);
        CHECK(hit(L, d, {5, 1}, {9, 3}).tMinus == kInf);
        CHECK(hit(L, d, {5, -1}, {9, -3}).tPlus == kInf);
        CHECK(hit(L, d, {5, -1}, {9, -3}).tMinus == kInf);
    }
    SUBCASE("proper crossing counts for both sides") {
        const Hit h = hit(L, d, {5, -5}, {5, 5});
        CHECK(h.tMinus == 0.5);
        CHECK(h.tPlus == 0.5);
        const Hit slanted = hit(L, d, {4, -2}, {8, 2});  // crosses y = 0 at x = 6
        CHECK(slanted.tMinus == doctest::Approx(0.6).epsilon(1e-15));
        CHECK(slanted.tPlus == slanted.tMinus);
    }
    SUBCASE("a hit beyond the target is reported with t > 1") {
        const Hit h = hit(L, d, {15, -1}, {15, 1});
        CHECK(h.tMinus == 1.5);
        CHECK(h.tPlus == 1.5);
    }
    SUBCASE("segment behind the light: no hit") {
        const Hit h = hit(L, d, {-5, -5}, {-5, 5});
        CHECK(h.tMinus == kInf);
        CHECK(h.tPlus == kInf);
    }
    SUBCASE("touch-left: endpoint on the ray, other endpoint left of it -> tPlus only") {
        const Hit h = hit(L, d, {5, 0}, {7, 3});
        CHECK(h.tPlus == 0.5);
        CHECK(h.tMinus == kInf);
        const Hit swapped = hit(L, d, {7, 3}, {5, 0});  // endpoint order must not matter
        CHECK(swapped.tPlus == 0.5);
        CHECK(swapped.tMinus == kInf);
    }
    SUBCASE("touch-right: endpoint on the ray, other endpoint right of it -> tMinus only") {
        const Hit h = hit(L, d, {5, 0}, {7, -3});
        CHECK(h.tMinus == 0.5);
        CHECK(h.tPlus == kInf);
        const Hit swapped = hit(L, d, {7, -3}, {5, 0});
        CHECK(swapped.tMinus == 0.5);
        CHECK(swapped.tPlus == kInf);
    }
    SUBCASE("touching endpoint behind the light: no hit") {
        const Hit h = hit(L, d, {-5, 0}, {-7, 3});
        CHECK(h.tMinus == kInf);
        CHECK(h.tPlus == kInf);
    }
    SUBCASE("only lowers the running minimum") {
        Hit h;
        h.tMinus = 0.25;
        h.tPlus = 0.75;
        rayHit(makeRay(L, L + d), {{5, -5}, {5, 5}}, h.tMinus, h.tPlus);  // t = 0.5
        CHECK(h.tMinus == 0.25);
        CHECK(h.tPlus == 0.5);
    }
}

TEST_CASE("rayHit: a ray's own target is hit at exactly t == 1") {
    // Awkward binary fractions: Q - L must round exactly like d = P - L, so t is exactly 1.
    const Vec2 L{0.3, 0.2};
    const Vec2 P{0.1, 0.7};
    const Vec2 d = P - L;
    const Vec2 other{P.x + 0.37, P.y + 0.11};  // other endpoint, left or right of the ray
    const double side = cross(d, other - L);
    REQUIRE(side != 0.0);

    for (int order = 0; order < 2; ++order) {
        const Segment s = order ? Segment{P, other} : Segment{other, P};
        Hit h;
        rayHit(makeRay(L, P), s, h.tMinus, h.tPlus);
        const double t = side > 0 ? h.tPlus : h.tMinus;
        const double untouched = side > 0 ? h.tMinus : h.tPlus;
        CHECK(t == 1.0);
        CHECK(untouched == kInf);
    }
}

TEST_CASE("rayHit: shared endpoint of two walls gives both sides t == 1 exactly") {
    const Vec2 L{12.345678901234567, 3.4567890123456789};
    const Vec2 P{80.11111111111111, 41.22222222222222};
    const Vec2 d = P - L;
    const Vec2 up{P.x + 3.7, P.y + 20.1};
    const Vec2 down{P.x + 5.3, P.y - 17.9};
    Hit h;
    rayHit(makeRay(L, P), {P, up}, h.tMinus, h.tPlus);
    rayHit(makeRay(L, P), {down, P}, h.tMinus, h.tPlus);
    CHECK(h.tMinus == 1.0);
    CHECK(h.tPlus == 1.0);
}

TEST_CASE("rayHit: kLightClearance") {
    const Vec2 d{10, 0};
    SUBCASE("light on the interior of a crossing wall: that wall does not occlude") {
        const Hit h = hit({5, 0}, d, {5, -5}, {5, 5});  // wall passes through the light, t = 0
        CHECK(h.tMinus == kInf);
        CHECK(h.tPlus == kInf);
    }
    SUBCASE("light on a wall endpoint: that wall does not occlude") {
        const Hit h = hit({5, 0}, d, {5, 0}, {5, 5});
        CHECK(h.tMinus == kInf);
        CHECK(h.tPlus == kInf);
    }
    SUBCASE("threshold is measured along the ray in world units, t * |d|") {
        // |d| = 10: a wall at distance 0.5e-9 is ignored, at 2e-9 it counts.
        const Hit near = hit({0, 0}, d, {0.5e-9, -1}, {0.5e-9, 1});
        CHECK(near.tMinus == kInf);
        const Hit far = hit({0, 0}, d, {2e-9, -1}, {2e-9, 1});
        CHECK(far.tMinus == doctest::Approx(2e-10).epsilon(1e-9));
        CHECK(far.tPlus == far.tMinus);
        // same distances with a much longer d: the unit is world units, not t
        const Hit nearLong = hit({0, 0}, {1000, 0}, {0.5e-9, -1}, {0.5e-9, 1});
        CHECK(nearLong.tMinus == kInf);
    }
}

TEST_CASE("intersectSegments classification") {
    Vec2 p;
    CHECK(intersectSegments({{0, 0}, {10, 10}}, {{0, 10}, {10, 0}}, p) == SegHit::Cross);
    CHECK(p.x == doctest::Approx(5.0));
    CHECK(p.y == doctest::Approx(5.0));
    // T-junction: endpoint of one on the interior of the other -> that endpoint, no new point
    CHECK(intersectSegments({{0, 5}, {10, 5}}, {{4, 5}, {4, 9}}, p) == SegHit::Touch);
    CHECK(p == Vec2{4, 5});
    CHECK(intersectSegments({{4, 5}, {4, 9}}, {{0, 5}, {10, 5}}, p) == SegHit::Touch);
    CHECK(p == Vec2{4, 5});
    // shared endpoint
    CHECK(intersectSegments({{0, 0}, {5, 5}}, {{5, 5}, {9, 1}}, p) == SegHit::Touch);
    CHECK(p == Vec2{5, 5});
    // parallel, collinear overlap, disjoint
    CHECK(intersectSegments({{0, 0}, {10, 0}}, {{5, 0}, {15, 0}}, p) == SegHit::None);
    CHECK(intersectSegments({{0, 0}, {10, 0}}, {{0, 1}, {10, 1}}, p) == SegHit::None);
    CHECK(intersectSegments({{0, 0}, {1, 1}}, {{3, 0}, {3, 5}}, p) == SegHit::None);
    // would intersect as infinite lines only
    CHECK(intersectSegments({{0, 0}, {1, 0}}, {{5, -1}, {5, 1}}, p) == SegHit::None);
}

TEST_CASE("intersectSegments agrees with plain parametric algebra on generic segments") {
    Rng rng(0xC0FFEE);
    int crosses = 0;
    for (int i = 0; i < scaled(20000); ++i) {
        const Segment s{{rng.range(0, 10), rng.range(0, 10)}, {rng.range(0, 10), rng.range(0, 10)}};
        const Segment t{{rng.range(0, 10), rng.range(0, 10)}, {rng.range(0, 10), rng.range(0, 10)}};
        const Vec2 r = s.b - s.a;
        const Vec2 u = t.b - t.a;
        const double den = r.x * u.y - r.y * u.x;
        const Vec2 w = t.a - s.a;
        const double ts = (w.x * u.y - w.y * u.x) / den;
        const double us = (w.x * r.y - w.y * r.x) / den;
        // generic random doubles are never within rounding of a degenerate case at this scale
        const bool expected = ts > 0 && ts < 1 && us > 0 && us < 1;
        Vec2 p;
        const bool got = intersectSegments(s, t, p) == SegHit::Cross;
        CHECK(got == expected);
        if (got && expected) {
            ++crosses;
            CHECK(p.x == doctest::Approx(s.a.x + ts * r.x).epsilon(1e-9));
            CHECK(p.y == doctest::Approx(s.a.y + ts * r.y).epsilon(1e-9));
        }
    }
    CHECK(crosses > scaled(1000));
}

TEST_CASE("segmentOverlapsAABB is inclusive") {
    CHECK(segmentOverlapsAABB({{0, 0}, {10, 10}}, 2, 2, 4, 4));        // passes through
    CHECK(segmentOverlapsAABB({{0, 3}, {10, 3}}, 2, 2, 4, 4));         // crosses horizontally
    CHECK(segmentOverlapsAABB({{3, 3}, {3.5, 3.5}}, 2, 2, 4, 4));      // fully inside
    CHECK(segmentOverlapsAABB({{4, 0}, {4, 10}}, 2, 2, 4, 4));         // along the right edge
    CHECK(segmentOverlapsAABB({{4, 4}, {8, 8}}, 2, 2, 4, 4));          // touches the corner only
    CHECK(segmentOverlapsAABB({{0, 6}, {6, 0}}, 2, 2, 4, 4));          // x+y=6 touches corners (2,4) and (4,2)
    CHECK(segmentOverlapsAABB({{0, 7}, {7, 0}}, 2, 2, 4, 4));          // x+y=7 clips the box between (3,4) and (4,3)
    CHECK_FALSE(segmentOverlapsAABB({{0, 9}, {9, 0}}, 2, 2, 4, 4));    // x+y=9 > 8 misses it
    CHECK_FALSE(segmentOverlapsAABB({{5, 0}, {5, 10}}, 2, 2, 4, 4));
    CHECK_FALSE(segmentOverlapsAABB({{0, 0}, {1, 1}}, 2, 2, 4, 4));    // stops short
}

TEST_CASE("clipToRect snaps clipped ends exactly onto the boundary") {
    Segment out;
    SUBCASE("crossing one edge") {
        REQUIRE(clipToRect({{-10, 50}, {50, 50}}, 100, 80, out));
        CHECK(out.a == Vec2{0, 50});
        CHECK(out.b == Vec2{50, 50});
    }
    SUBCASE("crossing two edges, slanted: the clipped coordinate is exact, the other is computed") {
        REQUIRE(clipToRect({{-10, -5}, {130, 135}}, 100, 80, out));  // y = x + 5
        CHECK(out.a.x == 0.0);
        CHECK(out.a.y == doctest::Approx(5.0));
        CHECK(out.b.y == 80.0);
        CHECK(out.b.x == doctest::Approx(75.0));
    }
    SUBCASE("passing through the whole world") {
        REQUIRE(clipToRect({{-5, 40}, {105, 40}}, 100, 80, out));
        CHECK(out.a == Vec2{0, 40});
        CHECK(out.b == Vec2{100, 40});
    }
    SUBCASE("inside is untouched, bit for bit") {
        const Segment in{{12.3456789, 4.5}, {77.7, 66.6}};
        REQUIRE(clipToRect(in, 100, 80, out));
        CHECK(out == in);
    }
    SUBCASE("entirely outside") {
        CHECK_FALSE(clipToRect({{-10, 5}, {-1, 50}}, 100, 80, out));
        CHECK_FALSE(clipToRect({{110, 5}, {120, 50}}, 100, 80, out));
        CHECK_FALSE(clipToRect({{-10, 90}, {110, 95}}, 100, 80, out));
    }
}

TEST_CASE("Scene::addWall validation and clipping") {
    Scene s(100, 80);
    REQUIRE(s.wallCount() == 4);
    const double nan = std::nan("");

    SUBCASE("non-finite coordinates are rejected") {
        CHECK(s.addWall({nan, 1}, {5, 5}) == AddStatus::RejectedNonFinite);
        CHECK(s.addWall({1, 1}, {kInf, 5}) == AddStatus::RejectedNonFinite);
        CHECK(s.addWall({1, -kInf}, {5, 5}) == AddStatus::RejectedNonFinite);
        CHECK(s.wallCount() == 4);
    }
    SUBCASE("entirely outside the world is rejected") {
        CHECK(s.addWall({-10, 5}, {-1, 50}) == AddStatus::RejectedOutside);
        CHECK(s.addWall({101, 5}, {200, 5}) == AddStatus::RejectedOutside);
        CHECK(s.wallCount() == 4);
    }
    SUBCASE("a wall crossing the boundary is clipped, not rejected") {
        REQUIRE(s.addWall({-10, 50}, {50, 50}) == AddStatus::Added);
        CHECK(s.walls().back() == Segment{{0, 50}, {50, 50}});
        REQUIRE(s.addWall({60, 20}, {160, 20}) == AddStatus::Added);
        CHECK(s.walls().back() == Segment{{60, 20}, {100, 20}});
        CHECK(s.wallCount() == 6);
    }
    SUBCASE("zero-length and too-short walls are rejected") {
        CHECK(s.addWall({5, 5}, {5, 5}) == AddStatus::RejectedTooShort);
        CHECK(s.addWall({5, 5}, {5 + 5e-7, 5}) == AddStatus::RejectedTooShort);
        CHECK(s.addWall({5, 5}, {5 + 2e-6, 5}) == AddStatus::Added);
    }
    SUBCASE("clipped length decides, not the input length") {
        // 1000 units long on input, but only 3e-7 of it is inside the world
        CHECK(s.addWall({100 - 3e-7, 40}, {1100, 40}) == AddStatus::RejectedTooShort);
        // touches the world at a single point
        CHECK(s.addWall({-10, -10}, {0, 0}) == AddStatus::RejectedTooShort);
    }
    SUBCASE("a wall lying on the boundary is accepted") {
        CHECK(s.addWall({10, 0}, {60, 0}) == AddStatus::Added);
    }
}

TEST_CASE("Scene mutators: boundary is permanent, bulk load, clear") {
    Scene s(100, 80);
    CHECK(s.walls()[0] == Segment{{0, 0}, {100, 0}});
    CHECK(s.walls()[1] == Segment{{100, 0}, {100, 80}});
    CHECK(s.walls()[2] == Segment{{100, 80}, {0, 80}});
    CHECK(s.walls()[3] == Segment{{0, 80}, {0, 0}});
    for (std::size_t i = 0; i < 4; ++i) CHECK_FALSE(s.removeWall(i));
    CHECK_FALSE(s.removeWall(99));

    REQUIRE(s.addWall({10, 10}, {20, 20}) == AddStatus::Added);
    REQUIRE(s.addWall({10, 20}, {20, 10}) == AddStatus::Added);
    CHECK(s.userWallCount() == 2);
    CHECK(s.intersections().size() == 1);
    CHECK(s.removeWall(4));
    CHECK(s.intersections().empty());
    CHECK(s.userWallCount() == 1);

    const std::vector<Segment> bulk{{{1, 1}, {2, 2}}, {{-5, -5}, {-1, -1}}, {{3, 3}, {3, 3}}, {{5, 5}, {9, 5}}};
    const std::uint64_t before = s.revision();
    CHECK(s.setWalls(bulk) == 2);  // one outside, one zero-length
    CHECK(s.revision() == before + 1);  // single rebuild
    CHECK(s.userWallCount() == 2);
    s.clear();
    CHECK(s.wallCount() == 4);
    CHECK(s.targets().size() == 4);  // only the corners
}

TEST_CASE("Rng and generators are deterministic") {
    std::uint64_t state = 0;
    CHECK(splitmix64(state) == 0xE220A8397B1DCDAFull);  // published splitmix64 reference value for seed 0
    CHECK(splitmix64(state) == 0x6E789E6AA1B965F4ull);

    Rng a(42), b(42), c(43);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const auto va = a.next();
        CHECK(va == b.next());
        differs |= va != c.next();
    }
    CHECK(differs);

    for (SceneKind k : {SceneKind::Scatter, SceneKind::Maze, SceneKind::LongWalls, SceneKind::Cluster}) {
        const Scene s1 = makeScene(k, 300, 7);
        const Scene s2 = makeScene(k, 300, 7);
        const Scene s3 = makeScene(k, 300, 8);
        CHECK(s1.walls() == s2.walls());
        CHECK(s1.walls() != s3.walls());
        CHECK(s1.userWallCount() == 300);
    }
    CHECK(makeDemoScene().userWallCount() >= 40);
    CHECK(makeDemoScene().userWallCount() <= 80);
}

TEST_CASE("generators: long-walls crossing count stays far below n^2/4 at n = 1000") {
    for (std::uint64_t seed : {1u, 2u, 3u}) {
        const Scene s = makeLongWallsScene(1000, seed);
        const double k = static_cast<double>(s.intersections().size());
        INFO("seed " << seed << " k = " << k);
        CHECK(k >= 10.0 * 1000);
        CHECK(k <= 30.0 * 1000);
    }
}
