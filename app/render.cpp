// Canvas drawing. Everything about the algorithm that is drawn here (cells walked, walls tested, hit
// points, rays) is read from App::trace, the Grid or the Scene: nothing is recomputed or invented.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <cstdio>
#include <vector>

#include <imgui_internal.h>  // ImDrawListSharedData: the white-pixel UV for the vertex-coloured mesh

#include "app.h"
#include "legend.h"
#include "widgets.h"

using vis::Vec2;

namespace {

constexpr ImU32 kFloor = IM_COL32(21, 24, 33, 255);
constexpr ImU32 kWall = IM_COL32(226, 231, 244, 255);
constexpr ImU32 kWallMuted = IM_COL32(120, 128, 150, 150);
constexpr ImU32 kTested = IM_COL32(255, 168, 64, 255);
constexpr ImU32 kAccent = IM_COL32(94, 196, 255, 255);
constexpr ImU32 kDelete = IM_COL32(255, 96, 96, 255);

ImU32 withAlpha(ImU32 c, int a) { return (c & 0x00FFFFFFu) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT); }

ImVec2 operator+(ImVec2 a, ImVec2 b) { return {a.x + b.x, a.y + b.y}; }
ImVec2 operator-(ImVec2 a, ImVec2 b) { return {a.x - b.x, a.y - b.y}; }
ImVec2 operator*(ImVec2 a, float s) { return {a.x * s, a.y * s}; }

// Radial gradient as a ring mesh with per-vertex colours; clipped by the caller's clip rect.
void drawLightGradient(ImDrawList* dl, ImVec2 c, float rMax) {
    constexpr int kRings = 28;
    constexpr int kSegs = 96;
    const int vtxCount = 1 + kRings * kSegs;
    const int idxCount = kSegs * 3 + (kRings - 1) * kSegs * 6;
    dl->PrimReserve(idxCount, vtxCount);
    const ImVec2 uv = dl->_Data->TexUvWhitePixel;
    const auto colourAt = [](float t) {  // t = distance / rMax in [0,1]
        const float fall = std::pow(1.0f - t, 2.4f);
        const float a = 0.10f + 0.62f * fall;
        // warm near the light, cooling slightly with distance
        const float r = 1.0f, g = 0.80f + 0.06f * t, b = 0.52f + 0.30f * t;
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a * (t >= 1.0f ? 0.0f : 1.0f)));
    };
    dl->PrimWriteVtx(c, uv, colourAt(0.0f));
    for (int i = 1; i <= kRings; ++i) {
        const float t = static_cast<float>(i) / kRings;
        // ring radii bunch up near the light where the colour changes fastest
        const float r = rMax * t * t * 0.0f + rMax * (t * t * 0.55f + t * 0.45f);
        const ImU32 col = colourAt(t * t * 0.55f + t * 0.45f);
        for (int s = 0; s < kSegs; ++s) {
            const float a = 6.28318530718f * static_cast<float>(s) / kSegs;
            dl->PrimWriteVtx({c.x + std::cos(a) * r, c.y + std::sin(a) * r}, uv, col);
        }
    }
    const auto vi = [&](int ring, int s) { return static_cast<ImDrawIdx>(1 + (ring - 1) * kSegs + (s % kSegs)); };
    const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx - vtxCount);
    for (int s = 0; s < kSegs; ++s) {
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(base + vi(1, s));
        dl->PrimWriteIdx(base + vi(1, s + 1));
    }
    for (int ring = 1; ring < kRings; ++ring)
        for (int s = 0; s < kSegs; ++s) {
            dl->PrimWriteIdx(base + vi(ring, s));
            dl->PrimWriteIdx(base + vi(ring + 1, s));
            dl->PrimWriteIdx(base + vi(ring + 1, s + 1));
            dl->PrimWriteIdx(base + vi(ring, s));
            dl->PrimWriteIdx(base + vi(ring + 1, s + 1));
            dl->PrimWriteIdx(base + vi(ring, s + 1));
        }
}

void drawShadows(ImDrawList* dl, const App& app, ImVec2 lp, float reach) {
    const auto& poly = app.result.polygon;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const ImVec2 a = app.view.toScreen(poly[i]);
        const ImVec2 b = app.view.toScreen(poly[(i + 1) % n]);
        const ImVec2 ca = a - lp, cb = b - lp;
        const float la = std::hypot(ca.x, ca.y), lb = std::hypot(cb.x, cb.y);
        if (la < 1e-3f || lb < 1e-3f) continue;
        const float cr = ca.x * cb.y - ca.y * cb.x;
        if (std::fabs(cr) <= 1e-5f * la * lb) continue;  // radial edge: zero-area shadow
        const ImVec2 pts[4] = {a, b, lp + cb * ((lb + reach) / lb), lp + ca * ((la + reach) / la)};
        dl->AddConvexPolyFilled(pts, 4, kFloor);
    }
}

void cellRect(const App& app, int cx, int cy, ImVec2& lo, ImVec2& hi) {
    const vis::Grid& g = app.scene.grid();
    lo = app.view.toScreen({g.xEdge(cx), g.yEdge(cy)});
    hi = app.view.toScreen({g.xEdge(cx + 1), g.yEdge(cy + 1)});
}

ImU32 heat(float t) {  // 0 -> transparent blue, 1 -> hot orange
    t = std::clamp(t, 0.0f, 1.0f);
    const float r = 0.25f + 0.75f * t, g = 0.35f + 0.35f * (1.0f - std::fabs(t - 0.5f) * 2.0f), b = 0.9f - 0.8f * t;
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.12f + 0.42f * t));
}

void drawGridLines(ImDrawList* dl, const App& app, int alpha) {
    const vis::Grid& g = app.scene.grid();
    const ImVec2 lo = app.view.toScreen({0, 0});
    const ImVec2 hi = app.view.toScreen({g.width(), g.height()});
    if (g.cellWidth() * app.view.scale < 4.0f || g.cellHeight() * app.view.scale < 4.0f) return;  // would be a smear
    const ImU32 col = IM_COL32(150, 170, 210, alpha);
    for (int i = 0; i <= g.cols(); ++i) {
        const float x = app.view.toScreen({g.xEdge(i), 0}).x;
        dl->AddLine({x, lo.y}, {x, hi.y}, col, 1.0f);
    }
    for (int j = 0; j <= g.rows(); ++j) {
        const float y = app.view.toScreen({0, g.yEdge(j)}).y;
        dl->AddLine({lo.x, y}, {hi.x, y}, col, 1.0f);
    }
}

void drawRays(ImDrawList* dl, const App& app, ImVec2 lp, int alpha) {
    const ImU32 col = IM_COL32(120, 210, 255, alpha);
    for (const vis::Trace::RayRecord& r : app.trace.rays) {
        const bool a = std::isfinite(r.tMinus), b = std::isfinite(r.tPlus);
        if (a) dl->AddLine(lp, app.view.toScreen(app.light + r.dir * r.tMinus), col, 1.0f);
        if (b && r.tPlus != r.tMinus) dl->AddLine(lp, app.view.toScreen(app.light + r.dir * r.tPlus), col, 1.0f);
    }
}


// "Drag me" pill beside the light, on whichever side (above, right, left, below) has the fewest walls
// under it and stays inside the world; a small pointer touches the handle.
bool segmentHitsRect(ImVec2 a, ImVec2 b, ImVec2 lo, ImVec2 hi) {
    float t0 = 0.0f, t1 = 1.0f;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {a.x - lo.x, hi.x - a.x, a.y - lo.y, hi.y - a.y};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0.0f) {
            if (q[i] < 0.0f) return false;
        } else {
            const float t = q[i] / p[i];
            if (p[i] < 0.0f) t0 = std::max(t0, t);
            else t1 = std::min(t1, t);
            if (t0 > t1) return false;
        }
    }
    return true;
}

void drawDragHint(ImDrawList* dl, const App& app, ImVec2 lp, float grab) {
    const float ui = app.uiScale;
    const char* tip = "Drag me";
    const float px = 15.0f * ui;
    const ImVec2 ts = app.uiFont->CalcTextSizeA(px, FLT_MAX, 0.0f, tip);
    const ImVec2 size{ts.x + 16.0f * ui, ts.y + 8.0f * ui};
    const float off = grab + 12.0f * ui;
    const ImVec2 wmin = app.view.toScreen({0, 0}), wmax = app.view.toScreen({app.scene.width(), app.scene.height()});
    struct Cand { ImVec2 lo; int dir; };  // dir: 0 above, 1 right, 2 left, 3 below
    const Cand cands[4] = {{{lp.x - size.x * 0.5f, lp.y - off - size.y}, 0},
                           {{lp.x + off, lp.y - size.y * 0.5f}, 1},
                           {{lp.x - off - size.x, lp.y - size.y * 0.5f}, 2},
                           {{lp.x - size.x * 0.5f, lp.y + off}, 3}};
    int best = 0;
    long long bestScore = 1LL << 40;
    for (int c = 0; c < 4; ++c) {
        const ImVec2 lo = cands[c].lo, hi{lo.x + size.x, lo.y + size.y};
        long long score = 0;
        if (lo.x < wmin.x || lo.y < wmin.y || hi.x > wmax.x || hi.y > wmax.y) score += 100000;
        const ImVec2 pad{4.0f * ui, 4.0f * ui};
        for (const vis::Segment& w : app.scene.walls())
            if (segmentHitsRect(app.view.toScreen(w.a), app.view.toScreen(w.b), lo - pad, hi + pad)) ++score;
        if (score < bestScore) {
            bestScore = score;
            best = c;
        }
    }
    const Cand& c = cands[best];
    const ImVec2 lo = c.lo, hi{lo.x + size.x, lo.y + size.y};
    const ImU32 fill = IM_COL32(255, 196, 90, 240);
    ImVec2 tri[3];
    const float t = 6.0f * ui;
    switch (c.dir) {
        case 0: tri[0] = {lp.x - t, hi.y - 1}; tri[1] = {lp.x + t, hi.y - 1}; tri[2] = {lp.x, hi.y + t}; break;
        case 1: tri[0] = {lo.x + 1, lp.y - t}; tri[1] = {lo.x + 1, lp.y + t}; tri[2] = {lo.x - t, lp.y}; break;
        case 2: tri[0] = {hi.x - 1, lp.y - t}; tri[1] = {hi.x - 1, lp.y + t}; tri[2] = {hi.x + t, lp.y}; break;
        default: tri[0] = {lp.x - t, lo.y + 1}; tri[1] = {lp.x + t, lo.y + 1}; tri[2] = {lp.x, lo.y - t}; break;
    }
    dl->AddRectFilled(lo, hi, fill, 6.0f * ui);
    dl->AddTriangleFilled(tri[0], tri[1], tri[2], fill);
    dl->AddText(app.uiFont, px, {lo.x + 8.0f * ui, lo.y + 4.0f * ui}, IM_COL32(30, 20, 5, 255), tip);
}

}  // namespace

ImU32 heatColour(float t) { return heat(t); }

void drawCanvas(ImDrawList* dl, const App& app) {
    const vis::Scene& scene = app.scene;
    const float ui = app.uiScale;
    const ImVec2 wmin = app.view.toScreen({0, 0});
    const ImVec2 wmax = app.view.toScreen({scene.width(), scene.height()});
    const ImVec2 lp = app.view.toScreen(app.light);
    const ImDrawListFlags savedFlags = dl->Flags;

    dl->PushClipRect(app.canvasMin, app.canvasMax, true);

    // ---- floor, light, shadows (clipped to the world) ----
    dl->PushClipRect(wmin, wmax, true);
    dl->AddRectFilled(wmin, wmax, kFloor);
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;  // shared edges: feathering would show as seams
    {
        float rMax = 0;
        for (Vec2 c : {Vec2{0, 0}, Vec2{scene.width(), 0}, Vec2{0, scene.height()}, Vec2{scene.width(), scene.height()}}) {
            const ImVec2 p = app.view.toScreen(c);
            rMax = std::max(rMax, std::hypot(p.x - lp.x, p.y - lp.y));
        }
        // Purely cosmetic falloff: it only makes the lit area readable, it is not a physical model.
        drawLightGradient(dl, lp, std::max(rMax, 10.0f) * 0.8f);
        drawShadows(dl, app, lp, std::hypot(wmax.x - wmin.x, wmax.y - wmin.y) * 2.0f + 100.0f);
    }
    dl->Flags = savedFlags;
    dl->PopClipRect();

    const bool haveTrace = app.overlay != Overlay::Off && !app.trace.rays.empty();
    const vis::Grid& grid = scene.grid();

    // ---- overlays that sit under the walls ----
    if (haveTrace && app.overlay == Overlay::Grid && app.mode == vis::Mode::Grid) {
        std::uint32_t maxVisits = 1;
        for (std::uint32_t v : app.trace.cellVisits) maxVisits = std::max(maxVisits, v);
        if (grid.cellCount() <= 200000) {
            for (int cy = 0; cy < grid.rows(); ++cy)
                for (int cx = 0; cx < grid.cols(); ++cx) {
                    ImVec2 lo, hi;
                    cellRect(app, cx, cy, lo, hi);
                    const std::size_t c = grid.cellIndex(cx, cy);
                    const std::uint32_t v = c < app.trace.cellVisits.size() ? app.trace.cellVisits[c] : 0;
                    if (v > 0) dl->AddRectFilled(lo, hi, heat(static_cast<float>(v) / static_cast<float>(maxVisits)));
                    if (!grid.cellWalls(c).empty() && hi.x - lo.x > 8.0f)
                        dl->AddRectFilled({lo.x + 2, lo.y + 2}, {lo.x + 2 + 3 * ui, lo.y + 2 + 3 * ui}, IM_COL32(190, 200, 225, 170));
                }
        }
        drawGridLines(dl, app, 70);
    }
    if (haveTrace && app.overlay == Overlay::Probe && app.mode == vis::Mode::Grid) {
        drawGridLines(dl, app, 40);
        if (const vis::Trace::RayRecord* pr = app.probe()) {
            const vis::Trace::RayRecord& r = *pr;
            int order = 0;
            for (std::uint32_t k = r.cellBegin; k < r.cellEnd; ++k, ++order) {
                const std::uint32_t c = app.trace.cells[k];
                if (c >= grid.cellCount()) continue;
                ImVec2 lo, hi;
                cellRect(app, static_cast<int>(c % static_cast<std::uint32_t>(grid.cols())),
                         static_cast<int>(c / static_cast<std::uint32_t>(grid.cols())), lo, hi);
                dl->AddRectFilled(lo, hi, IM_COL32(94, 196, 255, 46));
                dl->AddRect(lo, hi, IM_COL32(94, 196, 255, 150));
                if (hi.x - lo.x >= 22.0f * ui && hi.y - lo.y >= 22.0f * ui) {
                    char b[16];
                    std::snprintf(b, sizeof b, "%d", order + 1);
                    dl->AddText({lo.x + 3, lo.y + 2}, IM_COL32(200, 235, 255, 230), b);
                }
            }
        }
    }

    // ---- walls ----
    const auto& walls = scene.walls();
    std::vector<char> tested;
    const vis::Trace::RayRecord* probeRec = haveTrace && app.overlay == Overlay::Probe ? app.probe() : nullptr;
    const bool probing = probeRec != nullptr;
    if (probing) {
        // Baseline records no per-ray wall lists: every wall is tested by every ray.
        tested.assign(walls.size(), app.trace.allWallsTested ? 1 : 0);
        for (std::uint32_t k = probeRec->wallBegin; k < probeRec->wallEnd; ++k)
            if (app.trace.walls[k] < tested.size()) tested[app.trace.walls[k]] = 1;
    }
    dl->AddRect(wmin, wmax, IM_COL32(128, 138, 164, 255), 0.0f, 0, 2.0f * ui);
    // Thickness follows the world->screen scale (1-2 px) and is thinner still in dense scenes.
    float wallPx = std::clamp(3.0f * app.view.scale, 1.0f, 2.0f);
    if (scene.userWallCount() > 400) wallPx = std::max(1.0f, wallPx * 0.75f);
    const float wallW = wallPx * ui;
    // Dense scenes: lower the opacity too (about 250/n above 400 walls, floor 0.12), so overlapping
    // walls stay distinguishable and the lit region and light handle remain visible underneath/on top.
    const float density = static_cast<float>(scene.userWallCount());
    float wallAlpha = density > 400.0f ? std::clamp(250.0f / density, 0.12f, 1.0f) : 1.0f;
    // Long walls cover the room with ink whatever their number (1,000 Long walls = 45x the length of
    // 1,000 Scatter walls), so the opacity also follows the total drawn length.
    double inkLength = 0.0;
    for (std::size_t i = vis::Scene::kBoundaryWalls; i < walls.size(); ++i)
        inkLength += std::hypot(walls[i].b.x - walls[i].a.x, walls[i].b.y - walls[i].a.y);
    if (inkLength > 60000.0) wallAlpha = std::min(wallAlpha, std::clamp(static_cast<float>(60000.0 / inkLength), 0.05f, 1.0f));
    const ImU32 wallColour = withAlpha(kWall, static_cast<int>(255.0f * wallAlpha));
    for (std::size_t i = vis::Scene::kBoundaryWalls; i < walls.size(); ++i) {
        ImU32 col = wallColour;
        float w = wallW;
        if (probing) {
            if (tested[i]) { col = kTested; w = wallW * 1.3f; }
            else col = withAlpha(kWallMuted, static_cast<int>(150.0f * std::max(wallAlpha, 0.2f)));  // skipped walls fade in dense scenes
        }
        if (static_cast<int>(i) == app.hoverWall) { col = kDelete; w = wallW * 1.8f; }
        dl->AddLine(app.view.toScreen(walls[i].a), app.view.toScreen(walls[i].b), col, w);
    }

    // ---- overlays on top of the walls ----
    if (haveTrace && (app.overlay == Overlay::Rays || (app.overlay == Overlay::Grid && app.mode == vis::Mode::Brute)))
        drawRays(dl, app, lp, app.overlay == Overlay::Rays ? 90 : 70);
    if (probing) {
        const vis::Trace::RayRecord& r = *probeRec;
        const double tFar = std::max(std::isfinite(r.tMinus) ? r.tMinus : 0.0, std::isfinite(r.tPlus) ? r.tPlus : 0.0);
        dl->AddLine(lp, app.view.toScreen(app.light + r.dir * (tFar > 0 ? tFar : 1.0)), IM_COL32(94, 196, 255, 230), 1.6f * ui);
        // Labels sit off to the side of the ray (perpendicular), on a dark backing so they stay
        // readable over walls and shadows.
        ImVec2 dirPx = app.view.toScreen(app.light + r.dir) - lp;
        const float dl2 = std::hypot(dirPx.x, dirPx.y);
        const ImVec2 perp = dl2 > 0 ? ImVec2{-dirPx.y / dl2, dirPx.x / dl2} : ImVec2{0, -1};
        const auto marker = [&](double t, ImU32 col, const char* name, float side) {
            if (!std::isfinite(t)) return;
            const ImVec2 p = app.view.toScreen(app.light + r.dir * t);
            dl->AddCircleFilled(p, 4.5f * ui, col, 14);
            dl->AddCircle(p, 6.5f * ui, IM_COL32(255, 255, 255, 200), 14, 1.2f);
            const ImVec2 sz = ImGui::CalcTextSize(name);
            ImVec2 at = p + perp * (side * (18.0f * ui));
            if (side < 0) at.x -= sz.x * (perp.x > 0 ? 1.0f : 0.0f);
            else at.x -= sz.x * (perp.x < 0 ? 1.0f : 0.0f);
            at.y -= sz.y * 0.5f;
            dl->AddRectFilled({at.x - 4, at.y - 2}, {at.x + sz.x + 4, at.y + sz.y + 2}, IM_COL32(10, 12, 18, 225), 3.0f);
            dl->AddText(at, col, name);
        };
        if (r.tMinus == r.tPlus) {
            marker(r.tMinus, ui::kGold, "hit", 1.0f);
        } else {
            // The two sides of the ray end at different walls. The nearer end is where one side stops;
            // the farther end is where the other side's view continues to. Each label sits on its own side.
            const bool minusNear = r.tMinus < r.tPlus;
            marker(r.tMinus, ui::kGreen, minusNear ? "stops here" : "other side reaches here", -1.0f);
            marker(r.tPlus, ui::kPink, minusNear ? "other side reaches here" : "stops here", 1.0f);
        }
    }

    // ---- wall being drawn ----
    if (app.drawing) {
        const ImVec2 a = app.view.toScreen(app.drawStart), b = app.view.toScreen(app.drawEnd);
        dl->AddLine(a, b, withAlpha(kAccent, 230), 2.6f * ui);
        dl->AddCircleFilled(a, 4.0f * ui, kAccent, 12);
        dl->AddCircleFilled(b, 4.0f * ui, kAccent, 12);
    }

    // ---- light handle: pulses and says "Drag me" until it has been dragged once ----
    {
        const float grab = app.grabRadius();
        const bool active = app.hoverLight || app.draggingLight;
        if (!app.lightMoved && !app.draggingLight) {
            const float phase = static_cast<float>(std::fmod(app.now, 1.8) / 1.8);
            dl->AddCircle(lp, grab * (1.0f + 0.7f * phase), IM_COL32(255, 214, 140, static_cast<int>(190.0f * (1.0f - phase))), 40, 2.2f * ui);
        }
        dl->AddCircleFilled(lp, grab, IM_COL32(255, 196, 90, active ? 70 : 34), 36);
        dl->AddCircle(lp, grab, IM_COL32(255, 214, 140, active ? 255 : 190), 36, active ? 2.4f * ui : 1.6f * ui);
        dl->AddCircleFilled(lp, 8.5f * ui, IM_COL32(40, 28, 10, 200), 22);
        dl->AddCircleFilled(lp, 6.5f * ui, IM_COL32(255, 248, 224, 255), 20);
        if (!app.lightMoved && !app.draggingLight) drawDragHint(dl, app, lp, grab);
    }

    if (app.outline && app.result.polygon.size() >= 3) {
        std::vector<ImVec2> pts;
        pts.reserve(app.result.polygon.size());
        for (Vec2 v : app.result.polygon) pts.push_back(app.view.toScreen(v));
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), IM_COL32(94, 196, 255, 190), ImDrawFlags_Closed, 1.2f);
    }

    // ---- legend: in the strip below the world, never over it ----
    if (app.strip.legend) drawLegend(dl, app, buildLegend(app, app.strip.condensed), app.strip.legendPos, app.strip.legendSize.x);

    dl->PopClipRect();
}
