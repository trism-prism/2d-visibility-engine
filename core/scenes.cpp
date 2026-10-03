#include "scenes.h"

#include "visibility.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

namespace vis {

std::uint64_t splitmix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

namespace {
std::uint64_t rotl(std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
}  // namespace

Rng::Rng(std::uint64_t seed) {
    std::uint64_t sm = seed;
    for (auto& v : s_) v = splitmix64(sm);
}

std::uint64_t Rng::next() {
    const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
}

double Rng::uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
double Rng::range(double lo, double hi) { return lo + (hi - lo) * uniform(); }
int Rng::below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }

Vec2 Rng::direction() {
    for (;;) {
        const double x = range(-1.0, 1.0);
        const double y = range(-1.0, 1.0);
        const double r2 = x * x + y * y;
        if (r2 > 1e-4 && r2 <= 1.0) {
            const double r = std::sqrt(r2);  // correctly rounded, so identical everywhere
            return {x / r, y / r};
        }
    }
}

const char* sceneKindName(SceneKind kind) {
    switch (kind) {
        case SceneKind::Demo: return "demo";
        case SceneKind::Scatter: return "scatter";
        case SceneKind::Maze: return "maze";
        case SceneKind::LongWalls: return "longwalls";
        case SceneKind::Cluster: return "cluster";
    }
    return "?";
}

namespace {

// Collects accepted walls with the same rules the Scene applies, so generators can aim for an
// exact accepted count. The Scene re-applies the rules (a no-op on already clipped walls).
class Collector {
public:
    Collector(int target, std::uint64_t seed) : rng(seed), target_(static_cast<std::size_t>(std::max(target, 0))) {}
    bool full() const { return walls.size() >= target_; }
    bool add(Vec2 a, Vec2 b) {
        if (full()) return false;
        Segment s;
        if (Scene::sanitizeWall(a, b, kStressWidth, kStressHeight, s) != AddStatus::Added) return false;
        walls.push_back(s);
        return true;
    }
    std::vector<Segment> walls;
    Rng rng;

private:
    std::size_t target_;
};

// Bounded retry loop: keeps calling `step` until the collector is full or attempts run out.
template <class F>
void fill(Collector& c, int target, F&& step) {
    const long long maxAttempts = 100ll * std::max(target, 1) + 1000;
    for (long long i = 0; i < maxAttempts && !c.full(); ++i) step();
}

}  // namespace

static std::vector<Segment> demoWalls() {
    std::vector<Segment> w;
    const auto wall = [&](double x0, double y0, double x1, double y1) { w.push_back({{x0, y0}, {x1, y1}}); };
    const auto square = [&](double cx, double cy, double r) {
        wall(cx - r, cy - r, cx + r, cy - r);
        wall(cx + r, cy - r, cx + r, cy + r);
        wall(cx + r, cy + r, cx - r, cy + r);
        wall(cx - r, cy + r, cx - r, cy - r);
    };
    const auto diamond = [&](double cx, double cy, double r) {
        wall(cx, cy - r, cx + r, cy);
        wall(cx + r, cy, cx, cy + r);
        wall(cx, cy + r, cx - r, cy);
        wall(cx - r, cy, cx, cy - r);
    };

    // West rooms: partition with a door, a divider with a door that shares the partition's corner.
    wall(400, 0, 400, 300);
    wall(400, 400, 400, 650);
    wall(0, 300, 160, 300);
    wall(240, 300, 400, 300);
    // Floor between the west rooms and the south; its right half forms a T-junction with x = 400.
    wall(0, 650, 250, 650);
    wall(330, 650, 700, 650);
    // South-west: a door in x = 700, an L-shaped nook, and a triangle (three shared corners).
    wall(700, 650, 700, 820);
    wall(700, 900, 700, 1000);
    wall(150, 820, 380, 820);
    wall(380, 820, 380, 1000);
    wall(500, 750, 600, 850);
    wall(600, 850, 500, 850);
    wall(500, 850, 500, 750);
    // Central hall: pillars, a diamond, and two crossing diagonals.
    for (double cy : {150.0, 430.0})
        for (double cx : {520.0, 700.0, 880.0}) square(cx, cy, 18);
    diamond(700, 290, 30);
    wall(760, 250, 840, 330);
    wall(760, 330, 840, 250);
    // East rooms: partition with a door, a floor with a door, a zig-zag, and a T-junction.
    wall(1000, 0, 1000, 180);
    wall(1000, 260, 1000, 520);
    wall(1000, 520, 1250, 520);
    wall(1330, 520, 1600, 520);
    wall(1100, 100, 1300, 200);
    wall(1300, 200, 1500, 100);
    wall(1150, 350, 1450, 350);
    wall(1300, 350, 1300, 450);
    // South-east hall: collinear overlapping walls and a large X.
    wall(800, 800, 1000, 800);
    wall(900, 800, 1100, 800);
    wall(1200, 650, 1500, 950);
    wall(1200, 950, 1500, 650);

    return w;
}

Vec2 demoLight() { return {610.0, 290.0}; }

static std::vector<Segment> scatterWalls(int wallCount, std::uint64_t seed) {
    Collector c(wallCount, seed);
    fill(c, wallCount, [&] {
        const Vec2 centre{c.rng.range(0, kStressWidth), c.rng.range(0, kStressHeight)};
        const Vec2 dir = c.rng.direction();
        const double half = 0.5 * c.rng.range(10.0, 50.0);
        c.add(centre - dir * half, centre + dir * half);
    });
    return c.walls;
}

static std::vector<Segment> mazeWalls(int wallCount, std::uint64_t seed) {
    Collector c(wallCount, seed);
    const int diagonals = wallCount >= 20 ? wallCount / 25 : 0;
    const int lattice = wallCount - diagonals;

    // Recursive division of a lattice, one door per wall, breadth-first so rooms stay evenly sized.
    // Walls of one lattice cell get no door, so every division yields at least one wall.
    const double unit = std::sqrt(kStressWidth * kStressHeight / (1.2 * std::max(lattice, 8)));
    const int gx = std::max(2, static_cast<int>(std::ceil(kStressWidth / unit)));
    const int gy = std::max(2, static_cast<int>(std::ceil(kStressHeight / unit)));
    const double cw = kStressWidth / gx;
    const double ch = kStressHeight / gy;
    const auto xl = [&](int i) { return i >= gx ? kStressWidth : i * cw; };
    const auto yl = [&](int j) { return j >= gy ? kStressHeight : j * ch; };

    struct Region { int x0, y0, x1, y1; };
    std::deque<Region> queue{{0, 0, gx, gy}};
    while (!queue.empty() && static_cast<int>(c.walls.size()) < lattice) {
        const Region r = queue.front();
        queue.pop_front();
        const int w = r.x1 - r.x0;
        const int h = r.y1 - r.y0;
        if (w < 2 && h < 2) continue;
        const bool vertical = w >= h;
        const int line = vertical ? r.x0 + 1 + c.rng.below(w - 1) : r.y0 + 1 + c.rng.below(h - 1);
        const int from = vertical ? r.y0 : r.x0;
        const int to = vertical ? r.y1 : r.x1;
        const int len = to - from;
        const auto piece = [&](int i0, int i1) {
            if (i1 <= i0 || static_cast<int>(c.walls.size()) >= lattice) return;
            if (vertical) c.add({xl(line), yl(i0)}, {xl(line), yl(i1)});
            else c.add({xl(i0), yl(line)}, {xl(i1), yl(line)});
        };
        // Several doors per wall (1 to 4 by length, many two cells wide) so rooms connect widely and
        // the light sees through the maze instead of down single-cell corridors. Every wall keeps
        // at least one door, so the lattice stays connected; a one-cell wall gets none.
        std::vector<char> isDoor(static_cast<std::size_t>(std::max(len, 0)), 0);
        const int doors = len >= 14 ? 4 : len >= 8 ? 3 : len >= 4 ? 2 : len >= 2 ? 1 : 0;
        for (int d = 0; d < doors; ++d) {
            const int pos = c.rng.below(len);
            isDoor[static_cast<std::size_t>(pos)] = 1;
            if (len >= 4 && pos + 1 < len && c.rng.below(5) < 3) isDoor[static_cast<std::size_t>(pos + 1)] = 1;
        }
        for (int i = 0; i < len;) {
            if (isDoor[static_cast<std::size_t>(i)]) { ++i; continue; }
            int j = i;
            while (j < len && !isDoor[static_cast<std::size_t>(j)]) ++j;
            piece(from + i, from + j);
            i = j;
        }
        if (vertical) {
            queue.push_back({r.x0, r.y0, line, r.y1});
            queue.push_back({line, r.y0, r.x1, r.y1});
        } else {
            queue.push_back({r.x0, r.y0, r.x1, line});
            queue.push_back({r.x0, line, r.x1, r.y1});
        }
    }

    const long long budget = 100ll * std::max(wallCount, 1) + 1000;
    for (long long i = 0; i < budget && !c.full(); ++i) {
        const Vec2 start{c.rng.range(0, kStressWidth), c.rng.range(0, kStressHeight)};
        const Vec2 dir = c.rng.direction();
        c.add(start, start + dir * c.rng.range(0.15 * kStressWidth, 0.5 * kStressWidth));
    }
    return c.walls;
}

static std::vector<Segment> longWallsWalls(int wallCount, std::uint64_t seed) {
    Collector c(wallCount, seed);
    // A few near-vertical walls cross the many near-horizontal ones; the slope jitter makes
    // neighbouring near-parallel walls cross each other too. At n = 1000 that is roughly 10-30 n
    // crossings instead of the ~n^2/4 two full families would give.
    const int vertical = std::max(2, wallCount / 32);
    const int horizontal = std::max(0, wallCount - vertical);
    const double slope = 0.002;
    int placed = 0;
    fill(c, wallCount, [&] {
        if (placed < horizontal) {
            const double x0 = c.rng.range(0.0, 0.15 * kStressWidth);
            const double x1 = c.rng.range(0.85 * kStressWidth, kStressWidth);
            const double y0 = c.rng.range(0.0, kStressHeight);
            const double y1 = y0 + (x1 - x0) * c.rng.range(-slope, slope);
            if (c.add({x0, y0}, {x1, y1})) ++placed;
        } else {
            const double y0 = c.rng.range(0.0, 0.2 * kStressHeight);
            const double y1 = c.rng.range(0.8 * kStressHeight, kStressHeight);
            const double x0 = c.rng.range(0.0, kStressWidth);
            const double x1 = x0 + (y1 - y0) * c.rng.range(-slope, slope);
            c.add({x0, y0}, {x1, y1});
        }
    });
    return c.walls;
}

static std::vector<Segment> clusterWalls(int wallCount, std::uint64_t seed) {
    Collector c(wallCount, seed);
    // A patch of 5% x 5% of the world: a handful of cells at the default grid resolution.
    const double px = c.rng.range(0.2 * kStressWidth, 0.8 * kStressWidth);
    const double py = c.rng.range(0.2 * kStressHeight, 0.8 * kStressHeight);
    const double hx = 0.025 * kStressWidth;
    const double hy = 0.025 * kStressHeight;
    fill(c, wallCount, [&] {
        const Vec2 start{c.rng.range(px - hx, px + hx), c.rng.range(py - hy, py + hy)};
        c.add(start, start + c.rng.direction() * c.rng.range(4.0, 16.0));
    });
    return c.walls;
}

namespace {
Scene sceneFrom(const std::vector<Segment>& walls) {
    Scene s(kStressWidth, kStressHeight);
    s.setWalls(walls);  // one rebuild
    return s;
}
}  // namespace

Scene makeDemoScene() { return sceneFrom(demoWalls()); }
Scene makeScatterScene(int wallCount, std::uint64_t seed) { return sceneFrom(scatterWalls(wallCount, seed)); }
Scene makeMazeScene(int wallCount, std::uint64_t seed) { return sceneFrom(mazeWalls(wallCount, seed)); }
Scene makeLongWallsScene(int wallCount, std::uint64_t seed) { return sceneFrom(longWallsWalls(wallCount, seed)); }
Scene makeClusterScene(int wallCount, std::uint64_t seed) { return sceneFrom(clusterWalls(wallCount, seed)); }

std::vector<Segment> makeSceneWalls(SceneKind kind, int wallCount, std::uint64_t seed) {
    switch (kind) {
        case SceneKind::Demo: return demoWalls();
        case SceneKind::Scatter: return scatterWalls(wallCount, seed);
        case SceneKind::Maze: return mazeWalls(wallCount, seed);
        case SceneKind::LongWalls: return longWallsWalls(wallCount, seed);
        case SceneKind::Cluster: return clusterWalls(wallCount, seed);
    }
    return {};
}

Scene makeScene(SceneKind kind, int wallCount, std::uint64_t seed) { return sceneFrom(makeSceneWalls(kind, wallCount, seed)); }

}  // namespace vis

namespace vis {

namespace {
double distToWall(Vec2 p, const Segment& s) {
    const Vec2 e = s.b - s.a;
    const double l2 = dot(e, e);
    double t = l2 > 0.0 ? dot(p - s.a, e) / l2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return length(p - (s.a + e * t));
}
double polygonArea(const std::vector<Vec2>& poly) {
    double a = 0.0;
    for (std::size_t i = 0; i < poly.size(); ++i) a += cross(poly[i], poly[(i + 1) % poly.size()]);
    return 0.5 * a;
}
}  // namespace

Vec2 chooseLight(const Scene& scene, Vec2 preferred) {
    std::vector<Vec2> candidates{preferred};
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) candidates.push_back({preferred.x + 70.0 * dx, preferred.y + 70.0 * dy});
    VisibilityResult r;
    Vec2 best = preferred;
    double bestArea = -1.0;
    for (const Vec2 p : candidates) {
        if (p.x < 0 || p.y < 0 || p.x > scene.width() || p.y > scene.height()) continue;
        bool open = true;
        for (std::size_t i = Scene::kBoundaryWalls; i < scene.walls().size() && open; ++i)
            open = distToWall(p, scene.walls()[i]) >= 8.0;
        if (!open) continue;
        computeVisibility(scene, p, Mode::Grid, r);
        const double area = polygonArea(r.polygon);
        if (area > bestArea) {
            bestArea = area;
            best = p;
        }
    }
    return best;
}

}  // namespace vis
