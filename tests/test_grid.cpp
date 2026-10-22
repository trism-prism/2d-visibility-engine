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
    Tally tally;
    for (Scene& scene : sampleScenes()) {
        for (int k = 0; k < 6; k += VIS_TEST_SCALE) {  // largest resolutions first when scaled down
            const int ri = 5 - k;
            const int* res = kResolutionList[ri];
            scene.setGridResolution(res[0], res[1]);
            const Grid& g = scene.grid();
            TALLY(tally, g.cols() == res[0] && g.rows() == res[1], "grid resolution " << res[0] << "x" << res[1] << " not applied");
            if (g.cols() != res[0] || g.rows() != res[1]) continue;
            for (int cy = 0; cy < g.rows(); ++cy) {
                for (int cx = 0; cx < g.cols(); ++cx) {
                    const std::size_t c = g.cellIndex(cx, cy);
                    const auto list = g.cellWalls(c);
                    const std::set<std::uint32_t> registered(list.begin(), list.end());
                    TALLY(tally, registered.size() == list.size(), "duplicate walls in cell (" << cx << "," << cy << ")");
                    TALLY(tally, std::is_sorted(list.begin(), list.end()), "cell (" << cx << "," << cy << ") list not ascending");
                    const double padX = 1e-5 * g.cellWidth();  // 10x kGridPad: registration must stay tight
                    const double padY = 1e-5 * g.cellHeight();
                    for (std::uint32_t w = 0; w < scene.wallCount(); ++w) {
                        const Segment& s = scene.walls()[w];
                        ++cellsChecked;
                        if (overlapsBox(s, g.xEdge(cx), g.yEdge(cy), g.xEdge(cx + 1), g.yEdge(cy + 1))) {
                            TALLY(tally, registered.count(w) == 1, "wall " << w << " overlaps cell (" << cx << "," << cy
                                                                           << ") of " << g.cols() << "x" << g.rows()
                                                                           << " but is not registered");
                        }
                        TALLY(tally, !(registered.count(w) &&
                                       !overlapsBox(s, g.xEdge(cx) - padX, g.yEdge(cy) - padY, g.xEdge(cx + 1) + padX,
                                                    g.yEdge(cy + 1) + padY)),
                              "wall " << w << " registered in (" << cx << "," << cy << ") far from the cell");
                        if (registered.count(w) && !overlapsBox(s, g.xEdge(cx), g.yEdge(cy), g.xEdge(cx + 1), g.yEdge(cy + 1)))
                            ++registeredOnly;  // legitimate padding at exact cell boundaries
                    }
                }
            }
            // inverse lists agree
            for (std::uint32_t w = 0; w < scene.wallCount(); ++w) {
                for (std::uint32_t c : g.wallCells(w)) {
                    const auto list = g.cellWalls(c);
                    TALLY(tally, std::binary_search(list.begin(), list.end(), w), "inverse list: wall " << w << " missing from cell " << c);
                }
            }
        }
    }
    TALLY_CHECK(tally);
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
    Tally tally;
    const std::vector<std::pair<int, int>> diagCells = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}, {2, 2}};
    for (auto [cx, cy] : diagCells) {
        const auto list = g.cellWalls(g.cellIndex(cx, cy));
        TALLY(tally, std::find(list.begin(), list.end(), diag) != list.end(), "diagonal missing in (" << cx << "," << cy << ")");
    }
    for (int cy = 0; cy < 8; ++cy)
        for (int cx : {3, 4}) {
            const auto list = g.cellWalls(g.cellIndex(cx, cy));
            TALLY(tally, std::find(list.begin(), list.end(), line) != list.end(), "line wall missing in (" << cx << "," << cy << ")");
        }
    TALLY_CHECK(tally);
}

TEST_CASE("grid: intersection finder equals the O(n^2) reference") {
    long long pairsReference = 0;
    Tally tally;
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
            const std::string ctx = "grid " + std::to_string(res[0]) + "x" + std::to_string(res[1]) + ", walls " + std::to_string(walls.size()) + ": ";
            TALLY(tally, found == reference, ctx << "intersections differ from the O(n^2) reference");  // exact: same arithmetic
            TALLY(tally, scene.lastRebuild().intersections == reference.size(), ctx << "rebuild stats intersections wrong");
            TALLY(tally, scene.lastRebuild().pairsTested <= walls.size() * (walls.size() - 1) / 2, ctx << "tested more pairs than n(n-1)/2");

            // targets = sorted unique endpoints + crossings
            std::vector<Vec2> expectTargets;
            for (const Segment& s : walls) { expectTargets.push_back(s.a); expectTargets.push_back(s.b); }
            expectTargets.insert(expectTargets.end(), reference.begin(), reference.end());
            std::sort(expectTargets.begin(), expectTargets.end(), lexLess);
            expectTargets.erase(std::unique(expectTargets.begin(), expectTargets.end()), expectTargets.end());
            TALLY(tally, scene.targets() == expectTargets, ctx << "targets differ from sorted unique endpoints + crossings");
        }
    }
    TALLY_CHECK(tally);
    CHECK(pairsReference > scaled(100000));
}

TEST_CASE("grid: the polygon does not depend on the grid resolution") {
    Scene scene = makeScatterScene(200, 5);
    std::vector<Vec2> lights = {{800, 500}, {10, 10}, {1599, 999}, {400, 250}};
    Tally tally;
    for (Vec2 l : lights) {
        const Poly reference = query(scene, l, Mode::Brute);
        for (auto res : kResolutionList) {
            scene.setGridResolution(res[0], res[1]);
            TALLY(tally, query(scene, l, Mode::Grid) == reference, "light " << str(l) << " grid " << res[0] << "x" << res[1] << " differs from Brute");
        }
    }
    TALLY_CHECK(tally);
}

TEST_CASE("trace: on equals off, and the trace describes what the query really did") {
    VisibilityResult plain, traced;
    Trace trace;
    long long raysChecked = 0;
    Tally tally;
    Rng rng(2024);
    for (Scene& scene : sampleScenes()) {
        for (Mode mode : {Mode::Brute, Mode::Grid}) {
            for (int li = 0; li < 4; ++li) {
                Vec2 light{rng.range(0, scene.width()), rng.range(0, scene.height())};
                if (li == 3 && scene.userWallCount() > 0) light = scene.walls()[4].a;  // on a wall endpoint
                computeVisibility(scene, light, mode, plain);
                computeVisibility(scene, light, mode, traced, &trace);
                const std::string ctx = std::string("mode ") + (mode == Mode::Brute ? "Brute" : "Grid") + " light " + str(light) + ": ";
                TALLY(tally, plain.polygon == traced.polygon, ctx << "trace-on polygon differs from trace-off");
                TALLY(tally, plain.stats.raysCast == traced.stats.raysCast, ctx << "raysCast differs");
                TALLY(tally, plain.stats.wallTests == traced.stats.wallTests, ctx << "wallTests differ");
                TALLY(tally, plain.stats.cellsVisited == traced.stats.cellsVisited, ctx << "cellsVisited differ");
                TALLY(tally, plain.stats.outputVertices == traced.stats.outputVertices, ctx << "outputVertices differ");

                TALLY(tally, trace.rays.size() == traced.stats.raysCast, ctx << "trace ray count != raysCast");
                if (trace.rays.size() != traced.stats.raysCast) continue;
                std::uint64_t cellsTotal = 0, wallsTotal = 0, visitsTotal = 0;
                for (const Trace::RayRecord& r : trace.rays) {
                    ++raysChecked;
                    cellsTotal += r.cellEnd - r.cellBegin;
                    wallsTotal += r.wallEnd - r.wallBegin;
                    // the recorded limits are the true limits: recompute them over all walls
                    double tm = kInf, tp = kInf;
                    const Ray ray = makeRay(trace.light, r.target);
                    for (const Segment& w : scene.walls()) rayHit(ray, w, tm, tp);
                    TALLY(tally, r.tMinus == tm && r.tPlus == tp, ctx << "recorded limits differ from recomputed at target " << str(r.target));
                    TALLY(tally, r.dir == r.target - trace.light, ctx << "ray direction != target - light");
                    // every finite limit shows up in the polygon
                    for (double t : {r.tMinus, r.tPlus}) {
                        const Vec2 v = std::isinf(t) ? trace.light : Vec2{trace.light.x + r.dir.x * t, trace.light.y + r.dir.y * t};
                        TALLY(tally, std::find(traced.polygon.begin(), traced.polygon.end(), v) != traced.polygon.end(),
                              ctx << "limit of ray to " << str(r.target) << " missing from the polygon");
                    }
                    if (mode == Mode::Grid) {
                        // walk starts in the light's cell and moves one cell at a time
                        const auto first = trace.cells[r.cellBegin];
                        TALLY(tally, first == scene.grid().cellIndex(scene.grid().cellXOf(trace.light.x), scene.grid().cellYOf(trace.light.y)),
                              ctx << "walk does not start in the light's cell");
                        for (std::uint32_t k = r.cellBegin + 1; k < r.cellEnd; ++k) {
                            const int cols = scene.grid().cols();
                            const long long a = trace.cells[k - 1], b = trace.cells[k];
                            const long long dx = std::llabs(a % cols - b % cols), dy = std::llabs(a / cols - b / cols);
                            TALLY(tally, dx + dy == 1, ctx << "walk jumps between non-adjacent cells");
                        }
                        // invariant 3: no wall tested twice by one ray
                        std::set<std::uint32_t> uniq(trace.walls.begin() + r.wallBegin, trace.walls.begin() + r.wallEnd);
                        TALLY(tally, uniq.size() == r.wallEnd - r.wallBegin, ctx << "a wall was tested twice by one ray");
                    } else {
                        // Brute records no per-ray wall lists (they would be T * n entries)
                        TALLY(tally, r.wallEnd == r.wallBegin && r.cellEnd == r.cellBegin, ctx << "Brute recorded per-ray lists");
                    }
                }
                for (std::uint32_t v : trace.cellVisits) visitsTotal += v;
                TALLY(tally, cellsTotal == traced.stats.cellsVisited, ctx << "per-ray cell total != cellsVisited");
                if (mode == Mode::Grid) {
                    TALLY(tally, wallsTotal == traced.stats.wallTests, ctx << "per-ray wall total != wallTests");
                    TALLY(tally, !trace.allWallsTested, ctx << "Grid trace claims all walls tested");
                } else {
                    TALLY(tally, wallsTotal == 0 && trace.walls.empty(), ctx << "Brute trace has wall lists");
                    TALLY(tally, trace.allWallsTested, ctx << "Brute trace does not claim all walls tested");
                    TALLY(tally, traced.stats.wallTests == traced.stats.raysCast * scene.wallCount(), ctx << "Brute wallTests != rays * walls");
                }
                TALLY(tally, visitsTotal == traced.stats.cellsVisited, ctx << "cell visit total != cellsVisited");
                TALLY(tally, trace.cellVisits.size() == (mode == Mode::Grid ? scene.grid().cellCount() : 0u), ctx << "cellVisits size wrong");
            }
        }
    }
    TALLY_CHECK(tally);
    CHECK(raysChecked > scaled(5000));
}

TEST_CASE("stats sanity: Grid never tests more walls than Brute") {
    Rng rng(77);
    long long gridTests = 0, bruteTests = 0;
    Tally tally;
    for (Scene& scene : sampleScenes()) {
        for (int i = 0; i < 6; ++i) {
            const Vec2 light{rng.range(0, scene.width()), rng.range(0, scene.height())};
            VisibilityResult b, g;
            computeVisibility(scene, light, Mode::Brute, b);
            computeVisibility(scene, light, Mode::Grid, g);
            const std::string ctx = "light " + str(light) + ": ";
            TALLY(tally, b.stats.raysCast == g.stats.raysCast, ctx << "raysCast differs between modes");
            TALLY(tally, b.stats.targets == scene.targets().size(), ctx << "targets stat wrong");
            TALLY(tally, b.stats.wallTests == b.stats.raysCast * scene.wallCount(), ctx << "Brute wallTests != rays * walls");
            TALLY(tally, b.stats.cellsVisited == 0, ctx << "Brute visited cells");
            TALLY(tally, g.stats.wallTests <= b.stats.wallTests, ctx << "Grid tested more walls than Brute");
            TALLY(tally, g.stats.cellsVisited >= g.stats.raysCast, ctx << "fewer cells than rays");  // at least the start cell per ray
            TALLY(tally, g.stats.outputVertices == g.polygon.size() && b.stats.outputVertices == b.polygon.size(), ctx << "outputVertices != polygon size");
            for (const VisibilityStats* st : {&b.stats, &g.stats}) {
                // total is one clock difference, sort + cast two: equal up to double rounding
                TALLY(tally, st->sortUs >= 0.0 && st->castUs >= 0.0 && st->sortUs + st->castUs <= st->totalUs * (1.0 + 1e-12),
                      ctx << "timing stats inconsistent");
                TALLY(tally, st->raysCast <= st->targets, ctx << "more rays than targets");
            }
            gridTests += static_cast<long long>(g.stats.wallTests);
            bruteTests += static_cast<long long>(b.stats.wallTests);
        }
    }
    TALLY_CHECK(tally);
    CHECK(gridTests < bruteTests);  // and on these scenes, strictly fewer overall
}

TEST_CASE("scene mutators leave derived data consistent with a fresh build") {
    Rng rng(31337);
    Scene s(100, 70);
    const auto lexLess = [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); };
    Tally tally;

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
        const std::string ctx = "step " + std::to_string(step) + " op " + std::to_string(op) + ": ";
        TALLY(tally, s.walls() == fresh.walls(), ctx << "walls differ from a fresh scene");
        const bool sameGrid = s.grid().cols() == fresh.grid().cols() && s.grid().rows() == fresh.grid().rows();
        TALLY(tally, sameGrid, ctx << "grid resolution differs from a fresh scene");
        if (!sameGrid) continue;
        for (std::size_t c = 0; c < s.grid().cellCount(); ++c) {
            const auto a = s.grid().cellWalls(c);
            const auto b = fresh.grid().cellWalls(c);
            TALLY(tally, std::equal(a.begin(), a.end(), b.begin(), b.end()), ctx << "cell " << c << " list differs from a fresh scene");
        }
        std::vector<Vec2> i1 = s.intersections(), i2 = fresh.intersections();
        std::sort(i1.begin(), i1.end(), lexLess);
        std::sort(i2.begin(), i2.end(), lexLess);
        TALLY(tally, i1 == i2, ctx << "intersections differ from a fresh scene");
        TALLY(tally, s.targets() == fresh.targets(), ctx << "targets differ from a fresh scene");
        TALLY(tally, s.lastRebuild().walls == s.wallCount(), ctx << "rebuild stats walls wrong");
        TALLY(tally, s.lastRebuild().targets == s.targets().size(), ctx << "rebuild stats targets wrong");
        TALLY(tally, s.lastRebuild().gridCells == s.grid().cellCount(), ctx << "rebuild stats gridCells wrong");
        TALLY(tally, s.lastRebuild().gridBuildUs >= 0.0 && s.lastRebuild().intersectUs >= 0.0, ctx << "negative rebuild timing");
        // and the query agrees with the fresh scene
        const Vec2 l{rng.range(0, 100), rng.range(0, 70)};
        TALLY(tally, query(s, l, Mode::Grid) == query(fresh, l, Mode::Grid), ctx << "query differs from the fresh scene");
        TALLY(tally, query(s, l, Mode::Grid) == query(s, l, Mode::Brute), ctx << "Grid != Brute");
    }
    TALLY_CHECK(tally);
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

TEST_CASE("stamp counter wraparound: Grid still equals Brute when the counter is near 2^32") {
    const Scene scene = makeScatterScene(300, 21);
    Rng rng(9);
    VisibilityResult r, b;
    computeVisibility(scene, {800, 500}, Mode::Grid, r);  // sizes the stamp array
    const std::uint64_t rays = r.stats.raysCast;
    // starts below, at and above the wrap threshold (counter > 2^32 - 1 - rays - 1 resets it), with
    // stale stamps that equal the next ray ids to catch a missing reset
    const std::uint32_t starts[] = {0u, 5u, 0xFFFFFFFFu - static_cast<std::uint32_t>(rays) - 3u,
                                    0xFFFFFFFFu - static_cast<std::uint32_t>(rays) - 2u,
                                    0xFFFFFFFFu - static_cast<std::uint32_t>(rays) - 1u,
                                    0xFFFFFFFFu - static_cast<std::uint32_t>(rays), 0xFFFFFFFFu - 5u, 0xFFFFFFFFu};
    Tally tally;
    for (std::uint32_t start : starts) {
        for (int i = 0; i < 3; ++i) {  // several queries in a row cross the threshold
            const Vec2 light{rng.range(0, 1600), rng.range(0, 1000)};
            r.workspace.stampCounter = (i == 0) ? start : r.workspace.stampCounter;
            std::fill(r.workspace.stamp.begin(), r.workspace.stamp.end(), start);  // stale values never exceed the counter
            computeVisibility(scene, light, Mode::Grid, r);
            computeVisibility(scene, light, Mode::Brute, b);
            TALLY(tally, r.polygon == b.polygon, "start " << start << " query " << i << ": Grid != Brute");
            TALLY(tally, r.stats.wallTests > 0, "start " << start << " query " << i << ": no wall tests");
        }
    }
    TALLY_CHECK(tally);
    CHECK(r.workspace.stampCounter < 0xFFFFFFFFu);
}

namespace {
// Cell boxes are closed: a point is "in" a cell if it is within the cell's box (small slack for the
// rounding of the sample point itself, 1e-9 of a cell).
bool inClosedCell(const Grid& g, int cx, int cy, Vec2 p) {
    const double sx = 1e-9 * g.cellWidth(), sy = 1e-9 * g.cellHeight();
    return p.x >= g.xEdge(cx) - sx && p.x <= g.xEdge(cx + 1) + sx && p.y >= g.yEdge(cy) - sy && p.y <= g.yEdge(cy + 1) + sy;
}
}  // namespace

TEST_CASE("GridWalker: terminates, exit parameters never decrease, visited cells cover the ray") {
    Scene s(80, 60);
    s.setGridResolution(8, 6);  // 10 x 10 cells
    const Grid& g = s.grid();
    const std::vector<Vec2> origins = {{35, 25},  {30, 25}, {30, 20},   {0, 35},     {80, 35}, {35, 0},
                                       {35, 60},  {0, 0},   {80, 60},   {0, 60},     {80, 0},  {10.5, 59.999999},
                                       {33.3, 41.7}};
    std::vector<Vec2> dirs = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}, {2, 1}, {-3, 1},
                              {1e-9, 1}, {1, 1e-9}, {0.1, -0.7}};
    Rng rng(4);
    for (int i = 0; i < 40; ++i) dirs.push_back(rng.direction());
    long long rays = 0;
    Tally tally;
    for (Vec2 o : origins)
        for (Vec2 d : dirs) {
            ++rays;
            GridWalker w(g, o, d);
            std::vector<std::pair<int, int>> visited;
            double lastExit = -1e300;
            int steps = 0;
            while (w.inside()) {
                const bool bounded = ++steps <= g.cols() + g.rows() + 2;  // a straight line crosses at most cols+rows-1 edges
                TALLY(tally, bounded, "walk from " << str(o) << " dir " << str(d) << " does not terminate");
                if (!bounded) break;
                const std::size_t c = w.cell();
                visited.push_back({static_cast<int>(c % static_cast<std::size_t>(g.cols())),
                                   static_cast<int>(c / static_cast<std::size_t>(g.cols()))});
                const double exitT = w.exitT();
                TALLY(tally, exitT >= lastExit, "exit parameter decreased from " << str(o) << " dir " << str(d));
                lastExit = exitT;
                w.advance();
            }
            TALLY(tally, !visited.empty(), "no cell visited from " << str(o) << " dir " << str(d));
            if (visited.empty()) continue;
            // Independent check: sample points along the ray inside the world; each must lie in the
            // closed box of some visited cell.
            double tEnd = 1e300;
            if (d.x > 0) tEnd = std::min(tEnd, (80 - o.x) / d.x);
            if (d.x < 0) tEnd = std::min(tEnd, (0 - o.x) / d.x);
            if (d.y > 0) tEnd = std::min(tEnd, (60 - o.y) / d.y);
            if (d.y < 0) tEnd = std::min(tEnd, (0 - o.y) / d.y);
            if (!(tEnd > 0 && tEnd < 1e299)) continue;  // starts on the boundary heading out
            for (int k = 0; k <= 400; ++k) {
                const Vec2 p = o + d * (tEnd * k / 400.0);
                bool covered = false;
                for (auto [cx, cy] : visited) covered = covered || inClosedCell(g, cx, cy, p);
                TALLY(tally, covered, "ray from " << str(o) << " dir " << str(d) << " point " << str(p) << " not covered");
            }
        }
    TALLY_CHECK(tally);
    CHECK(rays > 500);
}
