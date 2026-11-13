// Independent oracles for the visibility polygon (DESIGN section 7, check 3).
//  1. Membership: point-in-polygon of the result must equal "the open segment light->q properly
//     crosses no wall", a predicate written with orientation tests only (tutil::litByDefinition).
//  2. Area: the polygon area must match 1/2 * sum r(theta)^2 d(theta) over densely sampled
//     directions, with r from plain parametric ray/segment algebra (tutil::radialDistance).
// Neither oracle calls rayHit, the grid, or intersectSegments.
#include <functional>

#include "test_util.h"

using namespace vis;
using namespace tutil;

namespace {

struct OracleCase {
    std::string label;
    Scene scene;
    std::vector<Vec2> lights;
};

Vec2 randomLight(Rng& rng, const Scene& s) { return {rng.range(0, s.width()), rng.range(0, s.height())}; }

// Lights on walls (endpoint; interior midpoint only where it is exactly on the wall in double
// arithmetic, i.e. integer-coordinate walls) and on the boundary, plus random ones.
std::vector<Vec2> lightsFor(Rng& rng, const Scene& s, int random, bool exactMidpoint) {
    std::vector<Vec2> lights;
    for (int i = 0; i < random; ++i) lights.push_back(randomLight(rng, s));
    if (s.userWallCount() > 0) {
        const Segment& w = s.walls()[Scene::kBoundaryWalls + static_cast<std::size_t>(rng.below(static_cast<int>(s.userWallCount())))];
        if (exactMidpoint) lights.push_back({(w.a.x + w.b.x) / 2, (w.a.y + w.b.y) / 2});
        lights.push_back(w.a);
    }
    lights.push_back({s.width() * 0.5, 0.0});
    lights.push_back({0.0, 0.0});
    return lights;
}

std::vector<OracleCase> makeCases(std::uint64_t baseSeed, int count) {
    std::vector<OracleCase> cases;
    for (int i = 0; i < count; ++i) {
        const std::uint64_t seed = baseSeed + static_cast<std::uint64_t>(i);
        Rng rng(seed);
        const int kind = i % 5;
        const int n = 1 + rng.below(24);
        if (kind == 0) {
            cases.push_back({"lattice", makeScene(64, 64, latticeWalls(rng, n, 64, false)), {}});
        } else if (kind == 1) {
            cases.push_back({"lattice-axis", makeScene(64, 64, latticeWalls(rng, n + 6, 16, true)), {}});
        } else if (kind == 2) {
            cases.push_back({"real", makeScene(100, 70, realWalls(rng, n, 100, 70, 60)), {}});
        } else if (kind == 3) {
            cases.push_back({"scatter", makeScatterScene(30 + n, seed), {}});
        } else {
            cases.push_back({"maze", makeMazeScene(30 + n, seed), {}});
        }
        cases.back().label += " seed " + std::to_string(seed);
        // independent stream: the generators seed their own Rng with `seed`, so reusing it would put
        // the "random" lights exactly on generated wall midpoints
        Rng lightRng(seed ^ 0x9E3779B97F4A7C15ull);
        cases.back().lights = lightsFor(lightRng, cases.back().scene, 3, kind <= 1);
    }
    return cases;
}

}  // namespace

TEST_CASE("oracle: polygon membership equals the segment-crossing predicate") {
    // Points within kBand of the polygon boundary are skipped. Polygon vertices carry rounding of
    // about 1e-13 (L + t*d with scene extents <= 1.6e3), and the orientation predicate can only flip
    // for q within about that distance of a shadow line. 1e-6 is seven orders above both.
    constexpr double kBand = 1e-6;
    long long compared = 0, skipped = 0, litTotal = 0;
    for (const OracleCase& c : makeCases(0x5EED0000, scaled(120))) {
        Rng rng(0xABCDEF ^ static_cast<std::uint64_t>(c.scene.wallCount()));
        for (Vec2 light : c.lights) {
            for (Mode mode : {Mode::Brute, Mode::Grid}) {
                const Poly poly = query(c.scene, light, mode);
                REQUIRE(poly.size() >= 3);
                long long mismatches = 0;
                Vec2 firstBad{};
                for (int k = 0; k < scaled(250); ++k) {
                    const Vec2 q{rng.range(0, c.scene.width()), rng.range(0, c.scene.height())};
                    if (distToBoundary(poly, q) < kBand) { ++skipped; continue; }
                    const bool lit = litByDefinition(c.scene, light, q);
                    const bool in = pointInPolygon(poly, q);
                    ++compared;
                    litTotal += lit;
                    if (lit != in && mismatches++ == 0) firstBad = q;
                }
                INFO(c.label << " light " << str(light) << " mode " << std::string(mode == Mode::Brute ? "Brute" : "Grid")
                             << " first mismatching q " << str(firstBad));
                CHECK(mismatches == 0);
            }
        }
    }
    // guard against a vacuous test: plenty of points, and both lit and shadowed ones
    CHECK(compared > scaled(scaled(50000)));  // scenes and samples are both scaled
    CHECK(litTotal > compared / 10);
    CHECK(litTotal < compared - compared / 20);
    CHECK(skipped < compared / 50);
}

TEST_CASE("oracle: polygon area equals dense angular sampling") {
    // Midpoint rule on r(theta)^2 / 2. r jumps at silhouette directions (at most one jump per polygon
    // vertex), each costing at most 1/2 * Rmax^2 * dTheta (the midpoint can land on the wrong side by
    // less than one step); between jumps r^2 is smooth and its error is orders smaller. So
    //   |area - sampled| <= (V + 2) * 1/2 * Rmax^2 * dTheta,  Rmax = world diagonal.
    const int kSamples = scaled(200000);
    const double dTheta = 2.0 * 3.14159265358979323846 / kSamples;
    double worstRatio = 0.0;
    for (const OracleCase& c : makeCases(0xA5EA0000, scaled(40))) {
        const double rmax2 = c.scene.width() * c.scene.width() + c.scene.height() * c.scene.height();
        for (Vec2 light : c.lights) {
            const Poly poly = query(c.scene, light, Mode::Grid);
            double sampled = 0.0;
            for (int i = 0; i < kSamples; ++i) {
                const double th = (i + 0.5) * dTheta;
                const double r = radialDistance(c.scene, light, std::cos(th), std::sin(th));
                sampled += 0.5 * r * r * dTheta;
            }
            const double tol = (static_cast<double>(poly.size()) + 2.0) * 0.5 * rmax2 * dTheta;
            const double err = std::fabs(signedArea(poly) - sampled);
            INFO(c.label << " light " << str(light) << " V=" << poly.size() << " area " << signedArea(poly)
                         << " sampled " << sampled << " tol " << tol);
            CHECK(err <= tol);
            worstRatio = std::max(worstRatio, err / tol);
        }
    }
    MESSAGE("worst |error| / tolerance = " << worstRatio);
}

// Decimal / non-dyadic lattices: wall ends, lights and many targets that are collinear in decimal but
// not in binary. This is the family that broke the atan2 + rounded-cross revision of the contract.
// Lights sit on lattice lines (a node, or a node coordinate on one axis); lights lying inside a wall
// are skipped because the independent predicate uses rounded orientation for "light on the wall".
TEST_CASE("oracle: decimal and non-dyadic lattices (grid snap 0.1, spacing 1600/7)") {
    constexpr double kBand = 1e-6;
    struct Family {
        const char* name;
        double w, h;
        std::function<Vec2(int, int)> node;
        int nx, ny;
    };
    const std::vector<Family> families = {
        {"unit world, 0.1 snap", 1.0, 1.0, [](int i, int j) { return Vec2{i / 10.0, j / 10.0}; }, 10, 10},
        {"100 world, 0.1 snap", 100.0, 100.0, [](int i, int j) { return Vec2{i * 0.1, j * 0.1}; }, 1000, 1000},
        {"1600x1000, spacing 1/7", 1600.0, 1000.0, [](int i, int j) { return Vec2{i * (1600.0 / 7), j * (1000.0 / 7)}; }, 7, 7},
        {"1600x1000, spacing 1/9", 1600.0, 1000.0, [](int i, int j) { return Vec2{i * (1600.0 / 9), j * (1000.0 / 9)}; }, 9, 9},
    };
    long long compared = 0, lightsUsed = 0;
    int caseIndex = 0;
    for (const Family& f : families) {
        for (int c = 0; c < scaled(60); ++c, ++caseIndex) {
            const std::uint64_t seed = 0xDEC10000 + static_cast<std::uint64_t>(caseIndex);
            Rng rng(seed);
            std::vector<Segment> walls;
            const int n = 2 + rng.below(9);
            for (int k = 0; k < n; ++k) {
                const Vec2 a = f.node(rng.below(f.nx + 1), rng.below(f.ny + 1));
                // short hops keep many walls aligned with each other and with the lattice lines
                const Vec2 b = f.node(std::clamp(static_cast<int>(a.x / f.node(1, 0).x) + rng.below(5) - 2, 0, f.nx),
                                      std::clamp(static_cast<int>(a.y / f.node(0, 1).y) + rng.below(5) - 2, 0, f.ny));
                walls.push_back({a, rng.below(3) == 0 ? f.node(rng.below(f.nx + 1), rng.below(f.ny + 1)) : b});
            }
            const Scene scene = makeScene(f.w, f.h, walls);
            Rng qr(seed ^ 0x9E3779B97F4A7C15ull);
            std::vector<Vec2> lights;
            for (int k = 0; k < 6; ++k) lights.push_back(f.node(qr.below(f.nx + 1), qr.below(f.ny + 1)));
            for (int k = 0; k < 3; ++k) {
                lights.push_back({f.node(qr.below(f.nx + 1), 0).x, qr.range(0, f.h)});
                lights.push_back({qr.range(0, f.w), f.node(0, qr.below(f.ny + 1)).y});
            }
            for (Vec2 light : lights) {
                bool insideWall = false;
                for (std::size_t w = Scene::kBoundaryWalls; w < scene.walls().size(); ++w) {
                    const Segment& s = scene.walls()[w];
                    if (light != s.a && light != s.b && distToSegment(light, s.a, s.b) < 1e-9 * std::max(f.w, f.h)) insideWall = true;
                }
                if (insideWall) continue;
                ++lightsUsed;
                for (Mode mode : {Mode::Brute, Mode::Grid}) {
                    const Poly poly = query(scene, light, mode);
                    REQUIRE(poly.size() >= 3);
                    long long mismatches = 0;
                    Vec2 firstBad{};
                    for (int k = 0; k < scaled(300); ++k) {
                        const Vec2 q{qr.range(0, f.w), qr.range(0, f.h)};
                        if (distToBoundary(poly, q) < kBand * std::max(f.w, f.h) * 1e-3) continue;
                        const bool lit = litByDefinition(scene, light, q);
                        const bool in = pointInPolygon(poly, q);
                        ++compared;
                        if (lit != in && mismatches++ == 0) firstBad = q;
                    }
                    INFO(f.name << " seed " << seed << " light " << str(light) << " mode "
                                << std::string(mode == Mode::Brute ? "Brute" : "Grid") << " first mismatching q " << str(firstBad));
                    CHECK(mismatches == 0);
                }
            }
        }
    }
    CHECK(lightsUsed > scaled(500));
    CHECK(compared > scaled(scaled(100000)));
}

// The area tolerance of the sampling test lets a thin wrong sliver through. This test probes the
// polygon boundary itself: for each polygon edge (not radial, not shorter than a few offsets) take
// the edge midpoint and test the points `off` to either side of it against the independent
// segment-crossing predicate. `off` is 1e-4 on the 1600-wide world and proportional elsewhere: far
// above the rounding of vertices (about 1e-13) and of the predicate, far below any feature of the test
// scenes (walls and gaps are at least about 1 unit). Probes that lie within off/2 of a different edge
// (a neighbouring sliver) are skipped, because there "which edge am I near" is the question.
TEST_CASE("oracle: points just inside and just outside every polygon edge agree with the predicate") {
    long long edgesTested = 0, probes = 0, skipped = 0;
    for (const OracleCase& c : makeCases(0xED6E0000, scaled(100))) {
        const double off = 1e-4 * std::max(c.scene.width(), c.scene.height()) / 1600.0;
        for (Vec2 light : c.lights) {
            const Poly poly = query(c.scene, light, Mode::Grid);
            const std::size_t n = poly.size();
            for (std::size_t i = 0; i < n; ++i) {
                const Vec2 a = poly[i], b = poly[(i + 1) % n];
                const Vec2 e = b - a;
                const double len = std::sqrt(e.x * e.x + e.y * e.y);
                if (len < 4 * off) continue;
                const Vec2 la = a - light, lb = b - light;
                const double ra = std::sqrt(la.x * la.x + la.y * la.y), rb = std::sqrt(lb.x * lb.x + lb.y * lb.y);
                if (ra == 0.0 || rb == 0.0) continue;
                if (std::fabs(la.x * lb.y - la.y * lb.x) <= 1e-9 * ra * rb) continue;  // radial edge
                ++edgesTested;
                const Vec2 mid = a + e * 0.5;
                const Vec2 normal{-e.y / len, e.x / len};
                for (double side : {-1.0, 1.0}) {
                    const Vec2 q = mid + normal * (side * off);
                    if (distToBoundary(poly, q) < 0.5 * off) { ++skipped; continue; }
                    ++probes;
                    const bool lit = litByDefinition(c.scene, light, q);
                    const bool in = pointInPolygon(poly, q);
                    INFO(c.label << " light " << str(light) << " edge " << i << " side " << side << " probe " << str(q));
                    CHECK(lit == in);
                }
            }
        }
    }
    CHECK(edgesTested > scaled(2000));
    CHECK(probes > scaled(3000));
    CHECK(skipped < probes);
}
