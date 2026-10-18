[Back to the README](../README.md)

# The demo application in detail

A reference for `vis_app`. Flags and the script language are in [RUNNING.md](RUNNING.md). The algorithm is explained in [WALKTHROUGH.md](WALKTHROUGH.md) and [ARCHITECTURE.md](ARCHITECTURE.md). Measured costs are in [BENCHMARK.md](BENCHMARK.md).

Contents: [Window layout](#window-layout) · [Simple and Engine](#two-levels-simple-and-engine) · [The panel](#the-panel) · [Legend and wall fading](#legend-and-wall-fading) · [The work meter](#the-work-meter) · [Guided tour](#guided-tour) · [Mouse and keys](#mouse-and-keys) · [What Engine adds](#what-engine-adds) · [Display scaling](#display-scaling)

## Window layout

- **Left:** the room (the world, letterboxed).
- **Right:** the panel.
- **Below the room:** a strip that holds the legend and the tour card.
- **Bottom:** a one-line status bar. It reads "Tab method · O show · 1-4 scenes · R reset · C clear" until a short message such as "Added · 56 walls" replaces it.

A "Drag me" tag points at the light until you've dragged it once. The smallest window is 900 x 600.

## Two levels: Simple and Engine

The two buttons at the top of the panel switch the level.

- **Simple** is the default. It uses plain words and fits a 1280 x 800 window without scrolling.
- **Engine** adds the precise numbers and terms. Every number there is the same one the first version of the application showed, with the same meaning.

Hover a `(?)` or a row label for a one-sentence explanation of the term.

## The panel

Top to bottom:

| Section | What it holds |
|---|---|
| "Light and walls" | "Move the light, draw walls, compare methods.", the hint "Drag the floor to draw, right-click to erase.", and a "Tour" button |
| Method cards | **All walls** ("Baseline: checks every wall.") is the Baseline back end: every ray is tested against every wall. It's called Baseline in the first version of the interface, the benchmark and these documents, and `Brute` in the code and `--mode brute`. **Grid** ("Checks only nearby walls.") is the Grid back end: a ray only tests the walls in the grid squares it crosses. Under the cards: "Same picture, different amount of work." Both give exactly the same picture. The card in use is highlighted |
| Show | **Light only** (overlay `off`), **Squares** (`grid`, formerly "Grid cells": squares tinted by how many rays crossed them), **One ray** (`probe`, formerly "Probe ray": the ray nearest the cursor, the squares it crossed and the walls it tested), **All rays** (`rays`). A caption under the choice describes what's on screen in one live sentence (see below). "Outline" (formerly "Outline visibility polygon") draws the edge of the lit region, the visibility polygon. In Engine the probe's numbers are listed here: squares crossed, walls tested "X of N", `tMinus`, `tPlus` |
| Work | The work meter, [below](#the-work-meter) |
| Scene cards | **Demo** (key `R`), **Scatter** (`1`), **Maze** (`2`), **Long walls** (`3`), **Cluster** (`4`), each with a one-line description (for example "Where the grid shines."). The **Walls** slider picks 100, 250, 500, 1,000, 2,000, 3,000 or 5,000 walls for the generated scenes. Releasing it rebuilds the current generated scene (it does nothing to the demo room). Then **Clear** (`C`) and **Reset** (`R`). A short orange note appears under the cards when "All walls" would take about 30 ms or more per update in the current scene, for example "All walls is slow here: ~246 ms per update." |
| Engine only | Numbers, Edit cost, Compare, Grid size and a key list, plus a Seed field under the scene cards ([below](#what-engine-adds)) |

Live captions under Show:

| Overlay | Example captions |
|---|---|
| Light only | "Just the result." |
| Squares | "40 x 25 squares; brighter ones see more rays." (in All walls: "No grid here; lines are rays.") |
| One ray | "Hover over the room to pick a ray." / "Passes 2 squares, checks 12 of 59 walls." / "Checks all 59 walls." |
| All rays | "78 rays, one per wall end or crossing." |

## Legend and wall fading

The strip under the room shows the **legend** for the selected overlay (what the orange, grey, cyan and gold marks mean). While the tour is open, the strip also shows the **tour card**. On a narrow window they stack and the legend gets shorter.

| Overlay | Legend title and rows |
|---|---|
| Squares | "Brighter = more rays (most: N)"; "Dot: a wall is in this square"; in All walls mode the one row is "One ray" |
| One ray | "Checked walls" (in All walls: "All walls checked"); "Skipped walls"; "Squares it passed, in order"; "Where it hits"; "Two colours: each side hits a different wall" (drawn dimmed when the probed ray's two sides end at the same wall) |
| All rays | "Each ray, up to its hit" |

The probe markers on the ray read "hit", "stops here" and "other side goes to here".

**Dense-scene fading.** Walls are drawn fainter in dense scenes. The opacity falls with the wall count above 400 walls, and also with the total wall length, so Long walls at 1,000 walls is drawn much fainter. Skipped walls in the One ray view fade the same way.

## The work meter

"Work" compares the two methods in **wall tests**. One wall test is one ray checked against one wall.

What you see:

- Two bars on one shared linear scale. A non-zero value is never drawn thinner than 3 px, and the true number is printed above the bar.
- The method in use is tagged "on".
- A sentence gives the ratio ("Grid does 22% of the work.").
- Below the bars, the arithmetic, for example `78 rays × 59 walls = 4,602 checks`.
- **Time:** the median of the last (up to) 60 real timed queries of the method in use. Simple shows milliseconds. Engine shows microseconds or milliseconds.

Where the two numbers come from (`App::updateMeter`, `app/app.cpp`; `WorkMeter`, `app/app.h`):

- **All walls = rays x walls.** This is exact arithmetic, not an estimate. Baseline tests every wall for every ray, and the interaction script checks that the counter measured in Baseline mode equals this product. "Rays" is the ray count of the latest timed query. "Walls" includes the 4 outer walls.
- **Grid = wall tests measured for the current light position.** There are three cases:
  - With the grid in use, it's the wall-test counter of the timed query.
  - In Baseline with "Run both" on, it's the counter of the timed Grid query that runs alongside.
  - In Baseline with Compare off there's no Grid query, so the application runs **one extra Grid query per update**, only to read this counter. That pass isn't timed, and its result is never added to any displayed time.

  All three give the same count for the same light position (the script checks this).
- **When the extra pass is skipped.** If rays x walls is above 20,000,000 (`kMeterSkipTests`), the extra pass doesn't run while the light or the scene is changing. That way "All walls" never gets slower for the meter's sake.
  - The grid bar then shows "not measured" on a dashed track, and the sentence reads "Measuring once you stop."
  - One update later, once nothing is changing, the application runs one untimed extra Grid pass and the real figure appears (`meter_source` is then `extra_pass`).
  - Before this change the figure was never measured in All walls mode on such scenes.
  - The threshold is a work count, not a time, so it's the same on every machine.
- **Verdict sentences.** The sentence changes to "Grid helps little" when the grid does 90% or more of the baseline work. It's followed by ": walls bunched in a few squares." or ": long walls cover lots of squares." when the scene shows it. Below 1% it reads "Grid does under 1% of the work."
- **Settle rule.** After a change, updates slower than 50 ms are repeated only once more (`settle` in `App::updateVisibility`) instead of the usual 8 times. That's just enough to finish the meter.
- **Slow-scene note.** The orange note estimates a Baseline update as the measured query time when Baseline is in use. Otherwise it uses rays x walls x the cost of one wall test, measured once on a 1,000-wall Scatter scene the first time it's needed. The 30 ms limit is `kSlowMs`.

## Guided tour

Six steps, shown in a card under the room with a counter such as "1 / 6":

| Step | Title | What it sets up |
|---|---|---|
| 1 | Move the light | Demo room, Grid, no overlay |
| 2 | Draw walls | Draw and remove a wall (same setup) |
| 3 | One ray | Grid, One ray overlay |
| 4 | All walls | The same ray, now testing everything (All walls, One ray) |
| 5 | More walls | 1,000 scattered walls (Scatter, Grid) and the meter |
| 6 | Worst case | 1,000 walls packed in one corner (Cluster), where the grid helps least |

Each step sets the scene, method and overlay it talks about. **Next** moves on whether or not you did what the card suggested. **Back**, **Close**, `Esc` and the final **Done** step back or close it. "Tour" in the panel starts it again from step 1 on the demo room (`App::tourStart`, `app/tour.cpp`).

The tour **starts by itself** only in a plain interactive launch: no `--scene`, `--walls`, `--seed`, `--mode`, `--overlay`, `--light` or `--compare` flag, and no `--capture` or `--script`. `--tour` starts it explicitly (`app/main.cpp`).

## Mouse and keys

| Input | Effect |
|---|---|
| Left-drag starting on the light handle (grab radius 22 px with up to 400 user walls, 18 px up to 1,500, 16 px above; multiplied by the display scale) | Move the light; it stays inside the world. Cursor: hand over the handle, four-way arrow while dragging |
| Left-drag elsewhere on the room | Draw a wall (rubber band, crosshair cursor); release adds it. A rejected wall shows the reason in the status bar |
| Shift held while drawing | Snap the wall's end points to existing wall end points within 8 px |
| Right-click within 8 px of a wall | Delete that wall (it's highlighted while hovered). User walls are looked up first, so one lying on the world edge can be deleted. The four outer walls can't be deleted, and the status bar says so |
| `Tab` or `M` | Switch between "All walls" and "Grid". The scene and the light stay as they are |
| `R` | Reset to the demo room (same as the Demo card and the Reset button) |
| `C` | Remove all user walls |
| `O` | Cycle Show: Light only, Squares, One ray, All rays |
| `1` `2` `3` `4` | Load Scatter, Maze, Long walls or Cluster (wall count from the slider, seed from the Seed field) |
| `Esc` | Cancel the wall being drawn; also closes the tour |

Keys and mouse presses are ignored while a text field has focus, or when the press lands on the panel or the tour card. A drag that started on the room keeps working if the cursor crosses the panel.

## What Engine adds

In Engine the panel also has:

| Item | What it is |
|---|---|
| Seed field | Under the scene cards; the seed used by the generated scenes |
| **Run both** (card "Compare", formerly "Compare both modes") | A table of both methods' median query time and wall tests, their time ratio, and "Identical: yes". Both methods then run on every update and the order alternates |
| **Grid size** | "Auto", or manual sliders of 1-256 squares per axis |
| Numbers | Frame time and fps, query median, sort / cast split, walls ("N + 4"), crossings k, targets T, rays cast, wall tests, squares walked, polygon vertices |
| Edit cost | What the rebuild cost after an edit, paid once per edit: grid rebuild and crossings + targets |
| Key list | The keys from the table above |
| Probe numbers | In the Show card: squares crossed, walls tested "X of N", `tMinus`, `tPlus` |

## Display scaling

At launch the window and its minimum size are scaled by the display's DPI scale (`GLFW_SCALE_TO_MONITOR`, interactive runs only). Moving the window to a monitor with a different scale does not rescale the interface.

**This was not verified on a scaled display.** Behaviour at 150% or 200% scaling on the test machine was not tested. Headless runs (`--capture`, `--script`) stay at 100%.
