#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "geometry.h"
#include "scene.h"

namespace vis {

enum class Mode { Brute, Grid };

struct VisibilityStats {
    std::uint64_t raysCast = 0;      // distinct rays after collapsing identical angles
    std::uint64_t wallTests = 0;     // actual rayHit calls
    std::uint64_t cellsVisited = 0;  // grid cells walked (0 in Brute mode)
    std::size_t targets = 0;         // scene targets
    std::size_t outputVertices = 0;
    double sortUs = 0.0;             // keying, sorting, collapsing runs
    double castUs = 0.0;             // ray casting + emission
    double totalUs = 0.0;
};

// Filled from the real query code. Flat storage: ray r owns cells[cellBegin, cellEnd) (in visiting
// order) and walls[wallBegin, wallEnd) (in testing order). In Brute mode every ray tests every wall,
// so no per-ray lists are stored (they would be T * n entries): `allWallsTested` is set instead and
// the ranges are empty.
struct Trace {
    struct RayRecord {
        Vec2 target;  // the target this ray was built for
        Vec2 dir;     // target - light (the unnormalised d)
        double tMinus = kInf;
        double tPlus = kInf;
        std::uint32_t cellBegin = 0, cellEnd = 0;
        std::uint32_t wallBegin = 0, wallEnd = 0;
    };
    Vec2 light;
    Mode mode = Mode::Grid;
    bool allWallsTested = false;  // true for Brute: wall lists are not recorded
    std::vector<RayRecord> rays;
    std::vector<std::uint32_t> cells;
    std::vector<std::uint32_t> walls;
    std::vector<std::uint32_t> cellVisits;  // per grid cell: how many rays walked it
    void clear() {
        allWallsTested = false;
        rays.clear();
        cells.clear();
        walls.clear();
        cellVisits.clear();
    }
};

// Scratch buffers kept between queries so steady-state calls do not allocate.
struct VisibilityContext {
    struct SortItem {
        std::uint32_t target;
        int half;  // halfTurn(light, target), cached for the comparator
    };
    std::vector<SortItem> items;
    std::vector<Ray> rays;
    std::vector<std::uint32_t> stamp;  // wall index -> id of the last ray that tested it
    std::uint32_t stampCounter = 0;
};

struct VisibilityResult {
    Vec2 light;                 // the light as used (clamped into the world)
    std::vector<Vec2> polygon;  // counter-clockwise, star-shaped around `light`
    VisibilityStats stats;
    VisibilityContext workspace;  // reuse the same result object across queries
};

// A non-finite light yields an empty polygon. Otherwise the light is clamped to the closed world
// rectangle. Where a ray's side sees outside the world (light on the boundary, ray along it) the
// boundary of the visible region on that side is the light itself.
void computeVisibility(const Scene& scene, Vec2 light, Mode mode, VisibilityResult& out, Trace* trace = nullptr);

}  // namespace vis
