#pragma once
// Test-only helpers. Everything that serves as an oracle here is written independently of
// core/geometry (no rayHit, no grid, no intersectSegments): plain orientation tests and plain
// parametric ray/segment algebra.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "scene.h"
#include "scenes.h"
#include "visibility.h"

#ifndef VIS_TEST_SCALE
#define VIS_TEST_SCALE 1
#endif

namespace tutil {

// Iteration / sample counts are divided by VIS_TEST_SCALE (1 in release; larger in the slow debug
// and sanitizer builds). Every test case still runs; only the number of random trials shrinks.
inline int scaled(int n) { return std::max(1, n / VIS_TEST_SCALE); }

using vis::Mode;
using vis::Scene;
using vis::Segment;
using vis::Vec2;
using Poly = std::vector<Vec2>;

inline std::string str(Vec2 p) {
    std::ostringstream o;
    o.precision(17);
    o << "(" << p.x << ", " << p.y << ")";
    return o.str();
}

inline Scene makeScene(double w, double h, const std::vector<Segment>& walls) {
    Scene s(w, h);
    s.setWalls(walls);
    return s;
}

inline Poly query(const Scene& s, Vec2 light, Mode mode) {
    vis::VisibilityResult r;
    vis::computeVisibility(s, light, mode, r);
    return r.polygon;
}

inline double signedArea(const Poly& p) {
    double a = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Vec2 u = p[i];
        const Vec2 v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return 0.5 * a;
}

inline bool near(Vec2 a, Vec2 b, double eps) { return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps; }

// Removes repeated vertices and vertices that continue straight on (forward collinear). A vertex that
// reverses direction (a spike) is kept on purpose: an analytic expectation has none.
inline Poly simplify(Poly p, double eps) {
    bool changed = true;
    while (changed && p.size() > 2) {
        changed = false;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const Vec2 prev = p[(i + p.size() - 1) % p.size()];
            const Vec2 cur = p[i];
            const Vec2 next = p[(i + 1) % p.size()];
            const Vec2 a = cur - prev;
            const Vec2 b = next - cur;
            const double la = std::sqrt(a.x * a.x + a.y * a.y);
            const double lb = std::sqrt(b.x * b.x + b.y * b.y);
            const bool duplicate = la <= eps;
            // distance of cur from the line prev-next, direction preserved
            const bool straight = !duplicate && lb > eps && std::fabs(a.x * b.y - a.y * b.x) <= eps * la &&
                                  (a.x * b.x + a.y * b.y) > 0.0;
            if (duplicate || straight || lb <= eps) {
                p.erase(p.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return p;
}

// Equal as cyclic vertex sequences (same orientation), element-wise within eps.
inline bool sameCycle(const Poly& a, const Poly& b, double eps) {
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    for (std::size_t shift = 0; shift < a.size(); ++shift) {
        bool ok = true;
        for (std::size_t i = 0; i < a.size() && ok; ++i) ok = near(a[(i + shift) % a.size()], b[i], eps);
        if (ok) return true;
    }
    return false;
}

inline std::string describe(const Poly& p) {
    std::string s;
    for (Vec2 v : p) s += str(v) + " ";
    return s;
}

// ---- independent oracle ----------------------------------------------------------------------

inline int orientSign(Vec2 a, Vec2 b, Vec2 c) {
    const double v = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    return (v > 0) - (v < 0);
}

// True iff the open segments p-q and a-b cross at a single interior point of both.
inline bool properlyCross(Vec2 p, Vec2 q, Vec2 a, Vec2 b) {
    return orientSign(p, q, a) * orientSign(p, q, b) < 0 && orientSign(a, b, p) * orientSign(a, b, q) < 0;
}

// "q is lit": no wall properly crosses the open segment light->q. A light on a wall interior (or
// on a wall's line) gives orientation 0 for that wall, so it never blocks: the same semantics as
// DESIGN section 2 (the light shines to both sides of a wall it sits on).
inline bool litByDefinition(const Scene& s, Vec2 light, Vec2 q) {
    for (const Segment& w : s.walls())
        if (properlyCross(light, q, w.a, w.b)) return false;
    return true;
}

// Even-odd point in polygon (crossing number against a +x ray).
inline bool pointInPolygon(const Poly& poly, Vec2 q) {
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec2 a = poly[i];
        const Vec2 b = poly[j];
        if ((a.y > q.y) != (b.y > q.y)) {
            const double x = (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x;
            if (q.x < x) inside = !inside;
        }
    }
    return inside;
}

inline double distToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 e = b - a;
    const double l2 = e.x * e.x + e.y * e.y;
    double t = l2 > 0 ? ((p.x - a.x) * e.x + (p.y - a.y) * e.y) / l2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const Vec2 c{a.x + t * e.x, a.y + t * e.y};
    return std::sqrt((p.x - c.x) * (p.x - c.x) + (p.y - c.y) * (p.y - c.y));
}

inline double distToBoundary(const Poly& poly, Vec2 q) {
    double best = 1e300;
    for (std::size_t i = 0; i < poly.size(); ++i)
        best = std::min(best, distToSegment(q, poly[i], poly[(i + 1) % poly.size()]));
    return best;
}

// Distance from `light` along unit direction (c, s) to the nearest wall; 0 if the ray leaves the
// world (light on the boundary looking outward). Plain parametric algebra, hits at t <= 1e-9 ignored.
inline double radialDistance(const Scene& scene, Vec2 light, double c, double s) {
    double best = 1e300;
    for (const Segment& w : scene.walls()) {
        const Vec2 e = w.b - w.a;
        const double den = c * e.y - s * e.x;
        if (std::fabs(den) < 1e-300) continue;
        const Vec2 r = w.a - light;
        const double t = (r.x * e.y - r.y * e.x) / den;
        const double u = (r.x * s - r.y * c) / den;
        if (t > 1e-9 && u >= 0.0 && u <= 1.0) best = std::min(best, t);
    }
    return best > 1e299 ? 0.0 : best;
}

// ---- random scenes -----------------------------------------------------------------------------

// Integer-lattice walls: many shared endpoints, T-junctions, collinear overlaps, axis-aligned and
// 45-degree walls, walls on grid lines.
inline std::vector<Segment> latticeWalls(vis::Rng& rng, int n, int maxCoord, bool axisHeavy) {
    std::vector<Segment> walls;
    for (int i = 0; i < n; ++i) {
        const double x0 = rng.below(maxCoord + 1);
        const double y0 = rng.below(maxCoord + 1);
        double x1 = rng.below(maxCoord + 1);
        double y1 = rng.below(maxCoord + 1);
        if (axisHeavy) {
            switch (rng.below(4)) {
                case 0: y1 = y0; break;
                case 1: x1 = x0; break;
                case 2: x1 = x0 + (y1 - y0); break;
                default: break;
            }
        }
        walls.push_back({{x0, y0}, {x1, y1}});
    }
    return walls;
}

inline std::vector<Segment> realWalls(vis::Rng& rng, int n, double w, double h, double maxLen) {
    std::vector<Segment> walls;
    for (int i = 0; i < n; ++i) {
        const Vec2 a{rng.range(0, w), rng.range(0, h)};
        walls.push_back({a, a + rng.direction() * rng.range(1.0, maxLen)});
    }
    return walls;
}

}  // namespace tutil
