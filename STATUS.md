# Status

Last updated: 2026-11-12 (paused at user request; resume on "go")

## Progress

| Stage | State | Evidence |
|---|---|---|
| 1. Environment inspected, design recorded | done | `docs/DESIGN.md` |
| 2. Build, headless core, tests, smoke app | done, committed `14dbe82` | 51 test cases pass in release (686,292 assertions), debug, asan; `build/smoke.png` viewed and correct |
| 3. Optimized (grid) mode + equivalence | core done in `14dbe82`: Brute vs Grid bitwise equal in the differential test; overlay is part of stage 4 work | tests |
| 4. Editing, overlay, stress scenes, telemetry, polish | **in progress, uncommitted** (Sonnet worker, milestone 2) | not yet reviewed |
| 5. Benchmarks, review, docs, acceptance, GitHub push | not started | - |

## Decisions made so far

- Stack: C++20 / MSVC, CMake + Ninja presets, GLFW 3.4, Dear ImGui v1.92.9, doctest v2.4.12 (tags verified upstream by the worker).
- Algorithm: targeted rays at wall endpoints and intersection points, exact one-sided hit limits (no epsilon offsets); baseline tests every wall per ray, optimized mode walks a uniform grid.
- Contract change after a test failure: angular sort and side tests now use one exact `orient` predicate. The earlier `atan2` sort + rounded cross product dropped polygon vertices for points collinear in decimal but not binary (regression test kept). Cost measured on Scatter n=1000: Grid query ~410 us -> ~700-750 us, Brute ~19 ms -> ~30 ms.

## Open items carried into the next steps

- Milestone 2 brief includes a core fix: a proper crossing whose rounded denominator is zero must not drop the hit (light leak); regression test required.
- GitHub: no blocker. `gh` shows the stale login `trism-dev`, but the API confirms the account is `trism-prism` (id 307220603). `trism-prism/2d-visibility-engine` does not exist yet; nothing has been pushed.

## Exact next action on resume

1. Check whether the milestone 2 worker finished or was interrupted; inspect the working tree (`git status`), rebuild `release`, rerun `ctest`.
2. If interrupted, resume the same worker with the remaining milestone 2 items (brief: full UI, overlays from real `Trace`, dashboard, stress buttons, `--script` replay + CTest `app` test, captures under `build/captures`).
3. Review its captures and script logs, commit, then: headless Release benchmark + CSV, one Sonnet review, documentation set, acceptance table, private repo creation and the single initial push to `main`.

## Commands

```
.\scripts\dev.ps1 cmake --preset release
.\scripts\dev.ps1 cmake --build --preset release
.\scripts\dev.ps1 ctest --preset release
.\build\release\app\vis_app.exe
```
