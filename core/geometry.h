#pragma once
#include <cmath>
#include <limits>

namespace vis {

// Tolerances, one per purpose (DESIGN section 2). None is used inside a comparator.
constexpr double kMinWallLength = 1e-6;     // input validation: shorter clipped walls are rejected
constexpr double kLightClearance = 1e-9;    // a wall closer than this along a ray does not occlude it
constexpr double kGridPad = 1e-6;           // conservative cell registration, relative to cell size
// Input coordinates beyond this magnitude are rejected (differences and products in clipping and the
// exact predicates would overflow far beyond it).
constexpr double kMaxCoordinate = 1e150;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(double s, Vec2 a) { return {a.x * s, a.y * s}; }
inline bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }

inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double length(Vec2 a) { return std::sqrt(dot(a, a)); }
inline bool isFinite(Vec2 a) { return std::isfinite(a.x) && std::isfinite(a.y); }

struct Segment {
    Vec2 a;
    Vec2 b;
};
inline bool operator==(const Segment& s, const Segment& t) { return s.a == t.a && s.b == t.b; }

// Exact sign (-1, 0, +1) of (b-a) x (c-a) evaluated on the given doubles: +1 when c is left of the
// directed line a->b. Floating-point filter first, error-free expansion fallback (see geometry.cpp).
// This is the only source of combinatorial decisions (angular order, side of a ray).
int orient(Vec2 a, Vec2 b, Vec2 c);

// Counter-clockwise order of directions around `l` as a strict weak ordering: first the half-turn
// (p.y > l.y, or p.y == l.y && p.x > l.x, comes first), then orient(l, p, q) > 0. Targets equal to
// `l` must be excluded by the caller. Equivalent elements lie on the same ray exactly.
int halfTurn(Vec2 l, Vec2 p);
bool angleLess(Vec2 l, Vec2 p, Vec2 q);

// The ray from `o` through `target`. d = target - o is the unnormalised direction; parameters t
// are in units of d, so the target sits at exactly t == 1. Side tests use the points (o, target),
// not the rounded d.
struct Ray {
    Vec2 o;
    Vec2 target;
    Vec2 d;
    double len = 0.0;  // |d|
};
inline Ray makeRay(Vec2 o, Vec2 target) {
    const Vec2 d = target - o;
    return {o, target, d, length(d)};
}

// One-sided ray/segment test (DESIGN section 3). Lowers tMinus (limit of the nearest hit as the
// direction approaches from the clockwise side) and tPlus (counter-clockwise side) if the segment
// is hit. Both are expected to start at kInf.
void rayHit(const Ray& ray, const Segment& s, double& tMinus, double& tPlus);

enum class SegHit {
    None,   // disjoint, or parallel (collinear overlap contributes no point)
    Touch,  // an endpoint of one lies on the other: `p` is that endpoint, already a target
    Cross   // proper crossing: `p` is the new intersection point
};
// Closed-segment intersection decided by exact orientation tests (the same arithmetic as rayHit).
SegHit intersectSegments(const Segment& s, const Segment& t, Vec2& p);

// Closed-box overlap (inclusive).
bool segmentOverlapsAABB(const Segment& s, double x0, double y0, double x1, double y1);

// Liang-Barsky clip to [0,w] x [0,h]. Clipped coordinates are snapped exactly onto the boundary
// they were clipped against. Returns false if the segment lies entirely outside.
bool clipToRect(const Segment& s, double w, double h, Segment& out);

}  // namespace vis
