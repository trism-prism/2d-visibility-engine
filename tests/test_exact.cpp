// Exact orientation predicate and the angular comparator built on it (DESIGN section 3).
// The reference for orient() is integer arithmetic: every input is chosen so that the determinant is
// an exact integer combination (small integers times powers of two, or doubles in [1,2) which are
// 1 + k * 2^-52 with integer k), evaluated with 128-bit products.
#include <cstdlib>

#include "test_util.h"

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

using namespace vis;
using namespace tutil;

namespace {

struct I128 {
    long long hi;
    unsigned long long lo;
};

I128 mul128(long long a, long long b) {
#if defined(_MSC_VER) && defined(_M_X64)
    I128 r;
    r.lo = static_cast<unsigned long long>(_mul128(a, b, &r.hi));
    return r;
#else
    const __int128 p = static_cast<__int128>(a) * b;
    return {static_cast<long long>(p >> 64), static_cast<unsigned long long>(p)};
#endif
}

int cmp128(I128 x, I128 y) {
    if (x.hi != y.hi) return x.hi < y.hi ? -1 : 1;
    if (x.lo != y.lo) return x.lo < y.lo ? -1 : 1;
    return 0;
}

// sign of (b-a)x(c-a) for integer points, |coordinate differences| < 2^62 / enough for the products.
int refOrient(const long long a[2], const long long b[2], const long long c[2]) {
    return cmp128(mul128(b[0] - a[0], c[1] - a[1]), mul128(b[1] - a[1], c[0] - a[0]));
}

// double in [1,2) with the integer mantissa k: exactly 1 + k * 2^-52
double lat(long long k) { return 1.0 + std::ldexp(static_cast<double>(k), -52); }
Vec2 latPoint(const long long p[2]) { return {lat(p[0]), lat(p[1])}; }

}  // namespace

TEST_CASE("orient: small integers times powers of two, exact int64 reference") {
    Rng rng(0x0123);
    for (int i = 0; i < scaled(200000); ++i) {
        const int e = rng.below(41) - 20;
        const double s = std::ldexp(1.0, e);
        long long p[3][2];
        for (auto& q : p) { q[0] = rng.below(2001) - 1000; q[1] = rng.below(2001) - 1000; }
        if (i % 3 == 0) {  // force exact collinearity: c = a + m (b - a)
            const int m = rng.below(7) - 3;
            p[2][0] = p[0][0] + m * (p[1][0] - p[0][0]);
            p[2][1] = p[0][1] + m * (p[1][1] - p[0][1]);
        }
        const Vec2 a{p[0][0] * s, p[0][1] * s}, b{p[1][0] * s, p[1][1] * s}, c{p[2][0] * s, p[2][1] * s};
        const int expect = refOrient(p[0], p[1], p[2]);
        if (orient(a, b, c) != expect) FAIL_CHECK("small-int mismatch at " << i << " e=" << e << " expected " << expect);
    }
}

TEST_CASE("orient: doubles in [1,2) as 2^-52 lattice, collinear and 1-ulp-perturbed triples") {
    Rng rng(0x0456);
    const long long span = 1ll << 52;
    long long zeros = 0, nonzeros = 0;
    for (int i = 0; i < scaled(200000); ++i) {
        long long p[3][2];
        switch (i % 4) {
            case 0:  // arbitrary
                for (auto& q : p) { q[0] = static_cast<long long>(rng.next() % span); q[1] = static_cast<long long>(rng.next() % span); }
                break;
            case 1: {  // exactly collinear: a + m * step
                const long long step[2] = {static_cast<long long>(rng.below(1 << 20)) - (1 << 19), static_cast<long long>(rng.below(1 << 20)) - (1 << 19)};
                p[0][0] = (1ll << 50) + static_cast<long long>(rng.next() % (1ll << 50));
                p[0][1] = (1ll << 50) + static_cast<long long>(rng.next() % (1ll << 50));
                const long long m1 = rng.below(200) - 100, m2 = rng.below(200) - 100;
                for (int k = 0; k < 2; ++k) {
                    p[1][k] = p[0][k] + m1 * step[k];
                    p[2][k] = p[0][k] + m2 * step[k];
                }
                break;
            }
            default: {  // nearly collinear: c rounded onto the line a->b, then each coordinate moved by +-1 ulp
                for (auto& q : p) { q[0] = (1ll << 50) + static_cast<long long>(rng.next() % (1ll << 50)); q[1] = (1ll << 50) + static_cast<long long>(rng.next() % (1ll << 50)); }
                const double s = rng.range(-1.0, 2.0);
                for (int k = 0; k < 2; ++k) p[2][k] = p[0][k] + std::llround(s * static_cast<double>(p[1][k] - p[0][k]));
                if (i % 4 == 3) {
                    for (auto& q : p)
                        for (int k = 0; k < 2; ++k) q[k] += rng.below(3) - 1;  // 0 or +-1 ulp
                }
                break;
            }
        }
        const Vec2 a = latPoint(p[0]), b = latPoint(p[1]), c = latPoint(p[2]);
        // sanity: the +-1 steps above are exactly nextafter neighbours
        CHECK(std::nextafter(a.x, 2.0) == lat(p[0][0] + 1));
        const int expect = refOrient(p[0], p[1], p[2]);
        (expect == 0 ? zeros : nonzeros)++;
        if (orient(a, b, c) != expect) FAIL_CHECK("lattice mismatch at " << i << " kind " << i % 4 << " expected " << expect);
    }
    CHECK(zeros > scaled(40000));    // many exact zeros: the filter must not report a sign for them
    CHECK(nonzeros > scaled(100000));
}

TEST_CASE("orient: classic near-collinear sweep (Kettner et al.)") {
    // a=(0.5,0.5), b=(12,12), c=(0.5 + i u, 0.5 + j u), u = 2^-53 (the spacing of doubles in [0.5,1)).
    // det = 11.5 * u * (j - i): the exact sign is sign(j - i), a rounded determinant cannot see this.
    const double u = std::ldexp(1.0, -53);
    const Vec2 a{0.5, 0.5};
    for (const Vec2 b : {Vec2{12, 12}, Vec2{24, 24}, Vec2{1, 1}}) {
        for (int i = 0; i < 256; ++i)
            for (int j = 0; j < 256; ++j) {
                const Vec2 c{0.5 + i * u, 0.5 + j * u};
                const int expect = (j > i) - (j < i);
                if (orient(a, b, c) != expect) FAIL_CHECK("sweep i=" << i << " j=" << j << " b=" << str(b));
            }
    }
}

TEST_CASE("orient: cyclic invariance and antisymmetry hold exactly, also for nearly collinear doubles") {
    Rng rng(0x0789);
    for (int i = 0; i < scaled(100000); ++i) {
        Vec2 a{rng.range(-100, 100), rng.range(-100, 100)};
        Vec2 b{rng.range(-100, 100), rng.range(-100, 100)};
        Vec2 c = a + (b - a) * rng.range(-2.0, 3.0);  // nearly on the line
        for (int k = rng.below(3); k > 0; --k) c.x = std::nextafter(c.x, rng.below(2) ? 1e9 : -1e9);
        const int o = orient(a, b, c);
        if (orient(b, c, a) != o || orient(c, a, b) != o || orient(b, a, c) != -o || orient(a, c, b) != -o)
            FAIL_CHECK("symmetry violated at " << i);
    }
}

TEST_CASE("orient: exact on an exactly collinear triple that a rounded cross product misjudges") {
    // 0.1, 0.3 and 0.7 + scalings: collinear in decimal, not in binary; the exact sign is whatever the
    // stored doubles say, and it must be consistent under permutation (checked above) and non-zero here.
    const Vec2 a{0.7, 0.3}, b{0.6, 0.4}, c{0.0, 1.0};
    const int o = orient(a, b, c);
    CHECK(orient(b, c, a) == o);
    CHECK(orient(a, c, b) == -o);
    // exactly collinear in binary: zero
    CHECK(orient({0.5, 0.25}, {1.0, 0.5}, {2.0, 1.0}) == 0);
    CHECK(orient({1, 1}, {1, 1}, {5, 7}) == 0);  // degenerate a == b
}

TEST_CASE("angleLess: strict weak ordering with exactly and nearly collinear clusters") {
    Rng rng(0xA11);
    const Vec2 light{0.7, 0.3};
    // decimal lattice around the light: lots of points collinear with it in decimal, not in binary
    std::vector<Vec2> pts;
    for (int i = 0; i <= 10; ++i)
        for (int j = 0; j <= 10; ++j) {
            const Vec2 p{i / 10.0, j / 10.0};
            if (p != light) pts.push_back(p);
        }
    for (int i = 0; i < 40; ++i) pts.push_back({rng.range(0, 1), rng.range(0, 1)});
    // exact rays through the light in binary, both directions
    for (int m = 1; m <= 4; ++m) {
        pts.push_back({0.7 + m * 0.5, 0.3});
        pts.push_back({0.7 - m * 0.25, 0.3});
        pts.push_back({0.7, 0.3 + m * 0.125});
        pts.push_back({0.7, 0.3 - m * 0.125});
    }
    const auto less = [&](Vec2 p, Vec2 q) { return angleLess(light, p, q); };
    const auto equiv = [&](Vec2 p, Vec2 q) { return !less(p, q) && !less(q, p); };

    const std::size_t n = pts.size();
    for (std::size_t i = 0; i < n; ++i) CHECK_FALSE(less(pts[i], pts[i]));
    long long equivalentPairs = 0;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            if (less(pts[i], pts[j])) CHECK_FALSE(less(pts[j], pts[i]));  // asymmetry
            if (i != j && equiv(pts[i], pts[j])) ++equivalentPairs;
        }
    CHECK(equivalentPairs > 20);  // the test really contains exactly collinear same-direction clusters
    long long checked = 0;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
            for (std::size_t k = 0; k < n; k += 3) {
                const Vec2 p = pts[i], q = pts[j], r = pts[k];
                if (less(p, q) && less(q, r) && !less(p, r)) FAIL_CHECK("transitivity of < violated");
                if (equiv(p, q) && equiv(q, r) && !equiv(p, r)) FAIL_CHECK("transitivity of equivalence violated");
                ++checked;
            }
    CHECK(checked > 10000);

    std::vector<Vec2> sorted = pts;
    std::sort(sorted.begin(), sorted.end(), less);
    for (std::size_t i = 0; i + 1 < sorted.size(); ++i)
        if (less(sorted[i + 1], sorted[i])) FAIL_CHECK("adjacent inversion at " << i);

    // Independent check: well separated directions must follow atan2 mapped to [0, 2pi).
    const auto angle = [&](Vec2 p) {
        double a = std::atan2(p.y - light.y, p.x - light.x);
        return a < 0 ? a + 6.283185307179586 : a;
    };
    for (std::size_t i = 0; i + 1 < sorted.size(); ++i)
        for (std::size_t j = i + 1; j < sorted.size(); ++j)
            if (angle(sorted[j]) < angle(sorted[i]) - 1e-9) FAIL_CHECK("order disagrees with atan2 at " << i << "," << j);
}

TEST_CASE("angleLess: half-turn split, direction along +x first and -x last") {
    const Vec2 l{5, 5};
    CHECK(angleLess(l, {6, 5}, {5, 6}));    // 0 deg before 90 deg
    CHECK(angleLess(l, {5, 6}, {4, 5}));    // 90 before 180
    CHECK(angleLess(l, {4, 5}, {5, 4}));    // 180 before 270
    CHECK_FALSE(angleLess(l, {5, 4}, {6, 5}));  // 270 after 0
    CHECK_FALSE(angleLess(l, {6, 5}, {9, 5}));  // same ray: equivalent
    CHECK_FALSE(angleLess(l, {9, 5}, {6, 5}));
    CHECK(angleLess(l, {6, 5}, {4, 5}));    // opposite directions are in different half-turns
}
