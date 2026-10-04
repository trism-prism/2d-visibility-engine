# Benchmark

This document describes `vis_bench`, the headless benchmark in `bench/main.cpp`, and the results committed in `bench/results/`. Every number below is read from `bench/results/results.csv`, `bench/results/grid_sweep.csv` or `bench/results/results.meta.txt` (the run time also from the console log of that run). The architecture and the algorithms are described in [ARCHITECTURE.md](ARCHITECTURE.md).

Terms used here:

- **Baseline / Brute**: the mode where every ray tests every wall (`Mode::Brute`).
- **Grid**: the mode where each ray walks a uniform grid and tests only the walls registered in the cells it visits (`Mode::Grid`).
- **n**: number of user walls requested from a scene generator (the four boundary walls come on top).
- **k**: number of proper wall-wall crossings. **T**: number of targets (wall endpoints plus crossing points, de-duplicated). One ray is cast per target direction (collinear targets share a ray), so T bounds the rays per query.
- **Query**: one call of `computeVisibility` for one light position.
- **Wall test**: one call of `rayHit`. **Cells walked**: grid cells visited by the ray walks of one query.

## What is measured, and what is not

Measured, per row (one scene layout, n, seed):

- the time of `computeVisibility` for each mode, split into the sort part and the cast part as reported by `VisibilityStats::sortUs` and `castUs`;
- counters from the same calls (wall tests, cells walked, rays, output vertices);
- the cost of rebuilding the scene's derived data (`RebuildStats::gridBuildUs`, `intersectUs`) and of one `addWall` followed by `removeWall`.

Not measured: scene generation, file output, rendering, the overlay trace pass, the GUI. Queries run on one thread; the benchmark starts no other threads.

## Reproduce

From the repository root, in PowerShell:

```powershell
.\scripts\dev.ps1 cmake --preset release
.\scripts\dev.ps1 cmake --build --preset release
.\build\release\bench\vis_bench.exe --out bench\results\results.csv
```

The full run took 1340 s on the machine below (console line `done in 1340.1 s`; the log, `build/bench_full.log`, is not committed). The command writes three files next to `--out`: `results.csv`, `grid_sweep.csv` and `results.meta.txt`. Options: `--quick` (small matrix, 16 positions, 3 samples; used by the CTest `bench_quick`, label `bench`), `--budget SECONDS` (default 10), `--samples N` (default 15). `vis_bench` links `viscore` only, so it also builds with `-DVIS_BUILD_APP=OFF`.

## Environment

From `bench/results/results.meta.txt`:

| Item | Value |
|---|---|
| Date | 2026-11-12 19:08:59 -0500 |
| Compiler | MSVC _MSC_FULL_VER 194435230 |
| Build type | Release |
| Flags | `/DWIN32 /D_WINDOWS /EHsc /O2 /Ob2 /DNDEBUG ; viscore adds /fp:precise` |
| CPU | 13th Gen Intel(R) Core(TM) i7-1355U (laptop-class part) |
| Logical cores | 12 (one used) |
| OS | Windows 10.0 build 26200 |
| Positions per pass | 64 |
| Samples per mode | 15 |
| Time budget per mode and row | 10 s |
| World | 1600x1000 |

The machine was not otherwise loaded as far as could be seen, but power and thermal state were not controlled or recorded (see Limitations).

## Method

1. **Frozen inputs.** Each scene is built once per (layout, n, seed) with the generators in `core/scenes.cpp` (`makeScene`). The light path is 64 positions on a closed curve, `x = W(0.5 + 0.42 sin(2*pi*3t + 0.6))`, `y = H(0.5 + 0.42 sin(2*pi*2t))`, `t = i/64`, computed once (`lightPath` in `bench/main.cpp`). Both modes use the same positions in the same order.
2. **Correctness gate.** Before any timing, Brute and Grid are run at every position that will be timed and the two polygons must be equal with `==` on every `double`. On a mismatch the program prints kind, n, seed, position and light, writes nothing for that row and exits with status 1. The gate passed on every row of the committed results (the run completed with all 66 main rows and 10 sweep rows written).
3. **Timing.** `std::chrono::steady_clock` around a loop of `computeVisibility` calls over the positions (one *sample* is one pass over the positions). The reported per-query time is the pass time divided by the number of positions. Sort and cast times are the means of `sortUs`/`castUs` over the pass. Reported: the median, the minimum and the maximum over the samples. (With at most 15 samples a nearest-rank 95th percentile is the maximum, so the maximum is reported under its own name.)
4. **Warm-up.** The sizing queries (step 7) and the gate pass warm both modes; one more warm-up pass is run when a pass is cheap (under a tenth of the budget).
5. **Order.** Samples alternate Brute-then-Grid and Grid-then-Brute.
6. **Output use.** The polygon size and wall-test count of every timed query are accumulated into a sink printed at the end of the run (`output checksum sink`), so the queries cannot be removed by the optimizer. The CSV column `checksum` is a vertex checksum of the gate pass.
7. **Budget.** Brute can take seconds per query on large scenes. The per-query time of each mode is estimated from up to five queries spread along the path (if the first Brute query already exceeds a third of the budget, only that one is used). The number of positions `m` is `clamp(budget / (3 * Brute query time), 1, 64)`, and both modes then use the same `m` evenly spaced positions. The number of samples per mode is `clamp(budget / (m * query time), 3, 15)`. The CSV records `positions_used`, `brute_samples` and `grid_samples`.
8. **Edit costs.** `setWalls` with the same wall list is repeated (up to 15 times, fewer when slow) and the median of `RebuildStats` is taken; then `addWall` of a short wall at the world centre followed by `removeWall` of it is timed as a pair, the same number of times. Each of these runs a full rebuild.

Where the reduction applied (rows with fewer than 64 positions or fewer than 15 samples): see the last columns of the main table. Rows with a single position: Cluster n=4000, Long walls n=2000, Long walls n=4000 (all three seeds each). On those rows the medians describe one light position, not the path.

## Results

Columns that combine seeds (k, T, times, counters) are the median over the three seeds, computed per column; the speedup columns are the median of the per-seed speedups, so they need not equal the ratio of the medians shown. Times are per query. The last column is the largest ratio of a row's maximum sample to its median sample among the three seeds.

| Layout | n | k | T | Brute median | Grid median | Speedup (total) | Speedup (cast only) | Brute wall tests | Grid wall tests | Grid cells walked | Positions used | Brute / Grid samples | Largest max/median (Brute / Grid) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|
| Scatter | 250 | 11 | 515 | 1.35 ms | 183.3 us | 7.4x | 12.3x | 130,810 | 3,898 | 3,045 | 64 of 64 | 15 / 15 | 1.43 / 1.50 |
| Scatter | 500 | 41 | 1045 | 5.09 ms | 300.2 us | 17.1x | 36.7x | 526,680 | 6,990 | 4,648 | 64 of 64 | 11-15 / 15 | 1.31 / 2.90 |
| Scatter | 1000 | 171 | 2175 | 20.6 ms | 655.2 us | 31.6x | 83.7x | 2,183,700 | 12,617 | 7,482 | 64 of 64 | 5-8 / 15 | 1.34 / 1.65 |
| Scatter | 2000 | 705 | 4709 | 86.9 ms | 1.34 ms | 66.6x | 208.9x | 9,436,836 | 25,950 | 12,869 | 36-38 of 64 | 3 / 15 | 1.15 / 1.22 |
| Scatter | 4000 | 2802 | 10806 | 400.4 ms | 3.28 ms | 118.2x | 398.8x | 43,267,224 | 60,820 | 25,454 | 8 of 64 | 3 / 15 | 1.23 / 1.55 |
| Maze | 250 | 46 | 348 | 1.05 ms | 87.0 us | 12.7x | 29.7x | 88,249 | 1,396 | 786 | 64 of 64 | 15 / 15 | 1.26 / 1.64 |
| Maze | 500 | 116 | 670 | 3.33 ms | 153.7 us | 22.5x | 61.1x | 337,680 | 2,719 | 1,474 | 64 of 64 | 15 / 15 | 1.31 / 1.36 |
| Maze | 1000 | 316 | 1402 | 13.4 ms | 325.7 us | 41.0x | 115.1x | 1,407,608 | 6,454 | 2,972 | 64 of 64 | 10-12 / 15 | 1.27 / 1.17 |
| Maze | 2000 | 1019 | 3210 | 58.9 ms | 801.6 us | 77.8x | 233.1x | 6,432,840 | 15,483 | 6,354 | 51-58 of 64 | 3 / 15 | 1.09 / 1.17 |
| Maze | 4000 | 3040 | 7325 | 256.1 ms | 2.02 ms | 127.1x | 349.3x | 29,260,565 | 48,367 | 16,257 | 12-13 of 64 | 3 / 15 | 1.08 / 1.29 |
| Long walls | 250 | 1312 | 1816 | 6.06 ms | 783.1 us | 7.7x | 11.2x | 461,264 | 34,445 | 2,383 | 64 of 64 | 15 / 15 | 1.87 / 1.82 |
| Long walls | 500 | 5270 | 6274 | 39.9 ms | 3.50 ms | 11.6x | 16.0x | 3,162,096 | 170,081 | 7,512 | 64 of 64 | 3-4 / 15 | 1.04 / 1.08 |
| Long walls | 1000 | 22486 | 24490 | 349.1 ms | 18.2 ms | 18.4x | 24.8x | 24,587,960 | 881,320 | 26,546 | 9-10 of 64 | 3 / 15 | 1.11 / 1.53 |
| Long walls | 2000 | 81714 | 85718 | 2.55 s | 99.2 ms | 26.7x | 33.1x | 171,778,872 | 5,407,626 | 119,683 | 1 of 64 | 4 / 15 | 1.09 / 1.29 |
| Long walls | 4000 | 357883 | 365887 | 23.65 s | 696.0 ms | 33.2x | 37.3x | 1,465,011,548 | 44,989,172 | 552,016 | 1 of 64 | 3 / 13-14 | 1.02 / 1.22 |
| Cluster | 250 | 460 | 964 | 2.62 ms | 1.71 ms | 1.5x | 1.6x | 244,856 | 142,137 | 9,480 | 64 of 64 | 15 / 15 | 1.21 / 1.49 |
| Cluster | 500 | 1710 | 2714 | 13.9 ms | 4.64 ms | 3.0x | 3.2x | 1,367,856 | 384,459 | 35,294 | 64 of 64 | 10-12 / 15 | 1.39 / 1.44 |
| Cluster | 1000 | 6877 | 8881 | 88.1 ms | 18.7 ms | 4.8x | 5.3x | 8,916,524 | 1,587,957 | 155,449 | 36-39 of 64 | 3 / 12-15 | 1.03 / 2.72 |
| Cluster | 2000 | 26930 | 30934 | 708.6 ms | 74.4 ms | 9.4x | 10.3x | 61,991,736 | 5,796,338 | 654,293 | 3-5 of 64 | 3 / 15 | 1.14 / 1.27 |
| Cluster | 4000 | 107335 | 115339 | 4.91 s | 167.6 ms | 29.3x | 35.9x | 461,817,356 | 12,826,645 | 1,252,751 | 1 of 64 | 3 / 15 | 1.08 / 1.36 |

Grid structure for seed 1 (the automatic resolution; see [DECISIONS.md](DECISIONS.md)):

| Layout | n | walls inserted | cols x rows | Registrations | Max walls per cell |
|---|---:|---:|---|---:|---:|
| Scatter | 250 | 250 | 20 x 13 | 424 | 6 |
| Scatter | 1000 | 1000 | 40 x 25 | 2,059 | 7 |
| Scatter | 4000 | 4000 | 80 x 50 | 11,834 | 10 |
| Maze | 250 | 250 | 20 x 13 | 645 | 8 |
| Maze | 1000 | 1000 | 40 x 25 | 2,630 | 7 |
| Maze | 4000 | 4000 | 80 x 50 | 12,899 | 11 |
| Long walls | 250 | 250 | 20 x 13 | 4,530 | 29 |
| Long walls | 1000 | 1000 | 40 x 25 | 34,807 | 52 |
| Long walls | 4000 | 4000 | 80 x 50 | 272,772 | 103 |
| Cluster | 250 | 250 | 20 x 13 | 342 | 241 |
| Cluster | 1000 | 1000 | 40 x 25 | 1,429 | 401 |
| Cluster | 4000 | 4000 | 80 x 50 | 6,706 | 679 |

Split of the Grid time into its sort and cast parts (median over seeds of each column):

| Layout | n | Grid median | of which sort | of which cast | sort share of Grid total |
|---|---:|---:|---:|---:|---:|
| Scatter | 250 | 183.3 us | 79.8 us | 103.7 us | 44% |
| Scatter | 1000 | 655.2 us | 412.4 us | 242.5 us | 63% |
| Scatter | 4000 | 3.28 ms | 2.31 ms | 966.9 us | 70% |
| Maze | 250 | 87.0 us | 48.7 us | 36.0 us | 56% |
| Maze | 1000 | 325.7 us | 210.1 us | 114.1 us | 65% |
| Maze | 4000 | 2.02 ms | 1.30 ms | 729.4 us | 64% |
| Long walls | 250 | 783.1 us | 269.0 us | 517.5 us | 34% |
| Long walls | 1000 | 18.2 ms | 4.60 ms | 13.5 ms | 25% |
| Long walls | 4000 | 696.0 ms | 78.7 ms | 621.8 ms | 11% |
| Cluster | 250 | 1.71 ms | 153.9 us | 1.55 ms | 9% |
| Cluster | 1000 | 18.7 ms | 1.82 ms | 16.9 ms | 10% |
| Cluster | 4000 | 167.6 ms | 31.6 ms | 135.8 ms | 19% |

## Small n

Scatter with 16 and 64 walls, 64 positions, all 15 samples used:

| n | seed | walls | k | T | Brute median | Grid median | Speedup (total) | Speedup (cast only) | Grid sort / cast |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 16 | 1 | 16 | 0 | 36 | 11.2 us | 10.0 us | 1.12x | 1.15x | 2.2 / 7.8 us |
| 16 | 2 | 16 | 0 | 36 | 11.6 us | 10.3 us | 1.13x | 1.16x | 2.5 / 7.8 us |
| 16 | 3 | 16 | 0 | 36 | 11.2 us | 10.1 us | 1.11x | 1.14x | 2.3 / 7.8 us |
| 64 | 1 | 64 | 2 | 134 | 98.3 us | 44.0 us | 2.24x | 2.78x | 13.3 / 30.6 us |
| 64 | 2 | 64 | 0 | 132 | 97.0 us | 45.8 us | 2.12x | 2.66x | 13.5 / 31.5 us |
| 64 | 3 | 64 | 0 | 132 | 100.0 us | 45.0 us | 2.22x | 2.78x | 13.5 / 31.2 us |

## Grid-resolution sweep

n = 1000, seed 1. Scale multiplies the automatic columns and rows (`Scene::autoResolution`) and the result is passed to `Scene::setGridResolution`. Scatter used 64 positions; Long walls used 8-10 positions and 3 Brute samples.

| Layout | scale | cols x rows | Registrations | Max walls per cell | Grid median | sort / cast | Grid wall tests | Cells walked | Speedup (total) | Grid rebuild | Add+remove pair | Positions |
|---|---:|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|
| Scatter | 0.25x | 10 x 6 | 1,232 | 30 | 1.31 ms | 406 / 897 us | 73,782 | 3,690 | 15.6x | 94.7 us | 849.1 us | 64 |
| Scatter | 0.5x | 20 x 13 | 1,500 | 12 | 729.0 us | 390 / 341 us | 25,164 | 4,816 | 28.8x | 75.0 us | 578.6 us | 64 |
| Scatter | 1x | 40 x 25 | 2,059 | 7 | 656.3 us | 415 / 240 us | 12,617 | 7,326 | 33.4x | 91.1 us | 605.1 us | 64 |
| Scatter | 2x | 80 x 50 | 3,125 | 5 | 624.3 us | 421 / 207 us | 7,027 | 12,617 | 35.9x | 141.8 us | 679.5 us | 64 |
| Scatter | 4x | 160 x 100 | 5,260 | 4 | 594.9 us | 379 / 214 us | 4,406 | 22,930 | 35.6x | 252.7 us | 804.9 us | 64 |
| Long walls | 0.25x | 10 x 6 | 9,279 | 195 | 62.2 ms | 4559 / 57816 us | 3,749,987 | 25,731 | 5.1x | 192.4 us | 9.64 ms | 9 |
| Long walls | 0.5x | 20 x 13 | 17,910 | 90 | 32.9 ms | 4745 / 28216 us | 1,764,858 | 25,856 | 10.9x | 352.3 us | 9.86 ms | 10 |
| Long walls | 1x | 40 x 25 | 34,807 | 52 | 21.1 ms | 5383 / 15904 us | 857,126 | 25,229 | 20.8x | 611.7 us | 13.0 ms | 9 |
| Long walls | 2x | 80 x 50 | 68,615 | 31 | 14.0 ms | 4440 / 9698 us | 630,729 | 36,471 | 27.5x | 1.14 ms | 11.6 ms | 8 |
| Long walls | 4x | 160 x 100 | 136,191 | 18 | 8.00 ms | 4028 / 3947 us | 273,116 | 34,114 | 37.2x | 2.02 ms | 12.1 ms | 10 |

## Edit costs

Median over seeds. Both numbers are paid once per edit, not per frame. `addWall` and `removeWall` each rebuild everything, so the pair costs about two rebuilds.

| Layout | n | Grid build (setWalls) | Crossings + targets (setWalls) | addWall + removeWall pair |
|---|---:|---:|---:|---:|
| Scatter | 250 | 14.4 us | 18.1 us | 69.6 us |
| Scatter | 1000 | 98.7 us | 227.8 us | 643.4 us |
| Scatter | 4000 | 485.4 us | 1.43 ms | 3.82 ms |
| Maze | 250 | 24.0 us | 80.2 us | 221.0 us |
| Maze | 1000 | 103.6 us | 411.4 us | 1.05 ms |
| Maze | 4000 | 648.1 us | 2.11 ms | 5.75 ms |
| Long walls | 250 | 74.8 us | 304.5 us | 767.1 us |
| Long walls | 1000 | 553.7 us | 4.45 ms | 9.93 ms |
| Long walls | 4000 | 4.27 ms | 69.6 ms | 145.9 ms |
| Cluster | 250 | 15.1 us | 424.2 us | 866.0 us |
| Cluster | 1000 | 74.5 us | 3.66 ms | 7.13 ms |
| Cluster | 4000 | 289.3 us | 35.2 ms | 70.9 ms |

## Where the grid loses or barely wins

- **Cluster at small n.** Cluster n=250: speedup 1.52x, 1.79x, 1.53x for seeds 1-3. All walls lie in a patch of a few cells (max walls in one cell: 241, 186, 219 for seeds 1-3), so a ray that enters the patch tests most of them. Grid wall tests per query against Brute's: seed 1: 151,584 of 246,888; seed 2: 110,754 of 244,856; seed 3: 142,137 of 243,078. The grid saves little because it cannot separate walls that share cells. At n=4000 the same layout shows 24.6x, 43.8x, 29.3x (seeds 1-3); for seed 1 the Grid tests are 16,090,211 against 460,271,812.
- **n = 16.** Scatter with 16 walls: 1.12x, 1.13x, 1.11x (Brute 11.21, 11.61, 11.23 us; Grid 10.03, 10.29, 10.10 us). The scene has no crossings (k = 0) and T = 36 targets, so the per-query work is a few microseconds in either mode and the walk's setup cost is of the same order as the tests it saves.
- **Sorting dominates the Grid total on Scatter and Maze.** In 27 of 66 rows the median Grid sort time is larger than the Grid cast time; all of them are Maze or Scatter rows. The sort is `O(T log T)` with the exact comparator and is the same in both modes, so the grid does not reduce it. For Scatter n=1000, seed 1 the Grid median is 670.1 us, of which 412.4 us is sort. The cast-only speedup (78.2x at that row) is therefore larger than the total speedup (30.7x).
- **Long walls: large k makes every query expensive in both modes.** The algorithm casts one ray per target (collinear targets share one) whether or not the target is visible: over all 66 rows the mean rays per query divided by T is between 0.998 and 1.000. Two families of near-parallel long walls produce many crossings, hence many targets. Long walls n=1000, seed 1: k = 21,722, T = 23,726, rays per query 23,726, Brute 314.8 ms, Grid 17.1 ms. At n=4000, seed 1: k = 337,930, T = 345,934, Brute 21.78 s, Grid 696.0 ms. The grid still reduces wall tests (1,385,119,736 to 44,989,172 at n=4000, seed 1), but the number of rays is fixed by T, and each long wall is registered in many cells (max 103 walls in a cell at n=4000; 272,772 registrations).
- **Finer grids cost registrations and rebuild time.** In the sweep, Long walls improves with each doubling up to 4x the automatic resolution (Grid 62.2 ms at 0.25x, 8.00 ms at 4x) while registrations grow from 9,279 to 136,191 and the grid build from 192.4 us to 2.02 ms. Scatter changes little above 1x (656.3 us, 624.3 us, 594.9 us at 1x, 2x, 4x) while its grid build grows from 91.1 us to 252.7 us.

## Limitations

- **Hardware and drift.** One laptop-class CPU (see Environment); clocks and power state were not pinned. Rows run minutes apart; compare Brute and Grid within a row (they alternate in time) and treat absolute times as indicative. For a check of consistency: Brute for the Scatter n=1000, seed 1 scene is 20.6 ms in the main table row and 21.9 ms in the 1x sweep row.
- **Spread.** The largest maximum-to-median ratios are 2.90 (Grid, Scatter n=500 seed 2) and 1.87 (Brute, Long walls n=250 seed 3); minima and maxima of every row are in `results.csv` (`*_min_us`, `*_max_us`).
- **Few samples and positions on the largest rows.** See the Positions and Samples columns. Brute rows at n >= 2000 have 3 or 4 samples; the n = 1000 rows have 3 to 10.
- **One world size.** All scenes are 1600 x 1000 with one light, single-threaded.
- **Gate scope.** The Brute/Grid equality gate covers the positions timed in each row, which is a single position on the single-position rows. Broader equality checks are in the test suite (`differential: ...` test cases in `tests/test_differential.cpp`).
- Results are for the build in `results.meta.txt`; other compilers, flags or machines were not measured.
