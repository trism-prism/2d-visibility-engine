# Status

Last updated: 2026-11-13. All five stages are complete; the required scope is delivered. The demo interface was redesigned on 2026-11-13 (section below).

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

## UI refresh (2026-11-13)

The desktop interface was redesigned for a first-time viewer (commit "Refresh the demo UI for newcomers"): Simple and Engine details levels, method cards ("Check every wall" = Baseline, "Use the grid" = Grid), renamed overlays, a work meter, scene cards with a wall-count slider, a legend, a six-step guided tour that starts on a plain launch, and cursor feedback. The reasoning is decision 12 in `docs/DECISIONS.md`; the behaviour is described in `README.md`. `core/`, `bench/` and the benchmark results were not touched by it.

| Check | Result | Evidence |
|---|---|---|
| Release tests | passed | `.\scripts\dev.ps1 ctest --preset release`, run again on 2026-11-13: 3/3 (`bench_quick` 1.13 s, `vis_tests` 14.37 s, `app_script` 26.03 s) |
| `vis_tests` unchanged | passed | `build/release/tests/vis_tests.exe`, run again on 2026-11-13: 60 cases, 757,017 assertions, 0 failed |
| Debug tests | reported, not rerun | 3/3 with the `app_script` timeout raised to 900 s (`app/CMakeLists.txt`); taken from the hand-over for this change, not rerun for this update |
| Work meter | passed by script | `tests/scripts/main.script`: Baseline figure equals rays x walls, the grid figure equals what Grid mode reports, the extra pass and the skip path, on the demo room and a 1,000-wall scene |
| Simple panel headroom at 1280 x 800 | passed by script, measured 2026-11-13 | `panel_slack` (free pixels under the panel): 54 on the demo room, 39 with a two-line probe caption, 24 on Long walls with the slow note (it was -4 there, so the panel scrolled, before the "not measured" sentence, the slow note and an overlapping in-bar label were shortened or removed). `main.script` asserts no overflow and more than 15 px on that scene. At 900 x 600 the Simple panel scrolls (slack about -150 to -200 px); that was not changed |
| Screenshots | viewed | `docs/images` are captures from `tests/scripts/captures.script` (`build/captures`), viewed before use |

Not checked in this refresh: hands-on use with a physical mouse and keyboard (the script injects events into ImGui, so the real OS cursor shape is never exercised; the `cursor` field only shows what the application asked for), other displays and DPI settings, and whether a person seeing it for the first time understands the meter.

## Known limitations

- Verified on one machine only (Windows 11, MSVC 19.44, Intel Iris Xe). Other platforms and compilers are untested.
- The algorithm casts one ray per target whether or not the target is visible. Crossing-heavy scenes (Long walls: k about 22 n at n=1000) are slow in both modes: about 17 ms per query on the grid at n=1000, about 700 ms at n=4000.
- The grid barely wins on tiny scenes (1.1x at 16 walls) and on the Cluster layout at 250 walls (1.5x).
- The exact angular sort is shared by both modes and exceeds the grid's casting time on 27 of 66 benchmark rows.
- Every edit rebuilds the grid, crossings and targets from scratch (about 0.6 ms per add/remove pair on Scatter n=1000, about 146 ms on Long walls n=4000).
- Benchmark numbers come from a laptop CPU; the largest rows use one light position and 3-4 baseline samples. Ratios are more stable than absolute times.
- Two review fixes lack an automated regression test: script-load failure shutdown (checked by hand) and deleting a user wall that exactly ties with a boundary wall (script cannot produce the exact tie).

## Not checked

- Hands-on use with a physical mouse and keyboard (including the new cursor shapes), high-DPI displays, multi-monitor, window sizes above the screen size.
- The app under AddressSanitizer.

## Next step

Run `.\build\release\app\vis_app.exe` and follow "Showing it to someone" and "A longer demonstration" in `README.md` by hand. If crossing-heavy scenes need to be interactive, the angular-sweep alternative in `docs/DECISIONS.md` is the change to evaluate.

## Commands

```
.\scripts\dev.ps1 cmake --preset release
.\scripts\dev.ps1 cmake --build --preset release
.\scripts\dev.ps1 ctest --preset release
.\build\release\app\vis_app.exe
.\build\release\bench\vis_bench.exe --out bench\results\results.csv
```
