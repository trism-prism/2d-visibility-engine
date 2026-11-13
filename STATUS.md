# Status

Last updated: 2026-11-13. All five stages are complete; the required scope is delivered. The demo interface was redesigned on 2026-11-13 and polished the same day (the two sections below).

## Acceptance

| Requirement | Result | Evidence |
|---|---|---|
| Clean build | passed | Fresh `release` configure + build, 0 warnings under `/W4` in project targets (also `debug`, `asan`) |
| Core builds without graphics | passed | `-DVIS_BUILD_APP=OFF` in `build\no-app` builds `viscore`, `vis_tests`, `vis_bench`; only doctest is fetched |
| Tests | passed | `vis_tests`: 60 cases, 757,017 assertions (release); 419,725 (debug) and 393,784 (asan) at reduced iteration counts. `ctest --preset release`: 4/4 (see "Polish") |
| AddressSanitizer | passed for core, tests and benchmark smoke run; **not run for the app** | `asan` preset builds without the app |
| App interactions | passed through scripted input replay; **not exercised by a person or by OS-level mouse events** | `tests/scripts/main.script` (CTest `app_script`): light drag, draw, rejected wall, delete, boundary delete refused, panel click ignored by canvas, drag across panel, mode switch, reset, clear, stress loads, probe overlay across scene changes |
| Equivalent outputs | passed | Differential tests (bitwise equal polygons), benchmark gate on every row, in-app Compare reports identical |
| Independent correctness | passed | Hand-computed polygons, point-membership and edge-probe oracles that do not call `rayHit` or the grid, area by dense sampling, `orient` against exact integer arithmetic |
| Debug overlay accuracy | passed by inspection | Overlay draws only from `Trace`, `Grid`, `Scene` (code review); captures in `docs/images` viewed |
| Stress scenes | passed | Four seeded layouts, 100-5000 walls, default 1000; captures viewed |
| Release benchmark | passed | `bench/results/results.csv` (66 rows), `grid_sweep.csv` (10 rows), `results.meta.txt`; 1340 s run; see `docs/BENCHMARK.md` |
| Independent review | done | One review pass; 15 findings, all accepted and fixed (1 high: stale probe index; 4 medium) |

## UI refresh (2026-11-13)

The desktop interface was redesigned for a first-time viewer (commit "Refresh the demo UI for newcomers"): Simple and Engine levels, method cards ("All walls" = Baseline, "Grid" = Grid), renamed overlays, a work meter, scene cards with a wall-count slider, a legend, a six-step guided tour that starts on a plain launch, and cursor feedback. The reasoning is decision 12 in `docs/DECISIONS.md`; the behaviour is described in `README.md`. `core/`, `bench/` and the benchmark results were not touched by it.

| Check | Result | Evidence |
|---|---|---|
| Release tests | passed | `.\scripts\dev.ps1 ctest --preset release`, run again on 2026-11-13 at the time of that refresh: 3/3 (`bench_quick` 1.13 s, `vis_tests` 14.37 s, `app_script` 26.03 s); superseded by the Polish rows below |
| `vis_tests` unchanged | passed | `build/release/tests/vis_tests.exe`, run again on 2026-11-13: 60 cases, 757,017 assertions, 0 failed |
| Work meter | passed by script | `tests/scripts/main.script`: Baseline figure equals rays x walls, the grid figure equals what Grid mode reports, the extra pass and the skip path, on the demo room and a 1,000-wall scene |
| Simple panel headroom at 1280 x 800 | passed by script, measured 2026-11-13 | `panel_slack` (free pixels under the panel): 54 on the demo room, 39 with a two-line probe caption, 24 on Long walls with the slow note (it was -4 there, so the panel scrolled, before the "not measured" sentence, the slow note and an overlapping in-bar label were shortened or removed; the later wording changes shortened the labels further, and these figures were not re-measured after each of them). `main.script` asserts no overflow and more than 15 px on that scene. At 900 x 600 the Simple panel scrolls (slack about -150 to -200 px); that was not changed |
| Screenshots | viewed | `docs/images` are captures from `tests/scripts/captures.script` (`build/captures`), viewed before use |

Not checked in this refresh: hands-on use with a physical mouse and keyboard (the script injects events into ImGui, so the real OS cursor shape is never exercised; the `cursor` field only shows what the application asked for), other displays and DPI settings, and whether a person seeing it for the first time understands the meter.

## Polish (2026-11-13)

Follow-up changes after the refresh, in the commits from "Keep the tallest Simple panel inside 1280x800" to "Shorten every label, caption and tooltip in the app". `core/` changed once (sort fast path); `bench/` and the benchmark results were not touched.

| Change | Result | Evidence |
|---|---|---|
| Legend row fix | done | The two-colour row under "One ray" ("Two colours: sides hit different walls") is drawn dimmed when the probed ray's two sides end at the same wall; script variable `legend_pair` (`full`, `dim`, `none`) |
| Simple panel headroom at 1280 x 800 | done | Not-measured and slow-scene text shortened, a clipped bar label dropped; `panel_slack` figures in the refresh table above, asserted by `main.script` |
| Regression tests | passed | Two review fixes now have automated tests: the exact boundary tie (a user wall drawn with Shift from corner to corner, picked before the boundary wall; script command `shift on|off` added) in `app_script`, and script-load failure in the new CTest `app_script_missing` (exit code 2 and "cannot read script") |
| Release tests | passed | `ctest --preset release`: 4/4 (`app_script` about 8-10 s) |
| Debug tests | passed | `ctest --preset debug`: 4/4 in about 133 s in total (`app_script` about 91 s; it was about 7 minutes before) |
| Work meter on heavy scenes | done | Above 20,000,000 rays x walls, All walls mode shows "not measured" while the light or scene changes ("Measuring when you stop."); one update later one untimed extra Grid pass fills in the figure (`meter_source` = `extra_pass`). Before, it was never measured there. Updates slower than 50 ms are repeated once more, not 8 times |
| Long walls opacity | done | Wall opacity also follows total wall length, so Long walls at 1,000 walls is drawn much fainter; skipped walls in the One ray view fade too |
| Sort fast path | done, partly measured | `core/visibility.cpp` caches each target's difference from the light and applies the same floating-point filter as `orient` first, so the result is bitwise identical. A throwaway harness gave a sort about 30% faster and a total query about 20-30% faster on Scatter and Maze at n=1000. The full benchmark was **not** rerun: `bench/results/*` and `docs/BENCHMARK.md` predate the change |
| DPI window scaling | done, not verified on a scaled display | The window and its 900 x 600 minimum scale with the display's DPI at launch (`GLFW_SCALE_TO_MONITOR`); moving the window to a monitor with another scale does not rescale the interface. Not tested at 150% or 200% |
| Terse wording | done | Every label, caption and tooltip shortened ("Simple" / "Engine", "All walls" / "Grid", "Show", "Work", "Tour", scene cards Demo, Scatter, Maze, Long walls, Cluster, six tour steps, status bar messages); screenshots in `docs/images` regenerated and the documents updated to the new labels |

## Known limitations

- Verified on one machine only (Windows 11, MSVC 19.44, Intel Iris Xe). Other platforms and compilers are untested.
- The algorithm casts one ray per target whether or not the target is visible. Crossing-heavy scenes (Long walls: k about 22 n at n=1000) are slow in both modes: about 17 ms per query on the grid at n=1000, about 700 ms at n=4000.
- The grid barely wins on tiny scenes (1.1x at 16 walls) and on the Cluster layout at 250 walls (1.5x).
- The exact angular sort is shared by both modes and exceeds the grid's casting time on 27 of 66 benchmark rows (committed results; the sort has since been made faster, see "Polish", and the benchmark was not rerun).
- Every edit rebuilds the grid, crossings and targets from scratch (about 0.6 ms per add/remove pair on Scatter n=1000, about 146 ms on Long walls n=4000).
- Benchmark numbers come from a laptop CPU; the largest rows use one light position and 3-4 baseline samples. Ratios are more stable than absolute times.

## Not checked

- Hands-on use with a physical mouse and keyboard (including the new cursor shapes).
- DPI scaling and multi-monitor: not verified on any scaled display; also window sizes above the screen size.
- The app under AddressSanitizer.
- The full benchmark after the sort change (`bench/results/*` and `docs/BENCHMARK.md` are from before it).

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
