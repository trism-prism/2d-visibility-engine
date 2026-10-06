#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "widgets.h"

using vis::Vec2;

const char* overlayName(Overlay o) {
    switch (o) {
        case Overlay::Off: return "off";
        case Overlay::Grid: return "grid";
        case Overlay::Probe: return "probe";
        case Overlay::Rays: return "rays";
    }
    return "?";
}

namespace {
const SceneInfo kSceneInfos[] = {
    {vis::SceneKind::Demo, "Demo room", "A small hand-built room. Good for drawing your own walls.", "R"},
    {vis::SceneKind::Scatter, "Scattered walls", "Many short walls spread out evenly. The grid's best case.", "1"},
    {vis::SceneKind::Maze, "Maze", "Rooms and corridors with doors, plus a few long diagonals.", "2"},
    {vis::SceneKind::LongWalls, "Long walls", "Long walls that cross each other a lot. Hard for both methods.", "3"},
    {vis::SceneKind::Cluster, "Tight cluster", "Everything packed into one corner. The grid helps least here.", "4"},
};
std::string wallsPhrase(std::size_t n) { return ui::fmtInt(n) + (n == 1 ? " wall" : " walls"); }
}  // namespace

const SceneInfo& sceneInfo(vis::SceneKind k) {
    for (const SceneInfo& i : kSceneInfos)
        if (i.kind == k) return i;
    return kSceneInfos[0];
}
const SceneInfo* sceneInfos(int& count) {
    count = static_cast<int>(sizeof kSceneInfos / sizeof kSceneInfos[0]);
    return kSceneInfos;
}

double Stat::median() const {
    if (n == 0) return 0.0;
    std::array<double, 60> tmp = v;
    std::nth_element(tmp.begin(), tmp.begin() + n / 2, tmp.begin() + n);
    return tmp[static_cast<std::size_t>(n / 2)];
}

namespace {

float distToSegmentPx(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float ex = b.x - a.x, ey = b.y - a.y;
    const float l2 = ex * ex + ey * ey;
    float t = l2 > 0.0f ? ((p.x - a.x) * ex + (p.y - a.y) * ey) / l2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float cx = a.x + t * ex - p.x, cy = a.y + t * ey - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

std::uint64_t fnv(const std::vector<Vec2>& poly) {
    std::uint64_t h = 1469598103934665603ull;
    for (Vec2 v : poly)
        for (double d : {v.x, v.y}) {
            std::uint64_t bits;
            std::memcpy(&bits, &d, sizeof bits);
            for (int i = 0; i < 8; ++i) {
                h ^= (bits >> (8 * i)) & 0xFF;
                h *= 1099511628211ull;
            }
        }
    return h;
}

const char* statusText(vis::AddStatus s) {
    switch (s) {
        case vis::AddStatus::Added: return "Wall added";
        case vis::AddStatus::RejectedNonFinite: return "That wall can't be added (invalid position)";
        case vis::AddStatus::RejectedOutside: return "That wall is outside the room";
        case vis::AddStatus::RejectedTooShort: return "That wall is too short to add";
        case vis::AddStatus::RejectedTooLarge: return "That wall can't be added (too far away)";
    }
    return "?";
}

}  // namespace

void App::init() {
    loadScene(vis::SceneKind::Demo);
    status.clear();
}

void App::sceneChanged() {
    ++epoch;
    dirty = true;
    stats[0].clear();
    stats[1].clear();
}

void App::setStatus(const std::string& msg, bool error) {
    status = msg;
    statusIsError = error;
    statusUntil = now + 4.0;
}

void App::loadScene(vis::SceneKind k) {
    kind = k;
    drawing = false;
    draggingLight = false;
    // One real rebuild: the grid resolution is set on the (boundary-only) scene first, then the walls
    // are loaded in a single setWalls call.
    vis::Scene fresh(vis::kStressWidth, vis::kStressHeight);
    if (!autoGrid) fresh.setGridResolution(gridCols, gridRows);
    fresh.setWalls(vis::makeSceneWalls(k, stressWalls, static_cast<std::uint64_t>(seed)));
    scene = std::move(fresh);
    if (k == vis::SceneKind::Demo) {
        light = vis::demoLight();
    } else {
        Vec2 preferred{scene.width() * 0.5, scene.height() * 0.5};
        if (k == vis::SceneKind::Cluster && scene.userWallCount() > 0) {
            // look at the packed patch from a distance
            Vec2 c{0, 0};
            for (std::size_t i = vis::Scene::kBoundaryWalls; i < scene.walls().size(); ++i)
                c = c + (scene.walls()[i].a + scene.walls()[i].b) * 0.5;
            c = c * (1.0 / static_cast<double>(scene.userWallCount()));
            preferred = {std::clamp(c.x - 380.0, 40.0, scene.width() - 40.0), c.y};
        }
        light = vis::chooseLight(scene, preferred);
    }
    sceneChanged();
    if (k == vis::SceneKind::Demo) setStatus("Demo room loaded", false);
    else setStatus(std::string(sceneInfo(k).name) + ": " + wallsPhrase(scene.userWallCount()), false);
}

void App::clearWalls() {
    scene.clear();
    drawing = false;
    sceneChanged();
    setStatus("All walls removed", false);
}

void App::setMode(vis::Mode m) {
    if (m == mode) return;
    mode = m;
    dirty = true;
    setStatus(mode == vis::Mode::Grid ? "Now using the grid" : "Now checking every wall", false);
}

void App::toggleMode() { setMode(mode == vis::Mode::Grid ? vis::Mode::Brute : vis::Mode::Grid); }

void App::setOverlay(Overlay o) {
    overlay = o;
    dirty = true;  // the trace is only recorded while an overlay needs it
    static const char* const names[] = {"Showing the light only", "Showing the grid", "Following one ray", "Showing every ray"};
    setStatus(names[static_cast<int>(o)], false);
}

void App::cycleOverlay() { setOverlay(static_cast<Overlay>((static_cast<int>(overlay) + 1) % 4)); }

float App::grabRadius() const {
    // Big enough to grab easily; smaller in dense scenes so it does not swallow wall drawing.
    const std::size_t n = scene.userWallCount();
    return (n <= 400 ? 22.0f : n <= 1500 ? 18.0f : 16.0f) * uiScale;
}

void App::cancelWall() {
    if (drawing) {
        drawing = false;
        setStatus("Wall cancelled", false);
    }
}

void App::setLight(Vec2 p) {
    light = {std::clamp(p.x, 0.0, scene.width()), std::clamp(p.y, 0.0, scene.height())};
    dirty = true;
}

void App::applyGridSettings() {
    if (autoGrid) scene.setGridAuto();
    else scene.setGridResolution(gridCols, gridRows);
    sceneChanged();
}

void App::layout(ImVec2 display) {
    displaySize = display;
    canvasMin = {0.0f, 0.0f};
    canvasMax = {std::max(display.x - panelWidth(), 100.0f), std::max(display.y - statusHeight(), 100.0f)};
    const float margin = 16.0f * uiScale;
    // Legend and tour card live in a strip below the world; with neither, the world is centred in the
    // whole canvas.
    strip = stripLayout();
    const float band = strip.height > 0.0f ? strip.height + 10.0f * uiScale + 4.0f * uiScale : 0.0f;
    const float cw = canvasMax.x - canvasMin.x - 2 * margin;
    const float ch = canvasMax.y - canvasMin.y - 2 * margin - band;
    view.scale = std::max(0.02f, std::min(cw / static_cast<float>(scene.width()), ch / static_cast<float>(scene.height())));
    const float worldH = static_cast<float>(scene.height()) * view.scale;
    const float centred = canvasMin.y + (canvasMax.y - canvasMin.y - worldH) * 0.5f;
    const float lowest = canvasMax.y - margin - band - worldH;
    view.origin = {canvasMin.x + (canvasMax.x - canvasMin.x - static_cast<float>(scene.width()) * view.scale) * 0.5f,
                   std::max(canvasMin.y + margin * 0.5f, std::min(centred, lowest))};
}

int App::nearestWall(ImVec2 mouse, float maxPixels, bool userOnly) const {
    int best = -1;
    float bestD = maxPixels;
    const auto& walls = scene.walls();
    for (std::size_t i = userOnly ? vis::Scene::kBoundaryWalls : 0; i < walls.size(); ++i) {
        const float d = distToSegmentPx(mouse, view.toScreen(walls[i].a), view.toScreen(walls[i].b));
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

Vec2 App::snapped(ImVec2 mouse, bool snap) const {
    const Vec2 w = view.toWorld(mouse);
    if (!snap) return w;
    Vec2 best = w;
    float bestD = 8.0f * uiScale;
    for (const vis::Segment& s : scene.walls())
        for (Vec2 p : {s.a, s.b}) {
            const ImVec2 sp = view.toScreen(p);
            const float d = std::hypot(sp.x - mouse.x, sp.y - mouse.y);
            if (d < bestD) {
                bestD = d;
                best = p;
            }
        }
    return best;
}

void App::handleInput(ImGuiIO& io) {
    const ImVec2 m = io.MousePos;
    const bool overCanvas = m.x >= canvasMin.x && m.x < canvasMax.x && m.y >= canvasMin.y && m.y < canvasMax.y;
    // A drag that started on the canvas keeps working over the panel (ImGui reports no capture for
    // it); a press that lands on the panel never starts one.
    const bool free = !io.WantCaptureMouse && overCanvas;

    const ImVec2 lp = view.toScreen(light);
    const float grab = grabRadius();
    hoverLight = free && !drawing && std::hypot(m.x - lp.x, m.y - lp.y) <= grab;
    hoverWall = -1;
    hoverBoundary = false;
    if (free && !drawing && !draggingLight) {
        // User walls first: one on or next to the world edge must stay deletable.
        const int w = nearestWall(m, 8.0f * uiScale, true);
        if (w >= 0) hoverWall = w;
        else hoverBoundary = nearestWall(m, 8.0f * uiScale, false) >= 0;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) && free) {
        if (hoverLight) {
            draggingLight = true;
            lightMoved = true;
        } else {
            drawing = true;
            drawStart = snapped(m, io.KeyShift);
            drawEnd = drawStart;
        }
    }
    if (draggingLight) {
        if (io.MouseDown[0]) setLight(view.toWorld(m));
        else draggingLight = false;
    }
    if (drawing) {
        drawEnd = snapped(m, io.KeyShift);
        if (!io.MouseDown[0]) {
            drawing = false;
            const vis::AddStatus st = scene.addWall(drawStart, drawEnd);
            if (st == vis::AddStatus::Added) {
                sceneChanged();
                setStatus("Wall added \u2014 " + wallsPhrase(scene.userWallCount()) + " in the room", false);
            } else {
                setStatus(statusText(st), true);
            }
        }
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right, false) && free && !drawing && !draggingLight) {
        if (hoverWall >= 0) {
            scene.removeWall(static_cast<std::size_t>(hoverWall));
            hoverWall = -1;
            sceneChanged();
            setStatus("Wall removed \u2014 " + wallsPhrase(scene.userWallCount()) + " in the room", false);
        } else if (hoverBoundary) {
            setStatus("The outer walls can't be removed", true);
        }
    }

    if (!io.WantCaptureKeyboard && !io.WantTextInput) {
        const auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
        if (pressed(ImGuiKey_Tab) || pressed(ImGuiKey_M)) toggleMode();
        if (pressed(ImGuiKey_R)) resetDemo();
        if (pressed(ImGuiKey_C)) clearWalls();
        if (pressed(ImGuiKey_O)) cycleOverlay();
        if (pressed(ImGuiKey_1)) loadScene(vis::SceneKind::Scatter);
        if (pressed(ImGuiKey_2)) loadScene(vis::SceneKind::Maze);
        if (pressed(ImGuiKey_3)) loadScene(vis::SceneKind::LongWalls);
        if (pressed(ImGuiKey_4)) loadScene(vis::SceneKind::Cluster);
        if (pressed(ImGuiKey_Escape)) {
            if (tourActive) tourClose();
            cancelWall();
        }
    }

    // Cursor over the canvas; over the panel or tour card ImGui's own cursor applies (main.cpp).
    if (draggingLight) cursor = Cursor::Move;
    else if (drawing) cursor = Cursor::Cross;
    else if (free && hoverLight) cursor = Cursor::Hand;
    else if (free) cursor = Cursor::Cross;
    else cursor = Cursor::Arrow;
}

// The probe ray is chosen from the trace that is about to be drawn (after updateVisibility), so a
// scene change in the same frame can never leave a stale index.
void App::updateProbe(const ImGuiIO& io) {
    probeRay = -1;
    if (overlay != Overlay::Probe || trace.rays.empty()) return;
    const Vec2 dir = view.toWorld(io.MousePos) - light;
    if (!(vis::dot(dir, dir) > 0.0)) return;
    double best = 1e9;
    for (std::size_t i = 0; i < trace.rays.size(); ++i) {
        const Vec2 d = trace.rays[i].dir;
        const double a = std::fabs(std::atan2(vis::cross(d, dir), vis::dot(d, dir)));
        if (a < best) {
            best = a;
            probeRay = static_cast<int>(i);
        }
    }
}

const vis::Trace::RayRecord* App::probe() const {
    if (overlay != Overlay::Probe || probeRay < 0 || probeRay >= static_cast<int>(trace.rays.size())) return nullptr;
    const vis::Trace::RayRecord& r = trace.rays[static_cast<std::size_t>(probeRay)];
    // Ranges must lie inside the recorded arrays and index real cells / walls.
    if (r.cellBegin > r.cellEnd || r.cellEnd > trace.cells.size() || r.wallBegin > r.wallEnd || r.wallEnd > trace.walls.size())
        return nullptr;
    return &r;
}

std::size_t App::probeWalls(const vis::Trace::RayRecord& r) const {
    return trace.allWallsTested ? scene.wallCount() : static_cast<std::size_t>(r.wallEnd - r.wallBegin);
}

void App::refreshGridInfo() {
    if (infoEpoch == epoch) return;
    infoEpoch = epoch;
    const vis::Grid& g = scene.grid();
    maxCellWalls = 0;
    if (g.cellCount() <= 4'000'000)
        for (std::size_t c = 0; c < g.cellCount(); ++c) maxCellWalls = std::max(maxCellWalls, g.cellWalls(c).size());
    if (autoGrid) {
        gridCols = g.cols();
        gridRows = g.rows();
    }
}

void App::updateVisibility() {
    refreshGridInfo();
    if (!dirty && settle <= 0) return;
    if (dirty) settle = 8;  // repeat a few frames: more real samples for the median
    else --settle;
    dirty = false;

    // Timed passes: nothing but the query itself. With Compare on, the order of the two back ends
    // alternates between queries so neither always runs on the warmer cache.
    const vis::Mode other = mode == vis::Mode::Grid ? vis::Mode::Brute : vis::Mode::Grid;
    const auto runCurrent = [&] {
        vis::computeVisibility(scene, light, mode, result);
        ModeStats& ms = stats[static_cast<int>(mode)];
        ms.total.add(result.stats.totalUs);
        ms.sort.add(result.stats.sortUs);
        ms.cast.add(result.stats.castUs);
        ms.last = result.stats;
        ms.valid = true;
    };
    const auto runOther = [&] {
        vis::computeVisibility(scene, light, other, otherResult);
        ModeStats& os = stats[static_cast<int>(other)];
        os.total.add(otherResult.stats.totalUs);
        os.sort.add(otherResult.stats.sortUs);
        os.cast.add(otherResult.stats.castUs);
        os.last = otherResult.stats;
        os.valid = true;
    };
    haveOther = false;
    if (compare) {
        compareFlip = !compareFlip;
        if (compareFlip) { runOther(); runCurrent(); }
        else { runCurrent(); runOther(); }
        haveOther = true;
        identical = result.polygon == otherResult.polygon;
    } else {
        runCurrent();
    }
    polyHash = fnv(result.polygon);
    updateMeter();

    // Untimed pass with tracing, only when an overlay reads it.
    if (overlay != Overlay::Off) vis::computeVisibility(scene, light, mode, traceResult, &trace);
    else trace.clear();
}

// Work meter and slow-scene estimate. Everything here is either arithmetic on counters of the timed
// query or an untimed pass whose timings are discarded.
void App::updateMeter() {
    // Above this many Baseline wall tests (an update of well over 100 ms) the extra untimed Grid pass is
    // not run, so "Check every wall" never gets slower for the meter's sake. A work count, not a timing,
    // so the decision is the same on every machine and every run.
    constexpr std::uint64_t kMeterSkipTests = 20'000'000;
    constexpr double kSlowMs = 30.0;          // below ~30 updates per second
    meter.valid = true;
    meter.rays = result.stats.raysCast;
    meter.walls = scene.wallCount();
    meter.baselineTests = meter.rays * static_cast<std::uint64_t>(meter.walls);
    if (mode == vis::Mode::Grid) {
        meter.gridTests = result.stats.wallTests;
        meter.gridSource = WorkMeter::GridSource::Measured;
    } else if (haveOther) {
        meter.gridTests = otherResult.stats.wallTests;
        meter.gridSource = WorkMeter::GridSource::Measured;
    } else if (meter.baselineTests <= kMeterSkipTests) {
        vis::computeVisibility(scene, light, vis::Mode::Grid, meterResult);  // never added to any statistic
        meter.gridTests = meterResult.stats.wallTests;
        meter.gridSource = WorkMeter::GridSource::ExtraPass;
    } else {
        meter.gridSource = WorkMeter::GridSource::Skipped;
    }

    if (mode == vis::Mode::Brute && result.stats.wallTests >= 100000)
        bruteNsPerTest = result.stats.castUs * 1000.0 / static_cast<double>(result.stats.wallTests);
    if (mode == vis::Mode::Brute) {
        baselineEstMs = result.stats.totalUs / 1000.0;
    } else {
        if (meter.baselineTests > 200000 && bruteNsPerTest <= 0.0) {
            // One-off calibration on a 1000-wall scene: the real cost of a wall test on this machine.
            const vis::Scene probeScene = vis::makeScene(vis::SceneKind::Scatter, 1000, 1);
            vis::VisibilityResult r;
            double best = 1e30;
            for (int i = 0; i < 3; ++i) {
                vis::computeVisibility(probeScene, {800, 500}, vis::Mode::Brute, r);
                if (r.stats.wallTests > 0) best = std::min(best, r.stats.castUs * 1000.0 / static_cast<double>(r.stats.wallTests));
            }
            bruteNsPerTest = best < 1e29 ? best : 10.0;
        }
        baselineEstMs = static_cast<double>(meter.baselineTests) * bruteNsPerTest / 1e6;
    }
    slowBaseline = baselineEstMs >= kSlowMs;
}

std::string App::slowNote() const {
    if (!slowBaseline) return {};
    std::string s = "This scene is slow with \u201CCheck every wall\u201D \u2014 about " + ui::fmtNum(baselineEstMs, 0) +
                    " ms per update.";
    if (mode == vis::Mode::Brute) s += " Switch to \u201CUse the grid\u201D to keep it smooth.";
    return s;
}

std::string App::overlayCaption() const {
    switch (overlay) {
        case Overlay::Off: return "Just the lit area.";
        case Overlay::Grid:
            if (mode == vis::Mode::Brute)
                return "\u201CCheck every wall\u201D doesn't use the grid, so no squares are tinted. The lines are the rays.";
            return "The world is split into " + std::to_string(scene.grid().cols()) + " x " + std::to_string(scene.grid().rows()) +
                   " squares. Brighter squares were crossed by more rays.";
        case Overlay::Probe: {
            const vis::Trace::RayRecord* r = probe();
            if (!r) return "Move the mouse over the room to pick a ray.";
            const std::size_t tested = probeWalls(*r);
            if (mode == vis::Mode::Brute) return "This one had to test all " + ui::fmtInt(scene.wallCount()) + " walls.";
            const std::size_t cells = r->cellEnd - r->cellBegin;
            return "Move the mouse to pick a ray. This one crossed " + ui::fmtInt(cells) + (cells == 1 ? " square" : " squares") +
                   " and had to test " + ui::fmtInt(tested) + " of " + ui::fmtInt(scene.wallCount()) + " walls.";
        }
        case Overlay::Rays:
            return ui::fmtInt(trace.rays.size()) + " rays, one aimed at each direction where a wall ends or two walls cross.";
    }
    return {};
}

void App::frame(ImGuiIO& io, double time) {
    now = time;
    frameMs += (static_cast<double>(io.DeltaTime) * 1000.0 - frameMs) * 0.05;
    layout(io.DisplaySize);
    handleInput(io);
    updateVisibility();
    updateProbe(io);
}

std::string App::field(const std::string& name) const {
    const auto num = [](double v) {
        char b[64];
        std::snprintf(b, sizeof b, "%.6g", v);
        return std::string(b);
    };
    const vis::VisibilityStats& st = result.stats;
    if (name == "mode") return mode == vis::Mode::Grid ? "grid" : "brute";
    if (name == "walls") return std::to_string(scene.userWallCount());
    if (name == "walls_total") return std::to_string(scene.wallCount());
    if (name == "k") return std::to_string(scene.intersections().size());
    if (name == "T") return std::to_string(scene.targets().size());
    if (name == "rays") return std::to_string(st.raysCast);
    if (name == "wall_tests") return std::to_string(st.wallTests);
    if (name == "cells") return std::to_string(st.cellsVisited);
    if (name == "vertices") return std::to_string(result.polygon.size());
    if (name == "identical") return !compare || !haveOther ? "off" : (identical ? "yes" : "NO");
    if (name == "light_x") return num(light.x);
    if (name == "light_y") return num(light.y);
    if (name == "poly_hash") return std::to_string(polyHash);
    if (name == "status") return status;
    if (name == "scene") return vis::sceneKindName(kind);
    if (name == "overlay") return overlayName(overlay);
    if (name == "grid_cols") return std::to_string(scene.grid().cols());
    if (name == "grid_rows") return std::to_string(scene.grid().rows());
    if (name == "grid_regs") return std::to_string(scene.grid().registrations());
    if (name == "compare") return compare ? "on" : "off";
    if (name == "seed") return std::to_string(seed);
    if (name == "mouse_x") return num(ImGui::GetIO().MousePos.x);
    if (name == "mouse_y") return num(ImGui::GetIO().MousePos.y);
    if (name == "scale") return num(view.scale);
    if (name == "origin_x") return num(view.origin.x);
    if (name == "origin_y") return num(view.origin.y);
    if (name == "drawing") return drawing ? "yes" : "no";
    if (name == "median_us_brute") return num(stats[0].total.median());
    if (name == "median_us_grid") return num(stats[1].total.median());
    if (name == "tests_brute") return std::to_string(stats[0].last.wallTests);
    if (name == "tests_grid") return std::to_string(stats[1].last.wallTests);
    if (name == "query_us") return num(st.totalUs);
    if (name == "query_median_us") return num(stats[static_cast<int>(mode)].total.median());
    if (name == "rebuild_grid_us") return num(scene.lastRebuild().gridBuildUs);
    if (name == "rebuild_isect_us") return num(scene.lastRebuild().intersectUs);
    if (name == "detail") return detail == Detail::Simple ? "simple" : "details";
    if (name == "tour") return tourActive ? "on" : "off";
    if (name == "tour_step") return std::to_string(tourStep + 1);
    if (name == "rays_x_walls") return std::to_string(st.raysCast * static_cast<std::uint64_t>(scene.wallCount()));
    if (name == "meter_rays") return std::to_string(meter.rays);
    if (name == "meter_walls") return std::to_string(meter.walls);
    if (name == "meter_baseline") return std::to_string(meter.baselineTests);
    if (name == "meter_grid") return meter.gridKnown() ? std::to_string(meter.gridTests) : "unknown";
    if (name == "meter_source")
        return meter.gridSource == WorkMeter::GridSource::Measured ? "measured"
               : meter.gridSource == WorkMeter::GridSource::ExtraPass ? "extra_pass" : "skipped";
    if (name == "meter_verdict") return meterVerdict();
    if (name == "slow_note") return slowBaseline ? "yes" : "no";
    if (name == "slow_ms") return num(baselineEstMs);
    if (name == "slow_text") return slowNote();
    if (name == "panel_slack") return num(panelSlack);
    if (name == "panel_scroll") return num(panelScrollMax);
    if (name == "panel_overflow") return panelOverflow ? "yes" : "no";
    if (name == "light_hint") return lightMoved ? "no" : "yes";
    if (name == "caption") return overlayCaption();
    if (name == "cursor") return cursor == Cursor::Hand ? "hand" : cursor == Cursor::Move ? "move" : cursor == Cursor::Cross ? "cross" : "arrow";
    if (name == "probe_cells" || name == "probe_walls" || name == "probe_tminus" || name == "probe_tplus") {
        const vis::Trace::RayRecord* r = probe();
        if (!r) return "none";
        if (name == "probe_cells") return std::to_string(r->cellEnd - r->cellBegin);
        if (name == "probe_walls") return std::to_string(probeWalls(*r));
        return num(name == "probe_tminus" ? r->tMinus : r->tPlus);
    }
    return "?";
}

std::string App::logLine() const {
    char b[512];
    std::snprintf(b, sizeof b,
                  "mode=%s walls=%zu light=(%.3f,%.3f) k=%zu T=%zu rays=%llu wall_tests=%llu vertices=%zu identical=%s",
                  field("mode").c_str(), scene.userWallCount(), light.x, light.y, scene.intersections().size(),
                  scene.targets().size(), static_cast<unsigned long long>(result.stats.raysCast),
                  static_cast<unsigned long long>(result.stats.wallTests), result.polygon.size(),
                  field("identical").c_str());
    return b;
}
