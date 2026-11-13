[Back to the README](../README.md)

# Building, testing, running and benchmarking

Related: [APP.md](APP.md) (the demo application in detail), [BENCHMARK.md](BENCHMARK.md) (benchmark results), [../STATUS.md](../STATUS.md).

## Verified platform and prerequisites

Everything below was built and run on one machine: Windows 11 Pro (build 26200), Intel Core i7-1355U, Intel Iris Xe Graphics (driver 32.0.101.7088), Visual Studio Build Tools 2022 with MSVC 19.44 (`_MSC_FULL_VER` 194435230), the CMake 3.31.6 and Ninja bundled with the Build Tools, Git 2.55. **Other operating systems, compilers and GPUs are untested.** The project files contain non-MSVC branches (`-Wall`, `-ffp-contract=off`), but they have not been built. Only Windows with MSVC was built and run.

You need:

- Windows 11 and Visual Studio Build Tools 2022 with the "Desktop development with C++" workload (this includes the bundled CMake and Ninja; nothing is installed system-wide by this project). `scripts/dev.ps1` expects them at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`; set the environment variable `VIS_VS_PATH` to use another location. The `asan` preset also needs the AddressSanitizer component of the Build Tools.
- Git, and internet access for the first configure: it fetches GLFW 3.4, Dear ImGui v1.92.9 (when building the app) and doctest v2.4.12 (when building the tests) with CMake `FetchContent`.
- A GPU and driver with OpenGL 3.0 or newer. The application sets no context-version hint (GLFW default context) and initialises the Dear ImGui OpenGL3 backend with GLSL `#version 130` (`app/main.cpp`), which is what sets the minimum. Only the Intel Iris Xe listed above was exercised; its OpenGL version string was not recorded.

## Build, test, run, benchmark

All commands are run in PowerShell from the repository root. `scripts/dev.ps1` imports the MSVC x64 developer environment for one command, puts the bundled CMake and Ninja on `PATH`, and restores your environment afterwards.

```powershell
# configure and build (release); first run downloads the dependencies
.\scripts\dev.ps1 cmake --preset release
.\scripts\dev.ps1 cmake --build --preset release

# tests: vis_tests (doctest), app_script and app_script_missing (scripted interaction, script-load failure), bench_quick (benchmark smoke run)
.\scripts\dev.ps1 ctest --preset release
.\scripts\dev.ps1 ctest --preset release -L app      # only the app tests (interaction script, missing-script check)
.\scripts\dev.ps1 ctest --preset release -L bench    # only the benchmark smoke run

# run the application
.\build\release\app\vis_app.exe

# benchmark: quick smoke run (about 2 s) and the full matrix (22 minutes on the verified machine: `done in 1340.1 s`)
.\build\release\bench\vis_bench.exe --out build\bench_quick\results.csv --quick
.\build\release\bench\vis_bench.exe --out bench\results\results.csv
```

### Other presets

`CMakePresets.json` also has `debug` (Debug) and `asan` (RelWithDebInfo with MSVC `/fsanitize=address`; core, tests and benchmark only). They are used the same way, e.g. `.\scripts\dev.ps1 ctest --preset debug`. The debug and asan presets divide the random-trial counts of the tests by 5 and 8 (`VIS_TEST_SCALE`).

### Building without the application

To build without GLFW and Dear ImGui (core, tests, benchmark only):

```powershell
.\scripts\dev.ps1 cmake -S . -B build\no-app -G Ninja -DCMAKE_BUILD_TYPE=Release -DVIS_BUILD_APP=OFF
.\scripts\dev.ps1 cmake --build build\no-app
.\scripts\dev.ps1 ctest --test-dir build\no-app
```

## Command-line flags

```
vis_app [--size WxH] [--scene demo|scatter|maze|long|cluster] [--walls N] [--seed S]
        [--mode brute|grid] [--overlay off|grid|probe|rays] [--light x,y] [--compare]
        [--tour] [--capture file.bmp [--frames N]] [--script file]
```

| Flag | Notes |
|---|---|
| `--mode brute` / `--mode grid` | `brute` selects "All walls" (Baseline), `grid` selects "Grid" |
| `--walls N` | Clamped to 100-5000 |
| `--size WxH` | Clamped to 400-7680 by 300-4320; the window cannot be resized below 900 x 600. Both are scaled by the display's DPI scale in interactive runs; headless runs stay at 100% |
| `--tour` | Opens the guided tour (it starts by itself only in a plain interactive launch, see [APP.md](APP.md)) |
| `--capture file.bmp [--frames N]` | Renders N frames (default 3), writes a 24-bit BMP (parent directories are created) and exits |
| `--script file` | Replays a script and exits when it ends. A missing file exits with code 2 and prints "cannot read script" |

Capture and script runs use a hidden window and no vsync, so the window size is limited by the screen (a `--size 2560x1440` capture produced 1924x1050 on the verified machine).

## Scripts

A script is a text file, one command per line; `#` starts a comment. Events go through ImGui's input queue, so the real canvas input handling runs. Real OS mouse and keyboard delivery through GLFW is not exercised, and neither is the cursor shape the OS displays (the cursor is checked as application state, `cursor`). Hands-on use with a physical mouse has not been done. Commands are documented in `app/script.h`.

### Commands

| Group | Commands |
|---|---|
| Mouse and keys | `shift on\|off` (hold Shift, which snaps a drawn wall to existing end points); `move X Y`; `move @Button Label` (the centre of a panel or tour-card button, for example `move @Engine` or `move @Next`); `wmove X Y` (world coordinates through the app's transform); `down\|up left\|right`; `key Tab\|R\|C\|O\|M\|1\|2\|3\|4\|Escape`; `wait N` (frames) |
| Output | `capture file.bmp`, `log`, `print FIELD...`, `quit` |
| State | `set walls N`, `set seed N`, `set compare on\|off`, `set mode brute\|grid`, `set overlay off\|grid\|probe\|rays`, `set detail simple\|details`, `set tour on\|off` |
| Checks | `save NAME FIELD`; `expect FIELD OP VALUE` with `OP` one of `== != < <= > >= has` (the value may be `$NAME`). The process exits non-zero if any `expect` fails |

### Variables

The `FIELD` names, all handled in `App::field` (`app/app.cpp`):

| Group | Fields |
|---|---|
| Model | `mode` (`grid` or `brute`), `scene`, `overlay`, `compare`, `detail` (`simple` or `details`), `walls`, `walls_total`, `seed`, `light_x`, `light_y`, `status`, `drawing` |
| Query | `k`, `T`, `rays`, `wall_tests`, `cells`, `vertices`, `identical`, `poly_hash`, `query_us`, `query_median_us`, `median_us_brute`, `median_us_grid`, `tests_brute`, `tests_grid`, `rays_x_walls`, `rebuild_grid_us`, `rebuild_isect_us`, `grid_cols`, `grid_rows`, `grid_regs` |
| Work meter | `meter_rays`, `meter_walls`, `meter_baseline`, `meter_grid` (or `unknown`), `meter_source` (`measured`, `extra_pass` or `skipped`; on heavy scenes in All walls mode it is `skipped` while the light or scene is changing and `extra_pass` one update later), `meter_verdict`, `slow_note`, `slow_ms`, `slow_text` |
| Probe | `probe_cells`, `probe_walls`, `probe_tminus`, `probe_tplus` (`none` when there is no valid probe ray), `legend_pair` (the two-colour legend row under "One ray": `full`, `dim` when both sides of the probed ray end at the same wall, or `none` when the row is not listed) |
| Interface | `tour` (`on` or `off`), `tour_step`, `caption`, `cursor` (`arrow`, `hand`, `move` or `cross`), `light_hint`, `panel_overflow`, `panel_scroll`, `panel_slack`, `mouse_x`, `mouse_y`, `scale`, `origin_x`, `origin_y` |

### Scripts in `tests/scripts/`

| File | Purpose |
|---|---|
| `main.script` | The CTest `app_script`: the canvas interaction checks, including the exact boundary tie (a user wall drawn with Shift from corner to corner, picked before the boundary wall), then checks of the interface: Simple fits without scrolling, the work meter figures in both methods, the slow-scene note, captions, cursors, the tour |
| `expect_script_load_failure.cmake` | The CTest `app_script_missing`: a missing script file must exit with code 2 and print "cannot read script" |
| `captures.script` | Writes the review captures (the README screenshots) to `build/captures` |
| `captures_small.script` | The 900 x 600 capture; run with `--size 900x600` |
| `measure.script` | Prints live measurements per stress scene |

## What was run

From clean `build\release` and `build\no-app` directories (the README was written against this state):

| Step | Result |
|---|---|
| Release configure | exit 0, 26 s |
| Release build | exit 0, 42 s, 0 compiler warnings |
| `ctest --preset release` (at the time) | 3 of 3 passed: `bench_quick`, `vis_tests`, `app_script` |
| `ctest -L app` / `ctest -L bench` (at the time) | 1 of 1 each |
| `vis_bench --quick` | exit 0 |
| `vis_app --script tests\scripts\main.script` | exit 0, "script ok" |
| `vis_app ... --capture` with most flags | exit 0 |
| `no-app` configure, build, `ctest` | 2 of 2 (`bench_quick`, `vis_tests`); `_deps` contained only doctest |
| `debug` and `asan` presets (configure, build, ctest) | passed, 3 of 3 and 2 of 2 |
| `vis_tests` | 60 cases; 757,017 assertions (release), 419,725 (debug), 393,784 (asan) |
| Full `vis_bench` run | not repeated for the README; output is in `bench/results/` (22 minutes, `done in 1340.1 s`) |

Since then CTest gained a fourth test, `app_script_missing` (label `app`):

| Step | Result |
|---|---|
| `ctest --preset release` | 4 of 4 (`app_script` takes about 8-10 s) |
| `ctest --preset debug` | 4 of 4 in about 133 s in total (`app_script` about 91 s; it took about 7 minutes before) |
| `ctest -L app` | now runs 2 tests |
| Assertion counts above | not re-counted after the later changes; those did not touch `vis_tests` |
