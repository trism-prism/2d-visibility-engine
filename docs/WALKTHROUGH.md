# Walkthrough

A guided derivation of why the algorithm works, for someone learning the project. The implementation is described in [ARCHITECTURE.md](ARCHITECTURE.md); measurements are in [BENCHMARK.md](BENCHMARK.md). Test names in *italics* are real `TEST_CASE` names in `tests/`.

Notation: the light is `L`, a ray goes from `L` through a target `P`, `d = P - L`. Points on the ray are `L + t d`, `t >= 0`, so `t = 1` is the target. `cross(a, b) = a.x b.y - a.y b.x`; it is positive when `b` is counter-clockwise from `a`.

## 1. Ray against a segment, and the parameter t

A wall is `A + u (B - A)`, `u in [0,1]`, with `e = B - A`. A ray point `L + t d` is on the wall when `L + t d = A + u e`. Take `cross(., e)` of both sides (this removes `u`, because `cross(e, e) = 0`):

```
cross(L, e) + t cross(d, e) = cross(A, e)
t = cross(A - L, e) / cross(d, e)
```

This is the "proper crossing" formula in `rayHit` (`core/geometry.cpp`). Taking `cross(., d)` instead gives `u = cross(A - L, d) / cross(d, e)`; the crossing is on the wall iff `0 <= u <= 1`. Instead of testing `u`, the code asks on which side of the ray `A` and `B` lie: `sa = orient(L, P, A)`, `sb = orient(L, P, B)`. If they are strictly opposite, the wall must cross the ray's line, so `u` is in `(0,1)` and the formula applies. The hit counts if `t > 0` in world units: `t * |d| > kLightClearance`.

If one end `Q` lies exactly on the ray's line, the crossing point is `Q` itself and `t = dot(Q - L, d) / dot(d, d)` (the projection of `Q - L` onto `d`, in units of `d`). For the ray's own target `P`, `Q - L` is computed exactly as `d` is, so `t` is exactly 1 (test *rayHit: a ray's own target is hit at exactly t == 1*).

**Guarding `t` for nearly end-on walls.** The crossing is on the wall, so its `t` must lie between the projections of `A` and `B` on the ray, `tA = dot(A - L, d)/dot(d, d)` and `tB` likewise. When the wall is almost parallel to the ray, `cross(d, e)` and `cross(A - L, e)` are both rounding noise, so the quotient can be non-finite, negative or huge even though the exact side tests have established that the wall blocks the ray. `rayHit` therefore accepts the quotient only inside `[min(tA, tB), max(tA, tB)]`; otherwise it takes the point of the wall interpolated from the rounded side distances, which is always on the wall. Tests: *rayHit: nearly end-on wall with a vanishing rounded denominator still blocks*, *rayHit: finite but inconsistent crossing parameter is replaced by an in-segment hit*.

## 2. Why only some directions matter

Fix a direction `theta` and let `r(theta)` be the distance to the nearest wall along it. Over an angular interval where the nearest wall does not change, `r(theta)` is the distance to one fixed line, so the visible boundary there is a straight piece of that wall. The nearest wall can change only at a direction where

1. a wall **end point** appears or disappears behind another wall (the ray passes through an end point), or
2. two walls **cross** (the order of their distances along the ray swaps).

So the polygon is determined by the rays through all wall end points and all crossing points: the **targets**. Between two consecutive target directions the boundary is one straight segment, so the polygon is the list of hit points taken in angular order (`Scene::rebuild` builds the targets, `computeVisibility` sorts and casts). Crossing walls therefore need no splitting. Tests: *X crossing: the crossing point is a vertex*, *T-junction, ...*. The oracle test *oracle: polygon membership equals the segment-crossing predicate* checks the result against a predicate that knows nothing of targets.

## 3. One-sided limits instead of epsilon offsets

At a target direction the nearest wall can jump. Example: a ray grazes the end `E` of a wall. Rays slightly clockwise of it miss the wall and hit something far away; rays slightly counter-clockwise hit the wall near `E`. The polygon has two vertices on this ray: the far hit and `E`.

```
              far wall
   ───────────●─────────────   <- tMinus (clockwise side): far hit
        L ────●E                <- ray through E
               \ wall          <- tPlus (counter-clockwise side): hits at E, t = 1
```

A simple method casts rays at `theta - e` and `theta + e`. With a fixed `e` it fails when two features are closer than `e` (a narrow gap is missed) and it leaks light at a corner where two walls share an end point. `rayHit` instead returns two numbers, the limits `tMinus` and `tPlus`, using the rule: a wall with exactly one end `Q` on the ray's line counts for the side where its other end lies (`other > 0` is left of the ray, counter-clockwise, so `tPlus`); a wall that properly crosses counts for both. Emission appends `L + tMinus d` then `L + tPlus d` (if different), which is the vertex list in counter-clockwise order. At a shared corner of two walls the two walls contribute `t = 1` to opposite sides, so both sides give the same point: no leak (test *two walls sharing a corner: no light leaks through it*).

## 4. Why the grid cannot change the answer

Both modes compute `min` of `rayHit` over a set of walls. Brute uses all walls. Grid uses the walls registered in the cells the ray visits, stopping early. The claim is that every wall that could affect either minimum is in the visited set.

1. **Registration is a superset.** A wall is registered in every cell whose box (inflated by `kGridPad * cellSize`) it touches, with an inclusive test. So if a wall contains a point `X`, it is registered in every cell whose closed box contains `X`. Test: *grid: registration is a superset of brute-force segment/cell overlap* compares with an independent segment/box test for every wall and cell.
2. **The walk covers the ray up to `exitT`.** The walk visits cells in the order the ray crosses them, and after cell `c` the ray has left `c` at parameter `exitT = min(tMaxX, tMaxY)`. Every ray point with `t <= exitT` is in a visited cell's closed box.
3. **Early stop is safe.** The walk stops after cell `c` only if `tMinus <= exitT` and `tPlus <= exitT`. A wall not yet tested is not registered in any visited cell; by (1) it contains no point of the ray with `t <= exitT`, so its hit, if any, has `t > exitT >= tMinus, tPlus`. It cannot lower either limit.
4. **If the walk never stops early,** it ends when it leaves the grid, having visited every cell the ray crosses; the boundary walls are registered in the border cells, so the nearest hit is found.
5. **Same function, same value.** Each tested wall contributes `rayHit`'s value, which does not depend on which other walls were tested. The minimum of the same set of relevant values is the same real number in both modes and the same `double`. The emitted vertex `L + t d` is computed by the same expression. So the polygons are equal bitwise. Tests: *differential: lattice scenes ...*, *differential: random real-valued scenes ...*, *differential: generator scenes at several resolutions*, *grid: the polygon does not depend on the grid resolution*.

The walk follows the rounded `d` while the side tests use the exact points `(L, P)`. The two lines differ by far less than `kGridPad * cellSize`, which registration absorbs; this is the one place where the argument relies on a tolerance, and the differential tests are the check that it holds in practice. Directly checked pieces: *stats sanity: Grid never tests more walls than Brute*, *trace: on equals off, and the trace describes what the query really did* (recomputes each ray's limits over all walls and compares).

## 5. A failure-prone edge case, in full

The first design sorted targets by `atan2` and decided sides with `cross(d, A-L)` in floating point. The scene: world 1 x 1, wall `A = (0.6, 0.4)`, `B = (0.5, 0.3)`, light `L = (0.7, 0.3)`.

In decimal, `L`, `(0.6,0.4)` and `(0,1)` lie on the line `x + y = 1`. In binary none of these decimals is exact. The differences are `(0.6,0.4) - L = (-0.09999999999999998, 0.10000000000000003)` and `(0,1) - L = (-0.7, 0.7)`. Their cross product is tiny but not zero (4.2e-17 in floating point, 3.3e-17 exactly): `(0,1)` is slightly counter-clockwise of the ray through `(0.6,0.4)`. `atan2` returned the same value, 2.356194490192345, for both directions (Python and the C library give the same double here), so the sort could not tell them apart and the "same key means same ray" rule merged them into one ray aimed at `(0,1)`.

For that merged ray the wall end `(0.6,0.4)` lies, exactly, a hair to the clockwise side (`orient` of `L`, `(0,1)`, `(0.6,0.4)` is -3.3e-17), and `B` is clearly counter-clockwise. The wall therefore crossed the ray properly near `t = 1/7` and counted for both sides: `tMinus = tPlus`. The clockwise side of the real ray through `(0.6,0.4)` sees the top-left corner `(0,1)`, but that vertex was never emitted. The polygon lost the triangle `(1,1), (0,1), (0.6,0.4)` (area 0.3): 0.465 instead of 0.765.

The fix is to make every combinatorial decision with one exact predicate. `orient(L, (0.6,0.4), (0,1))` returns +1: `(0.6,0.4)` precedes `(0,1)` and they remain two rays. Each ray's own side tests are exact for its own target, and the order of rays agrees with what the side tests assume. The exact value is obtained by the filter and, when the filter is not conclusive, by the error-free expansion described in ARCHITECTURE.md section 3.1. Tests: *regression: targets collinear with the light in decimal but not in binary* (area 0.765), *oracle: decimal and non-dyadic lattices (grid snap 0.1, spacing 1600/7)*, and for the predicate itself *orient: classic near-collinear sweep (Kettner et al.)*, *orient: doubles in [1,2) as 2^-52 lattice, collinear and 1-ulp-perturbed triples*, *angleLess: strict weak ordering with exactly and nearly collinear clusters*. The remaining rounding is confined to positions (`t`, crossing points), never to which side or which order.

## 6. Where the optimisation loses, and why

The cost of the grid depends on how many walls share the cells a ray visits and on how many rays there are. Numbers from `bench/results/results.csv` (see [BENCHMARK.md](BENCHMARK.md) for the tables and how they were produced):

- **Many walls in few cells.** Cluster n=250: Grid is 1.5x to 1.8x faster than Brute (three seeds), with 186-241 walls in the busiest cell; a ray entering the patch tests most of its walls.
- **Few walls.** Scatter n=16: 1.11x to 1.13x. The scene has no crossings and 36 targets, and the walk's setup is comparable to the work saved.
- **The sort is not reduced by the grid.** `O(T log T)` exact comparisons are the same in both modes. In Scatter and Maze rows the Grid sort time exceeds its cast time; for Scatter n=1000, seed 1 the sort is 412 us of the 670 us Grid median (committed results, produced before the sort got a cached-difference filter; see ARCHITECTURE.md).
- **The number of rays is set by T.** Long walls (two families of near-parallel long walls) has many crossings: at n=1000, k = 22,486 and T = 24,490 (three-seed medians), and each target gets a ray whether or not it is visible. The grid cuts the wall tests per ray but not the rays.

## 7. Where each claim is tested

| Claim | Test case (file) |
|---|---|
| `rayHit` cases, own target at `t == 1`, clearance | *rayHit: case table*, *... own target ...*, *rayHit: kLightClearance* (`tests/test_geometry.cpp`) |
| Exact orientation | the five *orient: ...* cases (`tests/test_exact.cpp`) |
| Angular order is a strict weak ordering | *angleLess: ...* (`tests/test_exact.cpp`) |
| Hand-computed polygons: single wall, shared corner, T-junction, X, collinear overlap, light on wall / endpoint / corner / boundary, wraparound | the cases in `tests/test_polygons.cpp` |
| Result agrees with an independent visibility predicate and with dense angular sampling | *oracle: ...* (`tests/test_oracle.cpp`) |
| Brute equals Grid bitwise | *differential: ...* (`tests/test_differential.cpp`) |
| Grid registration, crossings equal to an `O(n^2)` reference, mutators leave derived data consistent, trace on equals off, no allocation in steady state, stamp-counter wraparound, `GridWalker` coverage and termination | `tests/test_grid.cpp` |
| Exact `orient` with inexact differences (mixed exponents) | *orient: mixed exponents so that b-a and c-a round ...* (`tests/test_exact.cpp`) |
| The polygon boundary itself, not only its area | *oracle: points just inside and just outside every polygon edge agree with the predicate* (`tests/test_oracle.cpp`) |
| Wall input limits | *Scene::addWall rejects coordinates of huge magnitude* (`tests/test_geometry.cpp`) |
