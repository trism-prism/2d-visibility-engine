// Engine details: every number the dashboard showed before the refresh, with the same meaning.
#include <algorithm>
#include <cstdio>

#include "app.h"
#include "panel.h"
#include "widgets.h"

using namespace ui;

namespace {

void tableColumns() {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 1.15f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.0f);
}

void monoRight(const App& app, const std::string& s, ImU32 col = kText) {
    MonoScope m(app);
    const float tw = ImGui::CalcTextSize(s.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - tw));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col));
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
}

void compareTable(App& app) {
    const ModeStats& base = app.stats[0];
    const ModeStats& grid = app.stats[1];
    if (ui::beginTable("cmp", 3)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("All walls", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Grid", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("time");
        ImGui::TableSetColumnIndex(1);
        monoRight(app, fmtDuration(base.total.median()));
        ImGui::TableSetColumnIndex(2);
        monoRight(app, fmtDuration(grid.total.median()));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("wall tests");
        tooltip(gloss::wallTest);
        ImGui::TableSetColumnIndex(1);
        monoRight(app, fmtInt(base.last.wallTests));
        ImGui::TableSetColumnIndex(2);
        monoRight(app, fmtInt(grid.last.wallTests));
        ImGui::EndTable();
    }
    const double bm = base.total.median(), gm = grid.total.median();
    ImGui::TextUnformatted("Time ratio: ");
    ImGui::SameLine();
    {
        MonoScope m(app);
        ImGui::Text("%sx", fmtNum(gm > 0 ? bm / gm : 0.0, 1).c_str());
    }
    ImGui::TextUnformatted("Identical: ");
    ImGui::SameLine();
    ImGui::TextColored(app.identical ? ImVec4(0.45f, 0.9f, 0.55f, 1) : ImVec4(1, 0.4f, 0.4f, 1), app.identical ? "yes" : "NO");
    cardTextSmall(app, kTextDim,
                  "Both run every update. Timed separately; overlay and meter passes excluded.");
}

}  // namespace

void drawProbeNumbers(App& app) {
    if (app.overlay != Overlay::Probe) return;
    const vis::Trace::RayRecord* pr = app.probe();
    if (!pr) return;
    const vis::Trace::RayRecord& r = *pr;
    ImGui::Dummy({0, 2.0f * app.uiScale});
    if (ui::beginTable("probe", 2)) {
        tableColumns();
        kvRow(app, "Squares crossed", fmtInt(r.cellEnd - r.cellBegin).c_str(), gloss::cellsWalked);
        kvRow(app, "Walls tested", (fmtInt(app.probeWalls(r)) + " of " + fmtInt(app.scene.wallCount())).c_str(), gloss::wallTest);
        char b[48];
        std::snprintf(b, sizeof b, "%.4f", r.tMinus);
        kvRow(app, "tMinus", b, gloss::tMinus, kGreen);
        std::snprintf(b, sizeof b, "%.4f", r.tPlus);
        kvRow(app, "tPlus", b, gloss::tPlus, kPink);
        ImGui::EndTable();
    }
}

void drawEngineDetails(App& app) {
    const ModeStats& cur = app.stats[static_cast<int>(app.mode)];
    const vis::VisibilityStats& st = app.result.stats;
    const vis::RebuildStats& rb = app.scene.lastRebuild();

    cardBegin(app, "Numbers");
    if (ui::beginTable("dash", 2)) {
        tableColumns();
        kvRow(app, "Frame", (fmtNum(app.frameMs, 1) + " ms  " + fmtNum(1000.0 / std::max(app.frameMs, 0.01), 0) + " fps").c_str(),
              gloss::frame);
        kvRow(app, "Query", fmtDuration(cur.total.median()).c_str(), gloss::query);
        kvRow(app, "  sort / cast", (fmtNum(cur.sort.median(), 1) + " / " + fmtNum(cur.cast.median(), 1) + " µs").c_str(),
              gloss::sortCast);
        kvRow(app, "Walls", (fmtInt(app.scene.userWallCount()) + " + 4").c_str(), gloss::walls);
        kvRow(app, "Crossings k", fmtInt(app.scene.intersections().size()).c_str(), gloss::crossing);
        kvRow(app, "Targets T", fmtInt(app.scene.targets().size()).c_str(), gloss::target);
        kvRow(app, "Rays cast", fmtInt(st.raysCast).c_str(), gloss::ray);
        kvRow(app, "Wall tests", fmtInt(st.wallTests).c_str(), gloss::wallTest);
        kvRow(app, "Squares walked", fmtInt(st.cellsVisited).c_str(), gloss::cellsWalked);
        kvRow(app, "Polygon vertices", fmtInt(app.result.polygon.size()).c_str(), gloss::outlinePoints);
        ImGui::EndTable();
    }
    cardEnd(app);

    cardBegin(app, "Edit cost", gloss::editCost);
    if (ui::beginTable("edit", 2)) {
        tableColumns();
        kvRow(app, "Grid rebuild", fmtDuration(rb.gridBuildUs).c_str());
        kvRow(app, "Crossings + targets", fmtDuration(rb.intersectUs).c_str());
        ImGui::EndTable();
    }
    cardTextSmall(app, kTextDim, "Paid per edit, not per frame.");
    cardEnd(app);

    cardBegin(app, "Compare", gloss::compare);
    if (ImGui::Checkbox("Run both", &app.compare)) app.dirty = true;
    if (app.compare) compareTable(app);
    cardEnd(app);

    cardBegin(app, "Grid size", gloss::grid);
    bool a = app.autoGrid;
    if (ImGui::Checkbox("Auto", &a)) {
        app.autoGrid = a;
        app.applyGridSettings();
    }
    ImGui::BeginDisabled(app.autoGrid);
    bool edited = false;
    const float sw = cardWidth() * 0.62f;
    ImGui::SetNextItemWidth(sw);
    edited |= ImGui::SliderInt("columns", &app.gridCols, 1, 256, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(sw);
    edited |= ImGui::SliderInt("rows", &app.gridRows, 1, 256, "%d", ImGuiSliderFlags_Logarithmic);
    ImGui::EndDisabled();
    if (edited && !app.autoGrid) app.applyGridSettings();
    const vis::Grid& g = app.scene.grid();
    if (ui::beginTable("grid", 2)) {
        tableColumns();
        kvRow(app, "Squares", (std::to_string(g.cols()) + " x " + std::to_string(g.rows())).c_str(), gloss::square);
        kvRow(app, "Square size", (fmtNum(g.cellWidth(), 1) + " x " + fmtNum(g.cellHeight(), 1)).c_str());
        kvRow(app, "Registrations", fmtInt(g.registrations()).c_str(), gloss::registrations);
        kvRow(app, "Max per square", fmtInt(app.maxCellWalls).c_str());
        ImGui::EndTable();
    }
    cardEnd(app);

    cardBegin(app, "Keys");
    const char* lines[] = {
        "Tab / M    method",
        "O          show",
        "1 2 3 4    scenes",
        "R          reset",
        "C          clear walls",
        "Shift      snap ends",
        "Esc        cancel / close tour",
    };
    {
        MonoScope m(app);
        for (const char* l : lines) ImGui::TextUnformatted(l);
    }
    cardEnd(app);
}
