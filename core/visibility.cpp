#include "visibility.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vis {

namespace {

using Clock = std::chrono::steady_clock;
double microsBetween(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::micro>(b - a).count();
}

struct Caster {
    const Scene& scene;
    Vec2 light;
    Mode mode;
    VisibilityContext& ctx;
    VisibilityStats& stats;
    Trace* trace;

    void cast(const Ray& ray, double& tMinus, double& tPlus) {
        const std::vector<Segment>& walls = scene.walls();
        tMinus = kInf;
        tPlus = kInf;
        if (mode == Mode::Brute) {
            for (std::size_t w = 0; w < walls.size(); ++w) {
                rayHit(ray, walls[w], tMinus, tPlus);
            }
            stats.wallTests += walls.size();
            return;
        }

        const Grid& grid = scene.grid();
        const std::uint32_t id = ++ctx.stampCounter;
        // The side tests are exact on the points (light, target); the walk follows the rounded d.
        // The two lines differ by far less than kGridPad * cellSize, which registration absorbs.
        GridWalker walker(grid, light, ray.d);
        while (walker.inside()) {
            const std::size_t c = walker.cell();
            ++stats.cellsVisited;
            if (trace) {
                trace->cells.push_back(static_cast<std::uint32_t>(c));
                ++trace->cellVisits[c];
            }
            for (std::uint32_t w : grid.cellWalls(c)) {
                if (ctx.stamp[w] == id) continue;
                ctx.stamp[w] = id;
                ++stats.wallTests;
                if (trace) trace->walls.push_back(w);
                rayHit(ray, walls[w], tMinus, tPlus);
            }
            // Walls registered in later cells cannot be hit before the ray leaves this one.
            const double exitT = walker.exitT();
            if (tMinus <= exitT && tPlus <= exitT) break;
            walker.advance();
        }
    }
};

}  // namespace

void computeVisibility(const Scene& scene, Vec2 lightIn, Mode mode, VisibilityResult& out, Trace* trace) {
    const auto t0 = Clock::now();
    out.polygon.clear();
    out.stats = {};
    VisibilityContext& ctx = out.workspace;

    if (!isFinite(lightIn)) {
        out.light = lightIn;
        if (trace) {
            trace->clear();
            trace->light = lightIn;
            trace->mode = mode;
        }
        return;
    }
    const Vec2 light{std::clamp(lightIn.x, 0.0, scene.width()), std::clamp(lightIn.y, 0.0, scene.height())};
    out.light = light;

    // Exact angular sort (no keys) and collapse of exactly collinear runs.
    const std::vector<Vec2>& targets = scene.targets();
    // Upper bounds (one ray per target, at most two vertices per ray): after the first query on a
    // scene, later queries never allocate, wherever the light is.
    ctx.items.reserve(targets.size());
    ctx.rays.reserve(targets.size());
    out.polygon.reserve(2 * targets.size() + 2);
    ctx.items.clear();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const Vec2 d = targets[i] - light;
        if (length(d) < kLightClearance) continue;  // also excludes target == light
        ctx.items.push_back({static_cast<std::uint32_t>(i), halfTurn(light, targets[i]), d.x, d.y});
    }
    std::sort(ctx.items.begin(), ctx.items.end(),
              [&](const VisibilityContext::SortItem& a, const VisibilityContext::SortItem& b) {
                  if (a.half != b.half) return a.half < b.half;
                  // Same arithmetic as the filter inside orient(light, a, b), on cached differences.
                  // Only an inconclusive filter pays for the call; the answer is the exact sign either way.
                  const double l = a.dx * b.dy, r = a.dy * b.dx;
                  const double det = l - r;
                  if (std::fabs(det) > kOrientErrBound * (std::fabs(l) + std::fabs(r))) return det > 0.0;
                  return orient(light, targets[a.target], targets[b.target]) > 0;
              });
    ctx.rays.clear();
    for (std::size_t i = 0; i < ctx.items.size();) {
        // A run is every following item exactly collinear with the first (same half-turn, orient == 0).
        // Its ray is aimed at the nearest member (ties: lowest target index) so the result does not
        // depend on the unspecified order std::sort leaves inside a run.
        const Vec2 first = targets[ctx.items[i].target];
        std::size_t best = i;
        double bestD2 = dot(first - light, first - light);
        std::size_t j = i + 1;
        for (; j < ctx.items.size() && ctx.items[j].half == ctx.items[i].half &&
               orient(light, first, targets[ctx.items[j].target]) == 0;
             ++j) {
            const Vec2 q = targets[ctx.items[j].target] - light;
            const double d2 = dot(q, q);
            if (d2 < bestD2 || (d2 == bestD2 && ctx.items[j].target < ctx.items[best].target)) {
                best = j;
                bestD2 = d2;
            }
        }
        ctx.rays.push_back(makeRay(light, targets[ctx.items[best].target]));
        i = j;
    }
    const auto t1 = Clock::now();

    // Per-query state.
    if (ctx.stamp.size() != scene.wallCount()) ctx.stamp.assign(scene.wallCount(), 0);
    if (ctx.stampCounter > 0xFFFFFFFFu - static_cast<std::uint32_t>(ctx.rays.size()) - 1u) {
        std::fill(ctx.stamp.begin(), ctx.stamp.end(), 0u);
        ctx.stampCounter = 0;
    }
    if (trace) {
        trace->clear();
        trace->light = light;
        trace->mode = mode;
        trace->allWallsTested = mode == Mode::Brute;
        trace->cellVisits.assign(mode == Mode::Grid ? scene.grid().cellCount() : 0, 0u);
        trace->rays.reserve(ctx.rays.size());
    }

    Caster caster{scene, light, mode, ctx, out.stats, trace};
    std::vector<Vec2>& poly = out.polygon;
    const auto emit = [&](const Ray& ray, double t) {
        // An infinite side looks outside the world: the visible boundary there is the light itself.
        const Vec2 v = std::isinf(t) ? light : Vec2{light.x + ray.d.x * t, light.y + ray.d.y * t};
        if (poly.empty() || v != poly.back()) poly.push_back(v);
    };
    for (const Ray& ray : ctx.rays) {
        Trace::RayRecord rec;
        if (trace) {
            rec.cellBegin = static_cast<std::uint32_t>(trace->cells.size());
            rec.wallBegin = static_cast<std::uint32_t>(trace->walls.size());
        }
        double tMinus, tPlus;
        caster.cast(ray, tMinus, tPlus);
        emit(ray, tMinus);
        if (tPlus != tMinus) emit(ray, tPlus);
        if (trace) {
            rec.target = ray.target;
            rec.dir = ray.d;
            rec.tMinus = tMinus;
            rec.tPlus = tPlus;
            rec.cellEnd = static_cast<std::uint32_t>(trace->cells.size());
            rec.wallEnd = static_cast<std::uint32_t>(trace->walls.size());
            trace->rays.push_back(rec);
        }
    }
    if (poly.size() > 1 && poly.front() == poly.back()) poly.pop_back();
    const auto t2 = Clock::now();

    out.stats.raysCast = ctx.rays.size();
    out.stats.targets = targets.size();
    out.stats.outputVertices = poly.size();
    out.stats.sortUs = microsBetween(t0, t1);
    out.stats.castUs = microsBetween(t1, t2);
    out.stats.totalUs = microsBetween(t0, t2);
}

}  // namespace vis
