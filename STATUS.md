# Status

Last updated: 2026-11-12. All five stages are complete; the required scope is delivered.

## Acceptance

| Requirement | Result | Evidence |
|---|---|---|
| Clean build | passed | Fresh `release` configure + build, 0 warnings under `/W4` in project targets (also `debug`, `asan`) |
| Core builds without graphics | passed | `-DVIS_BUILD_APP=OFF` in `build\no-app` builds `viscore`, `vis_tests`, `vis_bench`; only doctest is fetched |
| Tests | passed | `vis_tests`: 60 cases, 757,017 assertions (release); 419,725 (debug) and 393,784 (asan) at reduced iteration counts. `ctest --preset release`: 3/3 |
| AddressSanitizer | passed for core, tests and benchmark smoke run; **not run for the app** | `asan` preset builds without the app |
| App interactions | passed through scripted input replay; **not exercised by a person or by OS-level mouse events** | `tests/scripts/main.script` (CTest `app_script`): light drag, draw, rejected wall, delete, boundary delete refused, panel click ignored by canvas, drag across panel, mode switch, reset, clear, stress loads, probe overlay across scene changes |
| Equivalent outputs | passed | Differential tests (bitwise equal polygons), benchmark gate on every row, in-app Compare reports identical |
| Independent correctness | passed | Hand-computed polygons, point-membership and edge-probe oracles that do not call `rayHit` or the grid, area by dense sampling, `orient` against exact integer arithmetic |
| Debug overlay accuracy | passed by inspection | Overlay draws only from `Trace`, `Grid`, `Scene` (code review); captures in `docs/images` viewed |
| Stress scenes | passed | Four seeded layouts, 100-5000 walls, default 1000; captures viewed |
| Release benchmark | passed | `bench/results/results.csv` (66 rows), `grid_sweep.csv` (10 rows), `results.meta.txt`; 1340 s run; see `docs/BENCHMARK.md` |
| Independent review | done | One review pass; 15 findings, all accepted and fixed (1 high: stale probe index; 4 medium) |

## Known limitations

- Verified on one machine only (Windows 11, MSVC 19.44, Intel Iris Xe). Other platforms and compilers are untested.
- The algorithm casts one ray per target whether or not the target is visible. Crossing-heavy scenes (Long walls: k about 22 n at n=1000) are slow in both modes: about 17 ms per query on the grid at n=1000, about 700 ms at n=4000.
- The grid barely wins on tiny scenes (1.1x at 16 walls) and on the Cluster layout at 250 walls (1.5x).
- The exact angular sort is shared by both modes and exceeds the grid's casting time on 27 of 66 benchmark rows.
- Every edit rebuilds the grid, crossings and targets from scratch (about 0.6 ms per add/remove pair on Scatter n=1000, about 146 ms on Long walls n=4000).
- Benchmark numbers come from a laptop CPU; the largest rows use one light position and 3-4 baseline samples. Ratios are more stable than absolute times.
- Two review fixes lack an automated regression test: script-load failure shutdown (checked by hand) and deleting a user wall that exactly ties with a boundary wall (script cannot produce the exact tie).

## Not checked

- Hands-on use with a physical mouse and keyboard, high-DPI displays, multi-monitor, window sizes above the screen size.
- The app under AddressSanitizer.

## Next step

Run `.\build\release\app\vis_app.exe` and follow the demonstration sequence in `README.md` by hand. If crossing-heavy scenes need to be interactive, the angular-sweep alternative in `docs/DECISIONS.md` is the change to evaluate.

## Commands

```
.\scripts\dev.ps1 cmake --preset release
.\scripts\dev.ps1 cmake --build --preset release
.\scripts\dev.ps1 ctest --preset release
.\build\release\app\vis_app.exe
.\build\release\bench\vis_bench.exe --out bench\results\results.csv
```
