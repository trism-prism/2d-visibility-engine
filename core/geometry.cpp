#include "geometry.h"

#include <algorithm>

namespace vis {

namespace {

// Error-free transformations (round-to-nearest, no overflow/underflow, and no contraction or
// reassociation by the compiler: viscore is built with /fp:precise, -ffp-contract=off):
//   twoSum:  x = fl(a+b) and a+b == x+y exactly (Knuth; valid for any magnitudes).
//   twoProd: x = fl(a*b) and a*b == x+y exactly, because fma computes a*b-x with a single rounding
//            and that value is representable.
inline void twoSum(double a, double b, double& x, double& y) {
    x = a + b;
    const double bv = x - a;
    const double av = x - bv;
    y = (a - av) + (b - bv);
}
inline void twoProd(double a, double b, double& x, double& y) {
    x = a * b;
    y = std::fma(a, b, -x);
}

// Exact sign of (b-a)x(c-a). Each difference is split exactly into hi + lo (twoSum); the product of
// two such sums is four exact twoProd pairs, so det is the exact sum of 16 doubles. They go into a
// non-overlapping expansion (Shewchuk's Grow-Expansion with zero elimination, built on twoSum); its
// components increase in magnitude, so the sign of the whole sum is the sign of the last non-zero one.
int orientExact(Vec2 a, Vec2 b, Vec2 c) {
    double u[2], v[2], w[2], z[2];  // u = b.x-a.x, v = c.y-a.y, w = b.y-a.y, z = c.x-a.x
    twoSum(b.x, -a.x, u[0], u[1]);
    twoSum(c.y, -a.y, v[0], v[1]);
    twoSum(b.y, -a.y, w[0], w[1]);
    twoSum(c.x, -a.x, z[0], z[1]);

    double e[20];
    int n = 0;
    const auto add = [&](double q) {
        int m = 0;
        for (int i = 0; i < n; ++i) {
            double hi, lo;
            twoSum(q, e[i], hi, lo);
            if (lo != 0.0) e[m++] = lo;
            q = hi;
        }
        if (q != 0.0) e[m++] = q;
        n = m;
    };
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            double hi, lo;
            twoProd(u[i], v[j], hi, lo);  // + u*v
            add(hi);
            add(lo);
            twoProd(w[i], z[j], hi, lo);  // - w*z
            add(-hi);
            add(-lo);
        }
    }
    if (n == 0) return 0;
    return e[n - 1] > 0.0 ? 1 : -1;
}

// Shewchuk's ccwerrboundA = (3 + 16 eps) eps with eps = 2^-53: when |det| exceeds this fraction of
// |l|+|r|, the sign of the floating-point det = l - r (l, r from rounded differences and products)
// is guaranteed to be the exact sign.
constexpr double kOrientErrBound = (3.0 + 16.0 * 1.1102230246251565e-16) * 1.1102230246251565e-16;

}  // namespace

int orient(Vec2 a, Vec2 b, Vec2 c) {
    const double l = (b.x - a.x) * (c.y - a.y);
    const double r = (b.y - a.y) * (c.x - a.x);
    const double det = l - r;
    if (std::fabs(det) > kOrientErrBound * (std::fabs(l) + std::fabs(r))) return det > 0.0 ? 1 : -1;
    return orientExact(a, b, c);
}

int halfTurn(Vec2 l, Vec2 p) { return (p.y > l.y || (p.y == l.y && p.x > l.x)) ? 0 : 1; }

bool angleLess(Vec2 l, Vec2 p, Vec2 q) {
    const int hp = halfTurn(l, p);
    const int hq = halfTurn(l, q);
    if (hp != hq) return hp < hq;
    return orient(l, p, q) > 0;
}

void rayHit(const Ray& ray, const Segment& s, double& tMinus, double& tPlus) {
    const Vec2 d = ray.d;
    const Vec2 qa = s.a - ray.o;
    const Vec2 qb = s.b - ray.o;
    const int sa = orient(ray.o, ray.target, s.a);
    const int sb = orient(ray.o, ray.target, s.b);

    if (sa == 0 && sb == 0) return;  // seen end-on
    if (sa == sb) return;            // same strict side

    if (sa != 0 && sb != 0) {
        // Proper crossing: counts for both sides.
        const Vec2 e = s.b - s.a;
        const double dd = dot(d, d);
        double t = cross(qa, e) / cross(d, e);
        // The crossing lies on the wall, so its parameter along the ray lies between those of the
        // wall's ends. For a nearly end-on wall cross(d, e) and cross(qa, e) are rounding noise and t
        // can be non-finite, negative or huge; any of those would skip the wall (light leak) or pass
        // over it. Then interpolate along the wall with the rounded side distances, which always lands
        // inside the wall.
        const double tA = dot(qa, d) / dd;
        const double tB = dot(qb, d) / dd;
        if (!(std::isfinite(t) && t >= std::min(tA, tB) && t <= std::max(tA, tB))) {
            const double fa = std::fabs(cross(d, qa));
            const double fb = std::fabs(cross(d, qb));
            const double u = (fa + fb) > 0.0 ? fa / (fa + fb) : 0.5;
            const Vec2 hit = s.a + e * u;
            t = dot(hit - ray.o, d) / dd;
        }
        if (t * ray.len > kLightClearance) {
            tMinus = std::min(tMinus, t);
            tPlus = std::min(tPlus, t);
        }
        return;
    }

    // Exactly one endpoint Q lies on the ray's line.
    const Vec2 q = (sa == 0) ? qa : qb;
    const int other = (sa == 0) ? sb : sa;
    const double t = dot(q, d) / dot(d, d);
    if (!(t * ray.len > kLightClearance)) return;
    if (other > 0) tPlus = std::min(tPlus, t);
    else tMinus = std::min(tMinus, t);
}

SegHit intersectSegments(const Segment& s, const Segment& t, Vec2& p) {
    const Vec2 r = s.b - s.a;
    const Vec2 u = t.b - t.a;
    const int o1 = orient(s.a, s.b, t.a);
    const int o2 = orient(s.a, s.b, t.b);
    const int o3 = orient(t.a, t.b, s.a);
    const int o4 = orient(t.a, t.b, s.b);

    if (o1 == 0 && o2 == 0) return SegHit::None;  // parallel (and collinear)
    if (o1 == o2) return SegHit::None;            // t strictly on one side of s's line
    if (o3 == o4 && o3 != 0) return SegHit::None;
    if (o3 == 0 && o4 == 0) return SegHit::None;

    // Not parallel and each segment straddles (or touches) the other's line. A zero
    // orientation means that endpoint is exactly the intersection of the two lines.
    if (o1 == 0) { p = t.a; return SegHit::Touch; }
    if (o2 == 0) { p = t.b; return SegHit::Touch; }
    if (o3 == 0) { p = s.a; return SegHit::Touch; }
    if (o4 == 0) { p = s.b; return SegHit::Touch; }

    // Parameters of the crossing along s (k) and along t (m); both must be in [0, 1]. For nearly
    // parallel segments the rounded cross products are noise and the parameters can be non-finite or
    // outside [0, 1]; then use the point of t interpolated from the rounded distances of its ends to
    // s's line, which lies inside t and (up to rounding) on s.
    const double den = cross(r, u);
    const double k = cross(t.a - s.a, u) / den;
    const double m = cross(t.a - s.a, r) / den;
    if (std::isfinite(k) && std::isfinite(m) && k >= 0.0 && k <= 1.0 && m >= 0.0 && m <= 1.0) {
        p = s.a + r * k;
    } else {
        const double fa = std::fabs(cross(r, t.a - s.a));
        const double fb = std::fabs(cross(r, t.b - s.a));
        const double w = (fa + fb) > 0.0 ? fa / (fa + fb) : 0.5;
        p = t.a + u * w;
    }
    return SegHit::Cross;
}

bool segmentOverlapsAABB(const Segment& s, double x0, double y0, double x1, double y1) {
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = s.b.x - s.a.x;
    const double dy = s.b.y - s.a.y;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {s.a.x - x0, x1 - s.a.x, s.a.y - y0, y1 - s.a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0) {
            if (r > t1) return false;
            if (r > t0) t0 = r;
        } else {
            if (r < t0) return false;
            if (r < t1) t1 = r;
        }
    }
    return true;
}

bool clipToRect(const Segment& s, double w, double h, Segment& out) {
    double t0 = 0.0;
    double t1 = 1.0;
    const double dx = s.b.x - s.a.x;
    const double dy = s.b.y - s.a.y;
    // For each of the four edges remember which parameter was set by it, to snap exactly.
    int snap0 = -1;
    int snap1 = -1;
    const double p[4] = {-dx, dx, -dy, dy};
    const double q[4] = {s.a.x, w - s.a.x, s.a.y, h - s.a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return false;
            continue;
        }
        const double r = q[i] / p[i];
        if (p[i] < 0.0) {
            if (r > t1) return false;
            if (r > t0) { t0 = r; snap0 = i; }
        } else {
            if (r < t0) return false;
            if (r < t1) { t1 = r; snap1 = i; }
        }
    }
    if (t0 > t1) return false;

    auto point = [&](double t, int snap) {
        Vec2 v = (t == 0.0) ? s.a : (t == 1.0 ? s.b : Vec2{s.a.x + t * dx, s.a.y + t * dy});
        switch (snap) {
            case 0: v.x = 0.0; break;
            case 1: v.x = w; break;
            case 2: v.y = 0.0; break;
            case 3: v.y = h; break;
            default: break;
        }
        v.x = std::clamp(v.x, 0.0, w);
        v.y = std::clamp(v.y, 0.0, h);
        // Canonicalise -0.0 so that equal points are bitwise equal.
        v.x += 0.0;
        v.y += 0.0;
        return v;
    };
    out.a = point(t0, snap0);
    out.b = point(t1, snap1);
    return true;
}

}  // namespace vis
