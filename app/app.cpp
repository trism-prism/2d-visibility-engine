#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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
        case vis::AddStatus::Added: return "added";
        case vis::AddStatus::RejectedNonFinite: return "non-finite coordinates";
        case vis::AddStatus::RejectedOutside: return "entirely outside the world";
        case vis::AddStatus::RejectedTooShort: return "too short (under 1e-6 after clipping)";
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
    if (k == vis::SceneKind::Demo) {
        scene = vis::makeDemoScene();
        light = vis::demoLight();
    } else {
        scene = vis::makeScene(k, stressWalls, static_cast<std::uint64_t>(seed));
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
    if (!autoGrid) scene.setGridResolution(gridCols, gridRows);
    sceneChanged();
    if (k == vis::SceneKind::Demo) setStatus("Demo scene loaded", false);
    else setStatus(std::string(vis::sceneKindName(k)) + " scene: " + std::to_string(scene.userWallCount()) +
                       " walls, seed " + std::to_string(seed),
                   false);
}

void App::clearWalls() {
    scene.clear();
    drawing = false;
    sceneChanged();
    setStatus("All user walls removed", false);
}

void App::toggleMode() {
    mode = mode == vis::Mode::Grid ? vis::Mode::Brute : vis::Mode::Grid;
    dirty = true;
    setStatus(mode == vis::Mode::Grid ? "Mode: Grid" : "Mode: Baseline (brute force)", false);
}

void App::cycleOverlay() {
    overlay = static_cast<Overlay>((static_cast<int>(overlay) + 1) % 4);
    dirty = true;  // the trace is only recorded while an overlay needs it
    setStatus(std::string("Overlay: ") + overlayName(overlay), false);
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
    const float margin = 24.0f * uiScale;
    const float cw = canvasMax.x - canvasMin.x - 2 * margin;
    const float ch = canvasMax.y - canvasMin.y - 2 * margin;
    view.scale = std::max(0.02f, std::min(cw / static_cast<float>(scene.width()), ch / static_cast<float>(scene.height())));
    view.origin = {canvasMin.x + (canvasMax.x - canvasMin.x - static_cast<float>(scene.width()) * view.scale) * 0.5f,
                   canvasMin.y + (canvasMax.y - canvasMin.y - static_cast<float>(scene.height()) * view.scale) * 0.5f};
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
    const float grab = 16.0f * uiScale;
    hoverLight = free && !drawing && std::hypot(m.x - lp.x, m.y - lp.y) <= grab;
    hoverWall = -1;
    hoverBoundary = false;
    if (free && !drawing && !draggingLight) {
        const int w = nearestWall(m, 8.0f * uiScale, false);
        if (w >= static_cast<int>(vis::Scene::kBoundaryWalls)) hoverWall = w;
        else if (w >= 0) hoverBoundary = true;
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) && free) {
        if (hoverLight) {
            draggingLight = true;
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
                setStatus("Wall added (" + std::to_string(scene.userWallCount()) + " walls)", false);
            } else {
                setStatus(std::string("Wall rejected: ") + statusText(st), true);
            }
        }
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right, false) && free && !drawing && !draggingLight) {
        if (hoverWall >= 0) {
            scene.removeWall(static_cast<std::size_t>(hoverWall));
            hoverWall = -1;
            sceneChanged();
            setStatus("Wall deleted (" + std::to_string(scene.userWallCount()) + " walls)", false);
        } else if (hoverBoundary) {
            setStatus("Boundary walls cannot be deleted", true);
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
        if (pressed(ImGuiKey_Escape)) cancelWall();
    }

    // Probe: the traced ray whose direction is nearest the cursor direction.
    probeRay = -1;
    if (overlay == Overlay::Probe && !trace.rays.empty()) {
        const Vec2 dir = view.toWorld(m) - light;
        if (vis::dot(dir, dir) > 0.0) {
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
    }
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

    // Timed passes: nothing but the query itself.
    vis::computeVisibility(scene, light, mode, result);
    ModeStats& ms = stats[static_cast<int>(mode)];
    ms.total.add(result.stats.totalUs);
    ms.sort.add(result.stats.sortUs);
    ms.cast.add(result.stats.castUs);
    ms.last = result.stats;
    ms.valid = true;
    haveOther = false;
    if (compare) {
        const vis::Mode other = mode == vis::Mode::Grid ? vis::Mode::Brute : vis::Mode::Grid;
        vis::computeVisibility(scene, light, other, otherResult);
        ModeStats& os = stats[static_cast<int>(other)];
        os.total.add(otherResult.stats.totalUs);
        os.sort.add(otherResult.stats.sortUs);
        os.cast.add(otherResult.stats.castUs);
        os.last = otherResult.stats;
        os.valid = true;
        haveOther = true;
        identical = result.polygon == otherResult.polygon;
    }
    polyHash = fnv(result.polygon);

    // Untimed pass with tracing, only when an overlay reads it.
    if (overlay != Overlay::Off) vis::computeVisibility(scene, light, mode, traceResult, &trace);
    else trace.clear();
}

void App::frame(ImGuiIO& io, double time) {
    now = time;
    frameMs += (static_cast<double>(io.DeltaTime) * 1000.0 - frameMs) * 0.05;
    layout(io.DisplaySize);
    handleInput(io);
    updateVisibility();
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
    if (name == "probe_cells" || name == "probe_walls" || name == "probe_tminus" || name == "probe_tplus") {
        if (probeRay < 0 || probeRay >= static_cast<int>(trace.rays.size())) return "none";
        const vis::Trace::RayRecord& r = trace.rays[static_cast<std::size_t>(probeRay)];
        if (name == "probe_cells") return std::to_string(r.cellEnd - r.cellBegin);
        if (name == "probe_walls") return std::to_string(r.wallEnd - r.wallBegin);
        return num(name == "probe_tminus" ? r.tMinus : r.tPlus);
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
