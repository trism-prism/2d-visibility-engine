# Decisions

One entry per decision that mattered: context, options, choice, cost, and what evidence would make us change it. Numbers come from the files named in each entry. How the code works is in [ARCHITECTURE.md](ARCHITECTURE.md); the contract is [DESIGN.md](DESIGN.md).

## 1. Targeted rays with exact one-sided limits, not epsilon offsets

- **Context.** A visibility polygon only changes at directions that pass through a wall end point or a wall crossing.
- **Options.**
  - (a) Cast three rays per end point, at angle `-e`, `0` and `+e`.
  - (b) Cast one ray per target and work out, for each side of it, the limit of the nearest hit (`tMinus`, `tPlus`).
- **Choice.** (b). `rayHit` classifies each wall end against the ray exactly (`orient`) and returns the limit from each side.
- **Cost.** A bit more logic per wall test than a plain ray/segment test. The case table is in ARCHITECTURE.md section 3.3.
- **Why.** Offsets leak light through shared corners and miss gaps narrower than the offset (DESIGN.md section 5). Two tests that would fail with offsets: "two walls sharing a corner: no light leaks through it" and "T-junction ...", both in `tests/test_polygons.cpp`.
- **Change if.** A use case wants polygons with a tolerance (say, rendering from a noisy light position) and the exact version turns out too slow. We haven't measured that.

## 2. Exact `orient` instead of `atan2` and a rounded cross product

- **Context.** The first implementation sorted targets by `atan2` and decided which side of a ray a point was on with `cross(d, A-L)` in plain floating point.
- **Failure.**
  - Setup: world 1 x 1, wall `(0.6,0.4)-(0.5,0.3)`, light `(0.7,0.3)`.
  - The light, `(0.6,0.4)` and the corner `(0,1)` are collinear in decimal but not in binary.
  - Both targets got the same `atan2` key, so the pair collapsed to one ray.
  - The wall end was then classified by a rounded cross product, which dropped the polygon vertex `(0,1)`. Area came out 0.465 instead of 0.765.
  - A search over decimal-lattice scenes found scenes of one or two walls with 150-300 of 600 sample points misclassified.
  - Kept as the test "regression: targets collinear with the light in decimal but not in binary".
- **Options.**
  - (a) Quantise coordinates.
  - (b) Make the sort agree with the side tests through a shared rounded key.
  - (c) One exact orientation predicate for both decisions.
- **Choice.** (c): `orient` (floating-point filter, exact expansion fallback). It's used by the angular order, run collapse, `rayHit` and `intersectSegments`.
- **Cost.** Measured on Scatter n=1000 when the change was made (`STATUS.md`):
  - Grid query: about 410 us to about 700-750 us.
  - Brute: about 19 ms to about 30 ms.
  - The sort is now the largest part of the Grid query on Scatter and Maze ([BENCHMARK.md](BENCHMARK.md), "Split of the Grid time").
- **Change if.** Inputs are guaranteed to sit on a lattice where plain arithmetic is exact.
- **Update 2026-11-13.** A cached-difference filter now runs before the exact test, in `core/visibility.cpp`. The result stays bitwise identical. A throwaway harness showed the sort about 30% faster and the query about 20-30% faster on Scatter and Maze at n=1000. The cost figures above and the committed benchmark results predate it and were not regenerated.

## 3. Uniform grid, not a quadtree, BVH or angular sweep

- **Context.** Brute force is `Theta(T n)` per query.
- **Options.**
  - Uniform grid with a DDA walk.
  - Quadtree or BVH with ordered traversal.
  - Angular sweep with an ordered set of active walls (`O((n+k) log n)` per query, DESIGN.md section 5).
- **Choice.** Uniform grid. It's the smallest change that keeps the same per-ray function and gives bit-identical output. Its walk can also be drawn from `Trace` without extra data structures.
- **Cost.** It degrades when many walls share cells, and it doesn't reduce the number of rays.
  - Cluster n=250 is only 1.5-1.8x faster than Brute.
  - Long walls n=4000 has 345,934 targets and takes 696 ms per query in Grid mode (seed 1).
  - Source: [BENCHMARK.md](BENCHMARK.md).
- **Change if.** Clustered scenes or crossing-heavy scenes have to be interactive.
  - The angular sweep needs walls split at crossings, and a distance comparator that's only valid for non-crossing pieces.
  - A quadtree or BVH adapts to clusters.
  - Neither is implemented or measured.

## 4. Crossing walls supported through crossing targets

- **Options.** Reject crossing walls; split walls at crossings; add every crossing point as a ray target.
- **Choice.** Add crossing points as targets (`Scene::rebuild`, `intersectSegments`).
- **Why.**
  - Users can draw crossing walls.
  - Splitting changes wall indices and the data the overlay shows.
  - Between consecutive target directions the nearest wall still can't change.
- **Cost.** `k` extra rays, plus a crossing search on every edit (`pairsTested`, worst case `O(n^2)`). Long walls n=1000 has k = 22,486 (median over seeds) and spends 4.45 ms per edit on crossings + targets ([BENCHMARK.md](BENCHMARK.md), edit costs).
- **Change if.** Typical scenes have far more crossings than walls and the per-edit cost matters.

## 5. Full rebuild on every edit

- **Options.** Rebuild grid, crossings and targets from scratch; or update incrementally (register one wall, find its crossings, merge targets).
- **Choice.** Full rebuild inside every mutator (invariant 4 in DESIGN.md: derived data is never older than the wall list).
- **Cost.** One `addWall` plus `removeWall` pair means two rebuilds. See "Edit costs" in [BENCHMARK.md](BENCHMARK.md). At n=1000 the pair costs 643 us for Scatter and 9.93 ms for Long walls (medians over seeds).
- **Change if.** Someone edits scenes where one edit costs more than a frame. The pair for Long walls n=4000 is 145.9 ms.

## 6. Automatic grid resolution

- **Rule (code: `Scene::autoResolution`).**
  - `cells = max(wallCount, 16)`, with the four boundary walls counted.
  - `side = sqrt(W*H / cells)`.
  - `cols = clamp(lround(W/side), 1, 2048)`, and `rows` the same way.
- **Evidence.** The sweep in [BENCHMARK.md](BENCHMARK.md) (n=1000, seed 1):
  - Scatter improves only slightly from 1x to 4x of this resolution (656, 624, 595 us), while its grid build grows from 91 to 253 us.
  - Long walls improves up to 4x (21.1 ms at 1x, 8.0 ms at 4x), while registrations grow from 34,807 to 136,191 and the grid build from 612 us to 2.02 ms.
  - A manual override (`Scene::setGridResolution`) is in the Engine level of the panel (Grid size).
- **Change if.** We find a layout class where the rule is far from the best resolution across seeds. Only n=1000 seed 1 of Scatter and Long walls was swept.

## 7. Rendering with `ImDrawList` and shadow quads

- **Options.** A custom OpenGL renderer with stencil or shadow volumes; or `ImDrawList` for the canvas.
- **Choice.** `ImDrawList` for everything:
  - the floor gradient (a ring mesh),
  - opaque shadow quads from each polygon edge to far beyond the world,
  - walls and overlays.

  The shadow edges are exactly the computed polygon edges, and there's no second renderer to maintain. Draw indices are 32-bit (`app/imconfig_user.h`) because dense scenes exceed 65,535 vertices per list.
- **Cost.** Shadow quads are filled from a CPU-submitted triangle list. Fill isn't anti-aliased; the outline uses 4x MSAA. The gradient is cosmetic.
- **Change if.** Rendering a frame, rather than the query, becomes the bottleneck.

## 8. GLFW, Dear ImGui, doctest, pinned tags

- **Choice.** GLFW 3.4, Dear ImGui v1.92.9 (GLFW and OpenGL3 backends), doctest v2.4.12. All are fetched with `FetchContent` at exact tags.
  - We checked the tags with `git ls-remote --tags` when choosing. Newer ones existed: GLFW 3.5.1, ImGui v1.93.0, doctest v2.5.x.
  - ImGui and doctest are fetched without running their CMake files (`SOURCE_SUBDIR`).
- **Why.** One library covers dashboard and canvas, and doctest is a single header. The other candidates (raylib, SFML) are covered in DESIGN.md section 1.
- **Change if.** A tag needs a security or driver fix. Then re-run the tests and captures.

## 9. Timing methodology

- **Choice.**
  - The displayed query time is a real timed `computeVisibility` call with no trace.
  - The overlay trace comes from a second, untimed call. So does the work meter's extra Grid pass (decision 12).
  - The panel shows the median of the last 60 samples.
  - After a change, the application re-queries a few frames later to collect samples: up to 8 more updates, but only one more when an update takes longer than 50 ms. The query isn't repeated while nothing changes.
  - "Run both" (Engine, card "Compare") is labelled, and runs the other back end as well.
  - The benchmark times only calls of `computeVisibility` (see [BENCHMARK.md](BENCHMARK.md)).
- **Why.** Tracing writes data the timed pass shouldn't pay for, and a median shrugs off scheduler hiccups.
- **Change if.** Rendering or GUI time has to be attributed separately.

## 10. The world rectangle is the visibility boundary

- **Choice.**
  - Light and walls live in `[0,W]x[0,H]`.
  - Its four edges are permanent walls (indices 0..3), so every ray terminates.
  - Wall input is clipped to it (`clipToRect`).
- **Cost.** No unbounded worlds. A light on the boundary sees only inward; the polygon vertex for a side that looks outside is the light itself.
- **Change if.** We need unbounded or very large worlds. The grid would need another bound.

## 11. Light on a wall

- **Choice.** A wall closer to the light than `kLightClearance` (1e-9 along the ray) doesn't block that ray. So a light on a wall shines on both sides. A wall seen exactly end-on blocks nothing.
- **Alternatives.** Treat the wall as a blocker from the light's own position (the light would see nothing on one side); or forbid lights on walls.
- **Cost.** A light placed within 1e-9 of a wall counts as on it. Tests: "light exactly on a wall interior: it shines to both sides" and "light exactly on a wall endpoint".
- **Change if.** The application needs lights to be blocked by the wall they sit on.

## 12. A plain-language interface with a work meter (the UI refresh)

- **Context.** The first interface assumed a technical reader.
  - The dashboard was full of engine terms: crossings k, targets T, cells walked, wall tests.
  - The methods were called Baseline and Grid.
  - The overlays were "Grid cells", "Probe ray" and "All rays". The controls were "Compare both modes" and "Outline visibility polygon".
  - Nothing on screen said what a number meant, or whether it was good.
- **Options.**
  - (a) Keep the dashboard and add documentation.
  - (b) Replace the technical panel with a simplified one and drop the numbers.
  - (c) Two levels: plain language first, the precise term one hover or one click away, and the original numbers kept.
- **Choice.** (c).
  - **Simple level (default).**
    - Methods: "All walls" (Baseline) and "Grid".
    - Overlays: "Light only", "Squares", "One ray" and "All rays" (`off`, `grid`, `probe`, `rays`; the first version called them "Grid cells", "Probe ray", "All rays").
    - Scenes have plain names: Demo, Scatter, Maze, Long walls, Cluster.
    - A six-step tour opens on a plain launch.
    - A legend and a live one-sentence caption say what is drawn.
  - **Engine level (one click).** Shows every number the first version showed, with the same meaning. `(?)` markers and row labels carry one-sentence definitions (`gloss` in `app/widgets.cpp`).
  - **Real numbers only.** Nothing in the meter, the captions or the legend is an illustration.
  - **The work meter** compares the two methods in wall tests.
    - Its **All walls** figure is exact arithmetic: rays x walls, because Baseline tests every wall for every ray.
    - Its **Grid** figure is measured: the wall-test counter of a real Grid query for the same light position.
    - The interaction script checks both properties (`tests/scripts/main.script`).
- **Cost.**
  - While Baseline is in use and Compare is off, there's no Grid query to read. So each update runs one extra Grid query into `meterResult`. It's untimed and feeds no statistic, but it is extra work per update (and the application repeats an update a few frames after a change, `settle`).
  - It's skipped while the light or scene is changing when rays x walls exceeds 20,000,000 (`kMeterSkipTests`, `App::updateMeter`, `app/app.cpp`).
    - The grid bar then reads "not measured", with the sentence "Measuring once you stop.".
    - One update later, a single untimed Grid pass fills in the real figure (`meter_source` = `extra_pass`). Updates slower than 50 ms are repeated only once, not 8 times.
  - The threshold is a work count, so the behaviour doesn't depend on the machine. The code comment puts it at an update of well over 100 ms. We didn't re-measure that for this entry.
  - A separate one-off calibration (three Baseline queries on a 1,000-wall Scatter scene) prices a wall test for the slow-scene note (limit `kSlowMs` = 30 ms).
  - The panel code grew from `app/ui.cpp` alone into `ui.cpp`, `meter.cpp`, `details.cpp`, `widgets.cpp`, `legend.cpp` and `tour.cpp`.
- **Change if.**
  - Someone seeing the application for the first time still can't say what the meter compares after the tour. We haven't tested this with other people.
  - The extra Grid pass is measured to make Baseline updates noticeably slower below the threshold. Then the threshold should come down, or the pass should be sampled.
  - The meter's two figures stop being comparable. For example, if Baseline gains an early exit, its figure would no longer be rays x walls and would have to be measured.
