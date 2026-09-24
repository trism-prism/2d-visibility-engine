// Seeded differential test (DESIGN section 7, check 4): Brute and Grid must produce bitwise
// identical polygons (== on doubles, same vertex count), for any grid resolution. On failure the
// scene seed, index and light are printed together with the first differing vertex.
#include "test_util.h"

using namespace vis;
using namespace tutil;

namespace {

struct Res {
    int cols, rows;  // 0, 0 = automatic
};
const std::vector<Res> kResolutions = {{0, 0},  {1, 1},  {2, 2},  {4, 4},   {8, 8},    {16, 16},
                                       {64, 64}, {3, 5}, {7, 3},  {13, 13}, {128, 128}, {1, 40}, {40, 1}};

void applyResolution(Scene& s, Res r) {
    if (r.cols == 0) s.setGridAuto();
    else s.setGridResolution(r.cols, r.rows);
}

struct Counters {
    long long comparisons = 0;
    long long vertices = 0;
};
Counters g_total;

// Compares Grid at every resolution in `resolutions` against Brute, for every light.
void compareScene(Scene& scene, const std::vector<Vec2>& lights, const std::vector<Res>& resolutions,
                  const std::string& label) {
    std::vector<Poly> brute;
    VisibilityResult rb;
    for (Vec2 l : lights) {
        computeVisibility(scene, l, Mode::Brute, rb);
        brute.push_back(rb.polygon);
    }
    VisibilityResult rg;
    for (Res res : resolutions) {
        applyResolution(scene, res);
        for (std::size_t i = 0; i < lights.size(); ++i) {
            computeVisibility(scene, lights[i], Mode::Grid, rg);
            const Poly& a = brute[i];
            const Poly& b = rg.polygon;
            ++g_total.comparisons;
            g_total.vertices += static_cast<long long>(a.size());
            bool equal = a.size() == b.size();
            std::size_t firstDiff = 0;
            for (std::size_t k = 0; equal && k < a.size(); ++k) {
                if (!(a[k] == b[k])) { equal = false; firstDiff = k; }
            }
            if (!equal) {
                const std::size_t k = std::min(firstDiff, std::min(a.size(), b.size()) - 1);
                FAIL_CHECK(label << " | grid " << scene.grid().cols() << "x" << scene.grid().rows()
                                 << " | light " << str(lights[i]) << " | vertices brute " << a.size() << " grid "
                                 << b.size() << " | first difference at " << firstDiff << ": brute "
                                 << (k < a.size() ? str(a[k]) : "-") << " grid "
                                 << (k < b.size() ? str(b[k]) : "-"));
            }
        }
    }
}

// Light positions chosen to be awkward for a grid: random, on lattice points (grid corners/lines when
// the resolution divides the world), at wall endpoints and midpoints, on the boundary and corners.
std::vector<Vec2> awkwardLights(Rng& rng, const Scene& s, bool integerLattice) {
    std::vector<Vec2> lights;
    for (int i = 0; i < 4; ++i) lights.push_back({rng.range(0, s.width()), rng.range(0, s.height())});
    if (integerLattice) {
        for (int i = 0; i < 4; ++i) lights.push_back({double(rng.below(65)), double(rng.below(65))});
        lights.push_back({double(8 * rng.below(9)), double(rng.below(65))});   // on a vertical grid line (8x8)
        lights.push_back({double(rng.below(65)), double(16 * rng.below(5))});  // on a horizontal line (4x4)
    }
    if (s.userWallCount() > 0) {
        for (int i = 0; i < 2; ++i) {
            const Segment& w =
                s.walls()[Scene::kBoundaryWalls + static_cast<std::size_t>(rng.below(static_cast<int>(s.userWallCount())))];
            lights.push_back(w.a);
            lights.push_back(w.b);
            lights.push_back({(w.a.x + w.b.x) / 2, (w.a.y + w.b.y) / 2});
        }
    }
    lights.push_back({0, 0});
    lights.push_back({s.width(), s.height()});
    lights.push_back({s.width() / 2, 0});
    lights.push_back({0, rng.range(0, s.height())});
    return lights;
}

std::vector<Res> pickResolutions(Rng& rng, int count) {
    std::vector<Res> r;
    for (int i = 0; i < count; ++i) r.push_back(kResolutions[static_cast<std::size_t>(rng.below(static_cast<int>(kResolutions.size())))]);
    return r;
}

}  // namespace

TEST_CASE("differential: lattice scenes (shared endpoints, T-junctions, collinear, grid lines and corners)") {
    constexpr std::uint64_t kBase = 0xD1FF0000;
    for (int i = 0; i < scaled(300); ++i) {
        const std::uint64_t seed = kBase + static_cast<std::uint64_t>(i);
        Rng rng(seed);
        const int step = (i % 3 == 0) ? 8 : (i % 3 == 1 ? 4 : 1);  // coordinates on 8- / 4-unit lines
        std::vector<Segment> walls = latticeWalls(rng, 2 + rng.below(50), 64 / step, rng.below(2) == 0);
        for (Segment& w : walls) {
            w.a = w.a * double(step);
            w.b = w.b * double(step);
        }
        Scene scene = makeScene(64, 64, walls);
        const std::vector<Vec2> lights = awkwardLights(rng, scene, true);
        compareScene(scene, lights, pickResolutions(rng, 3), "lattice seed " + std::to_string(seed) + " scene " + std::to_string(i));
    }
}

TEST_CASE("differential: random real-valued scenes, crossing-heavy") {
    constexpr std::uint64_t kBase = 0xD1FF1000;
    for (int i = 0; i < scaled(150); ++i) {
        const std::uint64_t seed = kBase + static_cast<std::uint64_t>(i);
        Rng rng(seed);
        const double maxLen = (i % 2) ? 25.0 : 120.0;  // long walls cross a lot
        Scene scene = makeScene(100, 70, realWalls(rng, 5 + rng.below(70), 100, 70, maxLen));
        const std::vector<Vec2> lights = awkwardLights(rng, scene, false);
        compareScene(scene, lights, pickResolutions(rng, 3), "real seed " + std::to_string(seed) + " scene " + std::to_string(i));
    }
}

TEST_CASE("differential: generator scenes at several resolutions") {
    const std::vector<Res> resolutions = {{0, 0}, {1, 1}, {3, 2}, {40, 25}, {200, 200}};
    constexpr std::uint64_t kBase = 0xD1FF2000;
    int index = 0;
    for (SceneKind kind : {SceneKind::Scatter, SceneKind::Maze, SceneKind::LongWalls, SceneKind::Cluster}) {
        for (std::uint64_t k = 0; k < 3; ++k, ++index) {
            const std::uint64_t seed = kBase + static_cast<std::uint64_t>(index);
            Rng rng(seed ^ 0x9E3779B97F4A7C15ull);  // not the generator's own stream
            Scene scene = makeScene(kind, 200, seed);
            const std::vector<Vec2> lights = awkwardLights(rng, scene, false);
            compareScene(scene, lights, resolutions,
                         std::string(sceneKindName(kind)) + " seed " + std::to_string(seed) + " scene " + std::to_string(index));
        }
    }
}

TEST_CASE("differential: demo scene, lights on a dense lattice including grid lines") {
    Scene scene = makeDemoScene();
    std::vector<Vec2> lights;
    for (int y = 0; y <= 1000; y += 125)
        for (int x = 0; x <= 1600; x += 100) lights.push_back({double(x), double(y)});
    Rng rng(0xDE30);
    for (int i = 0; i < 60; ++i) lights.push_back({rng.range(0, 1600), rng.range(0, 1000)});
    lights.push_back(demoLight());
    compareScene(scene, lights, {{0, 0}, {16, 10}, {32, 20}, {1, 1}, {100, 100}, {8, 8}}, "demo seed none");
}

TEST_CASE("differential: coverage summary") {
    MESSAGE("polygon comparisons so far: " << g_total.comparisons << ", brute vertices compared: " << g_total.vertices);
    CHECK(g_total.comparisons > scaled(10000));
}
