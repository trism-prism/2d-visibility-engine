# Design

This is the contract the implementation is built against. Where code and this file disagree, one of them is a bug.

## 1. Stack

| Part | Choice | Why |
|---|---|---|
| Language | C++20, MSVC 19.4x (VS Build Tools 2022) | Required; verified toolchain |
| Build | CMake + Ninja, `CMakePresets.json`, dependencies pinned by tag via `FetchContent` | Reproducible, no system-wide installs |
| Window / input | GLFW (pinned release tag) | Small, stable, Windows-native |
| GUI + 2D drawing | Dear ImGui (pinned release tag), its GLFW + OpenGL3 backends, `ImDrawList` for the canvas | One library covers dashboard and canvas; no separate renderer to maintain |
| Tests | doctest (pinned release tag) | Single header, fast builds |
| Core | `viscore` static library: standard library only | Headless tests and benchmarks |

Rejected: raylib + rlImGui (a third bridging dependency for no gain), SFML (heavier, ImGui-SFML version coupling), hand-written OpenGL renderer (more code than the product needs).

## 2. World and geometry contract

- **World**: axis-aligned rectangle `[0,W] x [0,H]` in world units (doubles). Its four edges are permanent walls (indices 0..3). This rectangle is the finite visibility boundary: nothing outside it exists.
- **Wall**: closed line segment `A-B`, opaque, two-sided, zero thickness.
- **Insertion rules** (`Scene::addWall`), applied identically to user edits and stress generators:
  - non-finite coordinates, or any coordinate with magnitude above 1e150 (differences would overflow): rejected;
  - the segment is clipped to the world rectangle (Liang-Barsky); a wall entirely outside is rejected;
  - clipped length `< kMinWallLength` (1e-6): rejected (covers zero-length input).
- **Crossing walls are supported.** Walls may cross, touch, share endpoints, form T-junctions, and overlap collinearly. No splitting is performed; instead every pairwise intersection point is added as a ray target (section 3).
- **Collinear overlap**: parallel pairs contribute no intersection points; their endpoints are already targets, which is sufficient because between endpoints the overlapping walls occlude identically.
- **Light**: a point, clamped to the closed world rectangle. A wall closer to the light than `kLightClearance` (1e-9 world units, measured along a ray) does not occlude that ray: a light sitting exactly on a wall shines to both sides. A wall seen exactly end-on (collinear with a ray) occludes nothing on that ray.
- **Tolerances by purpose**: `kMinWallLength` (input validation), `kLightClearance` (light-on-wall semantics), `kGridPad` (conservative cell registration, relative to cell size). No tolerance is used inside a sort comparator or to decide which side of a ray a point is on.

## 3. Visibility algorithm (shared by both modes)

Output: a star-shaped polygon around the light `L`, vertices in counter-clockwise order, exact up to floating-point rounding. No angular epsilon offsets are used.

**Targets** (scene-level, recomputed only on edit): the de-duplicated set of all wall endpoints and all pairwise wall intersection points. Between two consecutive target directions the nearest wall cannot change, so the visible boundary there is one straight piece of one wall.

**Query** (per light position):

1. For each target `P` with `d = P - L`, `|d| >= kLightClearance`: sort targets counter-clockwise around `L`. The comparator first splits directions into two half-turns using exact coordinate comparisons (`P.y > L.y`, or `P.y == L.y && P.x > L.x`, is the first half), then orders within a half by the sign of the exact predicate `orient(L, P, Q)`. Exact signs make this a strict weak ordering. Targets that compare equivalent lie on the same ray exactly; each such run is collapsed to one ray aimed at its nearest member.
2. For each ray compute two distances, the one-sided limits of the nearest hit as the direction approaches from clockwise (`tMinus`) and from counter-clockwise (`tPlus`). They differ exactly where a wall edge is a silhouette.
3. Emit `L + tMinus*d`, then `L + tPlus*d` if different. Consecutive duplicates are dropped.

**Exact orientation** (`orient` in `core/geometry`): the sign of `(b-a) x (c-a)` evaluated exactly on the given doubles (floating-point filter, then an error-free expansion fallback). It is the only source of combinatorial decisions: the angular order and the side of a ray a point lies on. Distances `t` are ordinary floating point. Exactness assumes no overflow or underflow in the products, which holds for world-scale coordinates (accepted walls lie inside the world rectangle and the light is clamped to it); it would fail only for points about 1e-150 apart or closer, far below `kLightClearance`.

Why exactness is required, not optional: an earlier revision sorted by `atan2` and classified sides with a rounded cross product. For a light, a wall end and another target that are collinear in decimal but not in binary, the sort and the side test could disagree, dropping a polygon vertex (repro: world 1x1, wall `(0.6,0.4)-(0.5,0.3)`, light `(0.7,0.3)`; area 0.465 instead of 0.765). With one exact predicate both decisions are consistent with the scene as actually stored.

**One-sided ray/segment test** (`rayHit` in `core/geometry`), for the ray from `L` through target `P` and segment `A-B`, with `sa = orient(L, P, A)`, `sb = orient(L, P, B)` (signs; positive = left of the ray):

| Case | Result |
|---|---|
| `sa == 0 && sb == 0` (collinear with ray) | no hit |
| `sa`, `sb` same strict sign | no hit |
| opposite strict signs (proper crossing) | `t = cross(A-L, B-A) / cross(d, B-A)`; counts for both sides |
| exactly one is zero (endpoint `Q` on the ray line) | `t = dot(Q-L, d) / dot(d, d)`; counts for `tPlus` only if the other endpoint is left of the ray (`> 0`), for `tMinus` only if right |

For a proper crossing, `t` must lie between the projections of `A` and `B` onto the ray, `dot(A-L,d)/dot(d,d)` and `dot(B-L,d)/dot(d,d)`. For a wall seen almost end-on the quotient above is a ratio of two rounding-noise values and can violate this (negative, huge or non-finite); in that case `t` is taken from the point `A + u(B-A)`, `u = |fa| / (|fa| + |fb|)` with the rounded `fa = cross(d, A-L)`, `fb = cross(d, B-L)` (`u = 1/2` if both are zero). The exact side tests have already decided that the wall blocks the ray; this rule only guarantees the reported distance is on the wall, so a blocking wall is never skipped.

A hit counts only if `t * |d| > kLightClearance`. `t` is in units of `d = P - L`, so the target itself is at exactly `t = 1`. Remaining rounding is confined to positions: `t` values, and intersection targets, whose computed position can differ from the true crossing by a few ulps. Such a ray is still classified exactly for its own direction, so the worst effect is a sliver of area on the order of rounding error, never a missing or misordered vertex. A light on the world boundary sees nothing on rays that leave the world; the polygon vertex for that side is the light itself.

Every ray terminates: the light is inside the world and the boundary walls enclose it.

## 4. Baseline and optimized modes

Both modes run the identical query above: same targets, same sort, same `rayHit`, same output. They differ **only** in which walls each ray tests.

- **Baseline (brute force)**: each ray tests every wall. `O(T * n)`.
- **Optimized (uniform grid)**: walls are registered in every grid cell they touch. Each ray walks cells outward from the light (Amanatides-Woo DDA) and tests only walls registered in visited cells, stopping early.

Because both take the minimum of the same function over a set that contains the true nearest wall, their outputs are required to be **bitwise identical**. Tests enforce exact equality.

### Grid invariants

1. **Conservative registration**: wall `s` is in cell `c` if `s` intersects `c` inflated by `kGridPad * cellSize` (inclusive test). A wall through a cell corner is therefore in all four adjacent cells. An endpoint lying outside a cell says nothing; the segment/box overlap decides.
2. **Early termination**: after testing a cell, the walk stops only when both `tMinus` and `tPlus` are `<=` the ray parameter at which the ray leaves that cell. A hit found beyond the current cell is kept as a candidate but does not stop the walk.
3. **No duplicate tests**: a per-query stamp array (wall index -> last ray id) skips a wall already tested by this ray.
4. **No stale references**: the grid and the target list are owned by `Scene` and rebuilt inside every mutating call; they are never visible in a state older than the wall list.
5. **Termination**: boundary walls are registered in the border cells; the walk also stops at the grid edge.

Resolution: `cols x rows` chosen so cells are roughly square and `cols*rows ~ n` (clamped), with a manual override in the UI to expose the tradeoff.

### Complexity

`n` walls, `k` pairwise intersections, `T = O(n + k)` targets, `G` grid cells, `c_s` cells touched by wall `s`, `m_r` cells walked by ray `r`, `w_r` distinct walls tested by ray `r`.

| Step | When | Cost |
|---|---|---|
| Grid build | on edit | `O(G + sum of bounding-box cells of each wall)` |
| Intersection finding | on edit | pairs sharing a cell; worst case `O(n^2)` |
| Target sort | per query, both modes | `O(T log T)` |
| Ray casting, baseline | per query | `Theta(T * n)` |
| Ray casting, grid | per query | `O(sum_r (m_r + w_r))`; worst case `O(T * (sqrt(G) + n))`, no better than baseline |
| Output | per query | `O(T)` |

No logarithmic query bound is claimed. The grid wins when rays stop early or pass through sparsely populated cells; it loses (overhead without pruning) when walls are long, clustered into few cells, or the light sees far. Moving the light rebuilds nothing.

## 5. Alternatives considered

- **Angular sweep with an ordered active set** (`O((n+k) log n)` per query): asymptotically better per query and output-sensitive in spirit, but requires splitting walls at intersections and a distance comparator that is only valid for non-crossing fragments; correctness risk is concentrated in the hardest-to-test code. Would be chosen if crossing-heavy scenes (large `k`) had to be interactive.
- **Quadtree / BVH**: adapt to clustered scenes where a uniform grid degrades; ray walks need a stack and ordered traversal. More code, harder to draw honestly. Would be chosen if the "cluster" stress scene mattered more than simplicity.
- **Endpoint rays with +/- epsilon offsets**: simplest, but leaks light through shared corners and misses thin gaps below the offset. Replaced by the exact one-sided test.

## 6. Data flow

```
edit (add/delete/reset/load stress)  ->  Scene: walls -> grid -> intersections -> targets
light move                           ->  computeVisibility(scene, light, mode, result, trace?)
                                          sort targets -> cast rays (brute | grid) -> polygon
app                                  ->  draw light gradient, shadow quads behind polygon edges, walls, overlay, dashboard
```

The overlay is drawn from a `Trace` filled by the real query code (cells visited per ray, walls tested, hit points), collected in a second, untimed pass so that displayed timings exclude tracing.

## 7. Acceptance checks

1. Configure + build from a clean tree with the documented commands; `viscore`, tests and benchmark build without GLFW/ImGui.
2. Unit tests: `rayHit` case table; clipping/validation; analytic polygons (empty world, single wall, shared corner, T-junction, crossing walls, collinear overlap, light on wall/endpoint/corner/boundary).
3. Independent oracle: point-in-polygon of the result agrees with a direct "segment light->q crosses no wall" predicate for random points (excluding a thin band at the polygon boundary), and polygon area agrees with dense angular sampling.
4. Seeded differential test: baseline and grid outputs bitwise equal over random scenes and light positions; seed printed on failure.
5. Grid invariants: registration is a superset of a brute-force segment/cell overlap; intersection finder equals an `O(n^2)` reference.
6. Tests pass under AddressSanitizer (MSVC `/fsanitize=address`). The `debug` and `asan` presets run every test case with reduced iteration counts (`VIS_TEST_SCALE`); only `release` runs the full counts.
7. App: draw, delete, drag light, reset, mode switch, overlay, each stress scene; verified through scripted input replay and captured frames.
8. Release benchmark CSV across wall counts and layouts, including update costs, with unfavorable cases reported.
