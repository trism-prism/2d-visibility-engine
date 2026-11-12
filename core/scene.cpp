#include "scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vis {

namespace {
using Clock = std::chrono::steady_clock;
double microsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}
}  // namespace

Scene::Scene(double width, double height) : w_(width), h_(height) {
    walls_ = {
        {{0.0, 0.0}, {w_, 0.0}},   // bottom
        {{w_, 0.0}, {w_, h_}},     // right
        {{w_, h_}, {0.0, h_}},     // top
        {{0.0, h_}, {0.0, 0.0}},   // left
    };
    rebuild();
}

AddStatus Scene::sanitizeWall(Vec2 a, Vec2 b, double w, double h, Segment& out) {
    if (!isFinite(a) || !isFinite(b)) return AddStatus::RejectedNonFinite;
    Segment clipped;
    if (!clipToRect({a, b}, w, h, clipped)) return AddStatus::RejectedOutside;
    if (length(clipped.b - clipped.a) < kMinWallLength) return AddStatus::RejectedTooShort;
    out = clipped;
    return AddStatus::Added;
}

AddStatus Scene::addWall(Vec2 a, Vec2 b) {
    Segment s;
    const AddStatus st = sanitizeWall(a, b, w_, h_, s);
    if (st != AddStatus::Added) return st;
    walls_.push_back(s);
    rebuild();
    return st;
}

bool Scene::removeWall(std::size_t index) {
    if (index < kBoundaryWalls || index >= walls_.size()) return false;
    walls_.erase(walls_.begin() + static_cast<std::ptrdiff_t>(index));
    rebuild();
    return true;
}

void Scene::clear() {
    walls_.resize(kBoundaryWalls);
    rebuild();
}

std::size_t Scene::setWalls(std::span<const Segment> userWalls) {
    walls_.resize(kBoundaryWalls);
    walls_.reserve(kBoundaryWalls + userWalls.size());
    std::size_t accepted = 0;
    for (const Segment& s : userWalls) {
        Segment c;
        if (sanitizeWall(s.a, s.b, w_, h_, c) == AddStatus::Added) {
            walls_.push_back(c);
            ++accepted;
        }
    }
    rebuild();
    return accepted;
}

void Scene::setGridResolution(int cols, int rows) {
    autoGrid_ = false;
    cols_ = std::clamp(cols, 1, kMaxGridDim);
    rows_ = std::clamp(rows, 1, kMaxGridDim);
    rebuild();
}

void Scene::setGridAuto() {
    autoGrid_ = true;
    rebuild();
}

void Scene::autoResolution(std::size_t wallCount, double w, double h, int& cols, int& rows) {
    const double cells = std::max<double>(static_cast<double>(wallCount), 16.0);
    const double side = std::sqrt(w * h / cells);
    cols = std::clamp(static_cast<int>(std::lround(w / side)), 1, kMaxGridDim);
    rows = std::clamp(static_cast<int>(std::lround(h / side)), 1, kMaxGridDim);
}

void Scene::rebuild() {
    ++revision_;
    stats_ = {};
    stats_.walls = walls_.size();

    auto t0 = Clock::now();
    if (autoGrid_) autoResolution(walls_.size(), w_, h_, cols_, rows_);
    grid_.build(walls_, w_, h_, cols_, rows_);
    stats_.gridBuildUs = microsSince(t0);
    stats_.gridCells = grid_.cellCount();
    stats_.registrations = grid_.registrations();

    t0 = Clock::now();
    // Every wall pair that shares a cell is tested once: wall i only looks at j > i, and a stamp
    // per wall skips pairs seen through another shared cell.
    intersections_.clear();
    std::vector<std::int64_t> seen(walls_.size(), -1);
    for (std::size_t i = 0; i < walls_.size(); ++i) {
        for (std::uint32_t c : grid_.wallCells(i)) {
            for (std::uint32_t j : grid_.cellWalls(c)) {
                if (j <= i || seen[j] == static_cast<std::int64_t>(i)) continue;
                seen[j] = static_cast<std::int64_t>(i);
                ++stats_.pairsTested;
                Vec2 p;
                if (intersectSegments(walls_[i], walls_[j], p) == SegHit::Cross) intersections_.push_back(p);
            }
        }
    }
    stats_.intersections = intersections_.size();

    targets_.clear();
    targets_.reserve(walls_.size() * 2 + intersections_.size());
    for (const Segment& s : walls_) {
        targets_.push_back(s.a);
        targets_.push_back(s.b);
    }
    targets_.insert(targets_.end(), intersections_.begin(), intersections_.end());
    std::sort(targets_.begin(), targets_.end(),
              [](Vec2 p, Vec2 q) { return p.x < q.x || (p.x == q.x && p.y < q.y); });
    targets_.erase(std::unique(targets_.begin(), targets_.end()), targets_.end());
    stats_.targets = targets_.size();
    stats_.intersectUs = microsSince(t0);
}

}  // namespace vis
