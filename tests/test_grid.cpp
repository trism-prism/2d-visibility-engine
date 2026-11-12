// Grid invariants (DESIGN section 7, check 5) and the supporting checks: trace-on equals trace-off,
// stats sanity, mutators leave derived data consistent, steady-state queries do not allocate.
#include <cstdlib>
#include <set>

#include "test_util.h"

using namespace vis;
using namespace tutil;

namespace {

// Independent segment / closed box overlap: bounding boxes overlap and the box corners are not all
// strictly on one side of the segment's line. Orientation tests only, no clipping.
bool overlapsBox(const Segment& s, double x0, double y0, double x1, double y1) {
    if (std::max(s.a.x, s.b.x) < x0 || std::min(s.a.x, s.b.x) > x1) return false;
    if (std::max(s.a.y, s.b.y) < y0 || std::min(s.a.y, s.b.y) > y1) return false;
    const int o[4] = {orientSign(s.a, s.b, {x0, y0}), orientSign(s.a, s.b, {x1, y0}), orientSign(s.a, s.b, {x1, y1}),
                      orientSign(s.a, s.b, {x0, y1})};
    const bool allPos = o[0] > 0 && o[1] > 0 && o[2] > 0 && o[3] > 0;
    const bool allNeg = o[0] < 0 && o[1] < 0 && o[2] < 0 && o[3] < 0;
    return !(allPos || allNeg);
}

std::vector<Scene> sampleScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(makeDemoScene());
    scenes.push_back(makeScatterScene(150, 11));
    scenes.push_back(makeMazeScene(150, 12));
    scenes.push_back(makeLongWallsScene(150, 13));
    scenes.push_back(makeClusterScene(150, 14));
    Rng rng(99);
    for (int i = 0; i < 6; ++i) {
        std::vector<Segment> walls = latticeWalls(rng, 20 + rng.below(30), 64, true);
        scenes.push_back(makeScene(64, 64, walls));
    }
    scenes.push_back(makeScene(100, 70, realWalls(rng, 60, 100, 70, 120)));
    return scenes;
}

const int kResolutionList[][2] = {{1, 1}, {2, 3}, {8, 8}, {16, 10}, {33, 21}, {64, 64}};

}  // namespace

TEST_CASE("grid: registration is a superset of brute-force segment/cell overlap") {
    long long cellsChecked = 0, registeredOnly = 0;
    for (Scene& scene : sampleScenes()) {
        for (int k = 0; k < 6; k += VIS_TEST_SCALE) {  // largest resolutions first when scaled down
            const int ri = 5 - k;
            const int* res = kResolutionList[ri];
            scene.setGridResolution(res[0], res[1]);
            const Grid& g = scene.grid();
            REQUIRE(g.cols() == res[0]);
            REQUIRE(g.rows() == res[1]);
            for (int cy = 0; cy < g.rows(); ++cy) {
                for (int cx = 0; cx < g.cols(); ++cx) {
                    const std::size_t c = g.cellIndex(cx, cy);
                    const auto list = g.cellWalls(c);
                    const std::set<std::uint32_t> registered(list.begin(), list.end());
                    CHECK(registered.size() == list.size());                       // no duplicates
                    CHECK(std::is_sorted(list.begin(), list.end()));               // ascending
                    const double padX = 1e-5 * g.cellWidth();  // 10x kGridPad: registration must stay tight
                    const double padY = 1e-5 * g.cellHeight();
                    for (std::uint32_t w = 0; w < scene.wallCount(); ++w) {
                        const Segment& s = scene.walls()[w];
                        ++cellsChecked;
                        if (overlapsBox(s, g.xEdge(cx), g.yEdge(cy), g.xEdge(cx + 1), g.yEdge(cy + 1))) {
                            CHECK_MESSAGE(registered.count(w) == 1, "wall " << w << " overlaps cell (" << cx << "," << cy
                                                                           << ") of " << g.cols() << "x" << g.rows()
                                                                           << " but is not registered");
                        }
                        if (registered.count(w) &&
                            !overlapsBox(s, g.xEdge(cx) - padX, g.yEdge(cy) - padY, g.xEdge(cx + 1) + padX,
                                         g.yEdge(cy + 1) + padY)) {
                            FAIL_CHECK("wall " << w << " registered in (" << cx << "," << cy << ") far from the cell");
                        }
                        if (registered.count(w) && !overlapsBox(s, g.xEdge(cx), g.yEdge(cy), g.xEdge(cx + 1), g.yEdge(cy + 1)))
                            ++registeredOnly;  // legitimate padding at exact cell boundaries
                    }
                }
            }
            // inverse lists agree
            for (std::uint32_t w = 0; w < scene.wallCount(); ++w) {
                for (std::uint32_t c : g.wallCells(w)) {
                    const auto list = g.cellWalls(c);
                    CHECK(std::binary_search(list.begin(), list.end(), w));
                }
            }
        }
    }
    CHECK(cellsChecked > scaled(100000));
    MESSAGE("(wall, cell) pairs checked: " << cellsChecked << "; registered only by padding: " << registeredOnly);
}

TEST_CASE("grid: a wall through a cell corner is registered in all four adjacent cells") {
    Scene s(80, 80);
    s.setGridResolution(8, 8);  // cell size 10
    REQUIRE(s.addWall({5, 5}, {25, 25}) == AddStatus::Added);     // diagonal through corners (10,10), (20,20)
    REQUIRE(s.addWall({40, 0}, {40, 80}) == AddStatus::Added);    // on a grid line
    const Grid& g = s.grid();
    const std::uint32_t diag = 4, line = 5;
    const std::vector<std::pair<int, int>> diagCells = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}, {2, 2}};
    for (auto [cx, cy] : diagCells) {
        const auto list = g.cellWalls(g.cellIndex(cx, cy));
        CHECK_MESSAGE(std::find(list.begin(), list.end(), diag) != list.end(), "diagonal missing in (" << cx << "," << cy << ")");
    }
    for (int cy = 0; cy < 8; ++cy)
        for (int cx : {3, 4}) {
            const auto list = g.cellWalls(g.cellIndex(cx, cy));
            CHECK_MESSAGE(std::find(list.begin(), list.end(), line) != list.end(), "line wall missing in (" << cx << "," << cy << ")");
        }
}

TEST_CASE("grid: intersection finder equals the O(n^2) reference") {
    long long pairsReference = 0;
    for (Scene& scene : sampleScenes()) {
        for (auto res : kResolutionList) {
            scene.setGridResolution(res[0], res[1]);
            std::vector<Vec2> reference;
            const auto& walls = scene.walls();
            for (std::size_t i = 0; i < walls.size(); ++i)
                for (std::size_t j = i + 1; j < walls.size(); ++j) {
                    Vec2 p;
                    ++pairsReference;
                    if (intersectSegments(walls[i], walls[j], p) == SegHit::Cross) reference.push_back(p);
                }
            std::vector<Vec2> found = scene.intersections();
            const auto lexLess = [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); };
            std::sort(reference.begin(), reference.end(), lexLess);
            std::sort(found.begin(), found.end(), lexLess);
            INFO("grid " << res[0] << "x" << res[1] << ", walls " << walls.size());
            CHECK(found == reference);  // exact: same pair order i < j, same arithmetic
            CHECK(scene.lastRebuild().intersections == reference.size());
            CHECK(scene.lastRebuild().pairsTested <= walls.size() * (walls.size() - 1) / 2);

            // targets = sorted unique endpoints + crossings
            std::vector<Vec2> expectTargets;
            for (const Segment& s : walls) { expectTargets.push_back(s.a); expectTargets.push_back(s.b); }
            expectTargets.insert(expectTargets.end(), reference.begin(), reference.end());
            std::sort(expectTargets.begin(), expectTargets.end(), lexLess);
            expectTargets.erase(std::unique(expectTargets.begin(), expectTargets.end()), expectTargets.end());
            CHECK(scene.targets() == expectTargets);
        }
    }
    CHECK(pairsReference > scaled(100000));
}

TEST_CASE("grid: the polygon does not depend on the grid resolution") {
    Scene scene = makeScatterScene(200, 5);
    std::vector<Vec2> lights = {{800, 500}, {10, 10}, {1599, 999}, {400, 250}};
    for (Vec2 l : lights) {
        const Poly reference = query(scene, l, Mode::Brute);
        for (auto res : kResolutionList) {
            scene.setGridResolution(res[0], res[1]);
            CHECK(query(scene, l, Mode::Grid) == reference);
        }
    }
}

TEST_CASE("trace: on equals off, and the trace describes what the query really did") {
    VisibilityResult plain, traced;
    Trace trace;
    long long raysChecked = 0;
    Rng rng(2024);
    for (Scene& scene : sampleScenes()) {
        for (Mode mode : {Mode::Brute, Mode::Grid}) {
            for (int li = 0; li < 4; ++li) {
                Vec2 light{rng.range(0, scene.width()), rng.range(0, scene.height())};
                if (li == 3 && scene.userWallCount() > 0) light = scene.walls()[4].a;  // on a wall endpoint
                computeVisibility(scene, light, mode, plain);
                computeVisibility(scene, light, mode, traced, &trace);
                INFO("mode " << std::string(mode == Mode::Brute ? "Brute" : "Grid") << " light " << str(light));
                CHECK(plain.polygon == traced.polygon);
                CHECK(plain.stats.raysCast == traced.stats.raysCast);
                CHECK(plain.stats.wallTests == traced.stats.wallTests);
                CHECK(plain.stats.cellsVisited == traced.stats.cellsVisited);
                CHECK(plain.stats.outputVertices == traced.stats.outputVertices);

                REQUIRE(trace.rays.size() == traced.stats.raysCast);
                std::uint64_t cellsTotal = 0, wallsTotal = 0, visitsTotal = 0;
                for (const Trace::RayRecord& r : trace.rays) {
                    ++raysChecked;
                    cellsTotal += r.cellEnd - r.cellBegin;
                    wallsTotal += r.wallEnd - r.wallBegin;
                    // the recorded limits are the true limits: recompute them over all walls
                    double tm = kInf, tp = kInf;
                    const Ray ray = makeRay(trace.light, r.target);
                    for (const Segment& w : scene.walls()) rayHit(ray, w, tm, tp);
                    CHECK(r.tMinus == tm);
                    CHECK(r.tPlus == tp);
                    CHECK(r.dir == r.target - trace.light);
                    // every finite limit shows up in the polygon
                    for (double t : {r.tMinus, r.tPlus}) {
                        const Vec2 v = std::isinf(t) ? trace.light : Vec2{trace.light.x + r.dir.x * t, trace.light.y + r.dir.y * t};
                        CHECK(std::find(traced.polygon.begin(), traced.polygon.end(), v) != traced.polygon.end());
                    }
                    if (mode == Mode::Grid) {
                        // walk starts in the light's cell and moves one cell at a time
                        const auto first = trace.cells[r.cellBegin];
                        CHECK(first == scene.grid().cellIndex(scene.grid().cellXOf(trace.light.x), scene.grid().cellYOf(trace.light.y)));
                        for (std::uint32_t k = r.cellBegin + 1; k < r.cellEnd; ++k) {
                            const int cols = scene.grid().cols();
                            const long long a = trace.cells[k - 1], b = trace.cells[k];
                            const long long dx = std::llabs(a % cols - b % cols), dy = std::llabs(a / cols - b / cols);
                            CHECK(dx + dy == 1);
                        }
                        // invariant 3: no wall tested twice by one ray
                        std::set<std::uint32_t> uniq(trace.walls.begin() + r.wallBegin, trace.walls.begin() + r.wallEnd);
                        CHECK(uniq.size() == r.wallEnd - r.wallBegin);
                    } else {
                        CHECK(r.wallEnd - r.wallBegin == scene.wallCount());
                        CHECK(r.cellEnd == r.cellBegin);
                    }
                }
                for (std::uint32_t v : trace.cellVisits) visitsTotal += v;
                CHECK(cellsTotal == traced.stats.cellsVisited);
                CHECK(wallsTotal == traced.stats.wallTests);
                CHECK(visitsTotal == traced.stats.cellsVisited);
                CHECK(trace.cellVisits.size() == (mode == Mode::Grid ? scene.grid().cellCount() : 0u));
            }
        }
    }
    CHECK(raysChecked > scaled(5000));
}

TEST_CASE("stats sanity: Grid never tests more walls than Brute") {
    Rng rng(77);
    long long gridTests = 0, bruteTests = 0;
    for (Scene& scene : sampleScenes()) {
        for (int i = 0; i < 6; ++i) {
            const Vec2 light{rng.range(0, scene.width()), rng.range(0, scene.height())};
            VisibilityResult b, g;
            computeVisibility(scene, light, Mode::Brute, b);
            computeVisibility(scene, light, Mode::Grid, g);
            CHECK(b.stats.raysCast == g.stats.raysCast);
            CHECK(b.stats.targets == scene.targets().size());
            CHECK(b.stats.wallTests == b.stats.raysCast * scene.wallCount());
            CHECK(b.stats.cellsVisited == 0);
            CHECK(g.stats.wallTests <= b.stats.wallTests);
            CHECK(g.stats.cellsVisited >= g.stats.raysCast);  // at least the start cell per ray
            CHECK(g.stats.outputVertices == g.polygon.size());
            CHECK(b.stats.outputVertices == b.polygon.size());
            for (const VisibilityStats* st : {&b.stats, &g.stats}) {
                CHECK(st->sortUs >= 0.0);
                CHECK(st->castUs >= 0.0);
                // total is one clock difference, sort + cast two: equal up to double rounding
                CHECK(st->sortUs + st->castUs <= st->totalUs * (1.0 + 1e-12));
                CHECK(st->raysCast <= st->targets);
            }
            gridTests += static_cast<long long>(g.stats.wallTests);
            bruteTests += static_cast<long long>(b.stats.wallTests);
        }
    }
    CHECK(gridTests < bruteTests);  // and on these scenes, strictly fewer overall
}

TEST_CASE("scene mutators leave derived data consistent with a fresh build") {
    Rng rng(31337);
    Scene s(100, 70);
    const auto lexLess = [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); };

    for (int step = 0; step < scaled(150); ++step) {
        const int op = rng.below(10);
        if (op <= 4) {
            const Vec2 a{rng.range(-10, 110), rng.range(-10, 80)};
            s.addWall(a, a + rng.direction() * rng.range(0, 60));
        } else if (op == 5) {
            s.removeWall(static_cast<std::size_t>(rng.below(static_cast<int>(s.wallCount()) + 2)));
        } else if (op == 6) {
            if (rng.below(4) == 0) s.clear();
        } else if (op == 7) {
            s.setWalls(realWalls(rng, rng.below(30), 100, 70, 50));
        } else if (op == 8) {
            s.setGridResolution(1 + rng.below(30), 1 + rng.below(30));
        } else {
            s.setGridAuto();
        }

        // fresh scene from the same walls and the same grid settings
        Scene fresh(100, 70);
        fresh.setWalls(std::vector<Segment>(s.walls().begin() + 4, s.walls().end()));
        if (!s.gridIsAuto()) fresh.setGridResolution(s.grid().cols(), s.grid().rows());
        INFO("step " << step << " op " << op);
        REQUIRE(s.walls() == fresh.walls());
        REQUIRE(s.grid().cols() == fresh.grid().cols());
        REQUIRE(s.grid().rows() == fresh.grid().rows());
        for (std::size_t c = 0; c < s.grid().cellCount(); ++c) {
            const auto a = s.grid().cellWalls(c);
            const auto b = fresh.grid().cellWalls(c);
            REQUIRE(std::equal(a.begin(), a.end(), b.begin(), b.end()));
        }
        std::vector<Vec2> i1 = s.intersections(), i2 = fresh.intersections();
        std::sort(i1.begin(), i1.end(), lexLess);
        std::sort(i2.begin(), i2.end(), lexLess);
        REQUIRE(i1 == i2);
        REQUIRE(s.targets() == fresh.targets());
        CHECK(s.lastRebuild().walls == s.wallCount());
        CHECK(s.lastRebuild().targets == s.targets().size());
        CHECK(s.lastRebuild().gridCells == s.grid().cellCount());
        CHECK(s.lastRebuild().gridBuildUs >= 0.0);
        CHECK(s.lastRebuild().intersectUs >= 0.0);
        // and the query agrees with the fresh scene
        const Vec2 l{rng.range(0, 100), rng.range(0, 70)};
        REQUIRE(query(s, l, Mode::Grid) == query(fresh, l, Mode::Grid));
        REQUIRE(query(s, l, Mode::Grid) == query(s, l, Mode::Brute));
    }
}

TEST_CASE("steady-state queries do not allocate") {
    const Scene scene = makeScatterScene(300, 3);
    VisibilityResult r;
    Rng rng(5);
    computeVisibility(scene, {800, 500}, Mode::Grid, r);  // warm-up, sizes every buffer
    const auto* poly = r.polygon.data();
    const auto* items = r.workspace.items.data();
    const auto* rays = r.workspace.rays.data();
    const auto* stamp = r.workspace.stamp.data();
    const std::size_t pc = r.polygon.capacity(), ic = r.workspace.items.capacity(), rc = r.workspace.rays.capacity(),
                      sc = r.workspace.stamp.capacity();
    for (int i = 0; i < scaled(300); ++i) {
        const Mode mode = (i % 2) ? Mode::Grid : Mode::Brute;
        computeVisibility(scene, {rng.range(0, 1600), rng.range(0, 1000)}, mode, r);
    }
    CHECK(r.polygon.data() == poly);
    CHECK(r.workspace.items.data() == items);
    CHECK(r.workspace.rays.data() == rays);
    CHECK(r.workspace.stamp.data() == stamp);
    CHECK(r.polygon.capacity() == pc);
    CHECK(r.workspace.items.capacity() == ic);
    CHECK(r.workspace.rays.capacity() == rc);
    CHECK(r.workspace.stamp.capacity() == sc);
}
