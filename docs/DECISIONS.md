# Decisions

One entry per consequential decision: context, options, choice, cost, and the evidence that would justify changing it. Numbers come from the files named in each entry. How the code works is in [ARCHITECTURE.md](ARCHITECTURE.md); the contract is [DESIGN.md](DESIGN.md).

## 1. Targeted rays with exact one-sided limits, not epsilon offsets

- **Context.** A visibility polygon changes only at directions through wall end points and wall crossings.
- **Options.** (a) Cast three rays per end point at angle `-e`, `0`, `+e`. (b) Cast one ray per target and compute, for each side, the limit of the nearest hit (`tMinus`, `tPlus`).
- **Choice.** (b). `rayHit` classifies each wall end against the ray exactly (`orient`) and returns the limit from each side.
- **Cost.** Slightly more logic per wall test than an ordinary ray/segment test; the case table in ARCHITECTURE.md section 3.3.
- **Why.** Offsets leak light through shared corners and miss gaps narrower than the offset (DESIGN.md section 5). Tests that would fail with offsets: "two walls sharing a corner: no light leaks through it", "T-junction ...", `tests/test_polygons.cpp`.
- **Change if.** A use case needs polygons with a tolerance (e.g. rendering from a noisy light position) and the exact version proves too slow, which is not measured here.

## 2. Exact `orient` instead of `atan2` and a rounded cross product

- **Context.** The first implementation sorted targets by `atan2` and classified sides with `cross(d, A-L)` in floating point.
- **Failure.** World 1 x 1, wall `(0.6,0.4)-(0.5,0.3)`, light `(0.7,0.3)`: the light, `(0.6,0.4)` and the corner `(0,1)` are collinear in decimal but not in binary. Both targets got the same `atan2` key, the pair collapsed to one ray, and the wall end was then classified by a rounded cross product, which dropped the polygon vertex `(0,1)` (area 0.465 instead of 0.765). A search over decimal-lattice scenes found scenes of one or two walls with 150-300 of 600 sample points misclassified. Kept as the test "regression: targets collinear with the light in decimal but not in binary".
- **Options.** (a) Quantise coordinates. (b) Make the sort consistent with the side tests by a shared rounded key. (c) One exact orientation predicate for both decisions.
- **Choice.** (c): `orient` (floating-point filter, exact expansion fallback), used by the angular order, run collapse, `rayHit` and `intersectSegments`.
- **Cost.** Measured on Scatter n=1000 when the change was made (`STATUS.md`): Grid query about 410 us to about 700-750 us, Brute about 19 ms to about 30 ms. The sort is now the largest part of the Grid query on Scatter and Maze ([BENCHMARK.md](BENCHMARK.md), "Split of the Grid time").
- **Change if.** A cheaper filter-only comparator or cached per-target directions keeps the exactness (not implemented), or inputs are guaranteed to be on a lattice where plain arithmetic is exact.

## 3. Uniform grid, not a quadtree, BVH or angular sweep

- **Context.** Brute force is `Theta(T n)` per query.
- **Options.** Uniform grid with a DDA walk; quadtree or BVH with ordered traversal; angular sweep with an ordered set of active walls (`O((n+k) log n)` per query, DESIGN.md section 5).
- **Choice.** Uniform grid. It is the smallest change that keeps the same per-ray function and yields bit-identical output, and its walk can be drawn from `Trace` without extra data structures.
- **Cost.** It degrades when many walls share cells and does not reduce the number of rays: Cluster n=250 is 1.5-1.8x faster than Brute, Long walls n=4000 has 345,934 targets and takes 696 ms per query in Grid mode (seed 1) ([BENCHMARK.md](BENCHMARK.md)).
- **Change if.** Clustered scenes or crossing-heavy scenes must be interactive. The angular sweep needs splitting walls at crossings and a distance comparator valid only for non-crossing pieces; a quadtree or BVH adapts to clusters. Neither is implemented or measured.

## 4. Crossing walls supported through crossing targets

- **Options.** Reject crossing walls; split walls at crossings; add every crossing point as a ray target.
- **Choice.** Add crossing points as targets (`Scene::rebuild`, `intersectSegments`).
- **Why.** Users can draw crossing walls; splitting changes wall indices and the data the overlay shows; between consecutive target directions the nearest wall still cannot change.
- **Cost.** `k` extra rays and an on-edit crossing search (`pairsTested`, worst case `O(n^2)`). Long walls n=1000 has k = 22,486 (median over seeds) and spends 4.45 ms per edit in crossings + targets ([BENCHMARK.md](BENCHMARK.md), edit costs).
- **Change if.** Typical scenes have far more crossings than walls and the per-edit cost matters.

## 5. Full rebuild on every edit

- **Options.** Rebuild grid, crossings and targets from scratch; update incrementally (register one wall, find its crossings, merge targets).
- **Choice.** Full rebuild inside every mutator (invariant 4 in DESIGN.md: derived data is never older than the wall list).
- **Cost.** One `addWall` plus `removeWall` pair (two rebuilds): see "Edit costs" in [BENCHMARK.md](BENCHMARK.md); at n=1000 the pair costs 643 us for Scatter and 9.93 ms for Long walls (medians over seeds).
- **Change if.** Interactive editing of scenes where one edit costs more than a frame (the pair for Long walls n=4000 is 145.9 ms).

## 6. Automatic grid resolution

- **Rule (code: `Scene::autoResolution`).** `cells = max(wallCount, 16)` with the four boundary walls counted; `side = sqrt(W*H / cells)`; `cols = clamp(lround(W/side), 1, 2048)`, `rows` likewise.
- **Evidence.** The sweep in [BENCHMARK.md](BENCHMARK.md) (n=1000, seed 1) shows Scatter improving only slightly from 1x to 4x of this resolution (656, 624, 595 us) while its grid build grows from 91 to 253 us, and Long walls improving up to 4x (21.1 ms at 1x, 8.0 ms at 4x) while registrations grow from 34,807 to 136,191 and the grid build from 612 us to 2.02 ms. A manual override (`Scene::setGridResolution`) is in the panel.
- **Change if.** A layout class is found where the rule is far from the best resolution across seeds; only n=1000 seed 1 of Scatter and Long walls was swept.

## 7. Rendering with `ImDrawList` and shadow quads

- **Options.** A custom OpenGL renderer with stencil or shadow volumes; `ImDrawList` for the canvas.
- **Choice.** `ImDrawList` for everything (floor gradient ring mesh, opaque shadow quads from each polygon edge to far beyond the world, walls, overlays). The shadow edges are exactly the computed polygon edges, and no second renderer needs maintaining. 32-bit draw indices (`app/imconfig_user.h`) because dense scenes exceed 65,535 vertices per list.
- **Cost.** Shadow quads are filled on the CPU-submitted triangle list; fill is un-anti-aliased with 4x MSAA for the outline. The gradient is cosmetic.
- **Change if.** Frame rendering rather than the query becomes the bottleneck.

## 8. GLFW, Dear ImGui, doctest, pinned tags

- **Choice.** GLFW 3.4, Dear ImGui v1.92.9 (GLFW and OpenGL3 backends), doctest v2.4.12, fetched with `FetchContent` at exact tags (checked with `git ls-remote --tags` when chosen; newer tags existed: GLFW 3.5.1, ImGui v1.93.0, doctest v2.5.x). ImGui and doctest are fetched without running their CMake files (`SOURCE_SUBDIR`).
- **Why.** One library covers dashboard and canvas; doctest is a single header; the others considered (raylib, SFML) in DESIGN.md section 1.
- **Change if.** A tag needs a security or driver fix; then re-run the tests and captures.

## 9. Timing methodology

- **Choice.** The displayed query time is a real timed `computeVisibility` call with no trace; the overlay trace comes from a second, untimed call. The panel shows the median of the last 60 samples, and the application re-queries a few frames after a change to collect samples (the query is not repeated while nothing changes). Compare mode is labelled and runs the other back end as well. The benchmark times only calls of `computeVisibility` (see [BENCHMARK.md](BENCHMARK.md)).
- **Why.** Tracing writes data the timed pass should not pay for; a median is robust against scheduler hiccups.
- **Change if.** Rendering or GUI time must be attributed separately.

## 10. The world rectangle is the visibility boundary

- **Choice.** Light and walls live in `[0,W]x[0,H]`; its four edges are permanent walls (indices 0..3), so every ray terminates; wall input is clipped to it (`clipToRect`).
- **Cost.** No unbounded worlds. A light on the boundary sees only inward; the polygon vertex for a side that looks outside is the light itself.
- **Change if.** Unbounded or very large worlds are needed (the grid would need another bound).

## 11. Light on a wall

- **Choice.** A wall closer to the light than `kLightClearance` (1e-9 along the ray) does not block that ray, so a light on a wall shines on both sides; a wall seen exactly end-on blocks nothing.
- **Alternatives.** Treat the wall as a blocker from the light's own position (the light would see nothing on one side); or forbid lights on walls.
- **Cost.** A light placed within 1e-9 of a wall is treated as on it. Tests: "light exactly on a wall interior: it shines to both sides", "light exactly on a wall endpoint".
- **Change if.** The application needs lights to be blocked by the wall they are on.
