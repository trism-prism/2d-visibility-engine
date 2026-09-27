// Control panel, status line, fonts and theme.
#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "app.h"

namespace {

// A button that also records its centre, so a script can click it with `move @label`.
bool button(App& app, const char* label, ImVec2 size = ImVec2(0, 0)) {
    const bool pressed = ImGui::Button(label, size);
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    app.widgets[label] = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
    return pressed;
}


void kv(App& app, const char* label, const char* value, const char* hint = nullptr) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    if (hint && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushFont(app.monoFont, 0.0f);
    ImGui::TextUnformatted(value);
    ImGui::PopFont();
}

void sectionNote(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.63f, 0.74f, 1.0f));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

}  // namespace

void setupUi(App& app, float scale) {
    ImGuiIO& io = ImGui::GetIO();
    app.uiScale = scale;
    const auto exists = [](const char* p) { return std::filesystem::exists(p); };
    ImFont* ui = nullptr;
    if (exists("C:/Windows/Fonts/segoeui.ttf")) ui = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 15.0f * scale);
    if (!ui) ui = io.Fonts->AddFontDefault();
    app.monoFont = nullptr;
    if (exists("C:/Windows/Fonts/consola.ttf")) app.monoFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 14.5f * scale);
    if (!app.monoFont) app.monoFont = ui;
    io.FontDefault = ui;

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.FrameRounding = 4.0f * scale;
    s.GrabRounding = 4.0f * scale;
    s.ChildRounding = 4.0f * scale;
    s.WindowPadding = {12.0f * scale, 8.0f * scale};
    s.FramePadding = {8.0f * scale, 3.0f * scale};
    s.ItemSpacing = {8.0f * scale, 4.0f * scale};
    s.CellPadding = {4.0f * scale, 2.0f * scale};
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 1.0f * scale;  // visible outline: off states of radios and checkboxes read clearly
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.082f, 0.105f, 1.0f);
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.54f, 0.62f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.14f, 0.16f, 0.21f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.19f, 0.22f, 0.29f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.26f, 0.34f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.19f, 0.26f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.30f, 0.42f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.38f, 0.54f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.42f, 0.47f, 0.58f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.23f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.30f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.28f, 0.36f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.78f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.70f, 0.95f, 1.0f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.52f, 0.80f, 1.0f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.20f, 0.23f, 0.30f, 1.0f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.15f, 0.17f, 0.22f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.15f, 0.17f, 0.22f, 1.0f);
}

void App::drawPanel() {
    ImGui::SetNextWindowPos({displaySize.x - panelWidth(), 0.0f});
    ImGui::SetNextWindowSize({panelWidth(), displaySize.y - statusHeight()});
    ImGui::Begin("controls", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    const float full = ImGui::GetContentRegionAvail().x;
    char b[160];

    // ---- Mode ----
    if (ImGui::CollapsingHeader("Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
        int m = mode == vis::Mode::Brute ? 0 : 1;
        bool changed = false;
        changed |= ImGui::RadioButton("Baseline: every ray tests every wall", &m, 0);
        changed |= ImGui::RadioButton("Grid: rays walk cells, test only walls registered there", &m, 1);
        if (changed && (m == 0) != (mode == vis::Mode::Brute)) toggleMode();
    }

    // ---- Scene ----
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        const float half = (full - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (button(*this, "Reset demo", {half, 0})) resetDemo();
        ImGui::SameLine();
        if (button(*this, "Clear walls", {half, 0})) clearWalls();
        if (button(*this, "Scatter - favourable", {half, 0})) loadScene(vis::SceneKind::Scatter);
        ImGui::SameLine();
        if (button(*this, "Maze - mixed", {half, 0})) loadScene(vis::SceneKind::Maze);
        if (button(*this, "Long walls - unfavourable", {half, 0})) loadScene(vis::SceneKind::LongWalls);
        ImGui::SameLine();
        if (button(*this, "Cluster - unfavourable", {half, 0})) loadScene(vis::SceneKind::Cluster);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("walls");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(full * 0.30f);
        if (ImGui::InputInt("##walls", &stressWalls, 0)) stressWalls = std::clamp(stressWalls, 100, 5000);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stress scene wall count, 100-5000. Same count + seed = same scene.");
        ImGui::SameLine();
        ImGui::TextUnformatted("seed");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(full * 0.30f);
        if (ImGui::InputInt("##seed", &seed, 0)) seed = std::max(seed, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stress scene seed. Same count + seed = same scene.");
    }

    // ---- Overlay ----
    if (ImGui::CollapsingHeader("Overlay", ImGuiTreeNodeFlags_DefaultOpen)) {
        int o = static_cast<int>(overlay);
        bool changed = false;
        changed |= ImGui::RadioButton("Off", &o, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Grid cells", &o, 1);
        changed |= ImGui::RadioButton("Probe ray", &o, 2);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("All rays", &o, 3);
        if (changed) {
            overlay = static_cast<Overlay>(o);
            dirty = true;
        }
        ImGui::Checkbox("Outline visibility polygon", &outline);
        if (overlay == Overlay::Probe) {
            if (probeRay >= 0 && probeRay < static_cast<int>(trace.rays.size())) {
                const vis::Trace::RayRecord& r = trace.rays[static_cast<std::size_t>(probeRay)];
                if (ImGui::BeginTable("probe", 2)) {
                    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 1.1f);
                    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    std::snprintf(b, sizeof b, "%u", r.cellEnd - r.cellBegin);
                    kv(*this, "Cells walked", b);
                    std::snprintf(b, sizeof b, "%u of %zu", r.wallEnd - r.wallBegin, scene.wallCount());
                    kv(*this, "Walls tested", b);
                    std::snprintf(b, sizeof b, "%.4f", r.tMinus);
                    kv(*this, "tMinus", b, "limit of the nearest hit from the clockwise side (units of the ray vector)");
                    std::snprintf(b, sizeof b, "%.4f", r.tPlus);
                    kv(*this, "tPlus", b, "limit of the nearest hit from the counter-clockwise side");
                    ImGui::EndTable();
                }
            } else {
                sectionNote("Move the cursor over the canvas to pick a ray.");
            }
        }
    }

    // ---- Dashboard ----
    if (ImGui::CollapsingHeader("Dashboard", ImGuiTreeNodeFlags_DefaultOpen)) {
        const ModeStats& cur = stats[static_cast<int>(mode)];
        const vis::VisibilityStats& st = result.stats;
        const vis::RebuildStats& rb = scene.lastRebuild();
        if (ImGui::BeginTable("dash", 2)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            std::snprintf(b, sizeof b, "%.1f ms  %.0f fps", frameMs, 1000.0 / std::max(frameMs, 0.01));
            kv(*this, "Frame", b, vsync ? "Whole frame incl. rendering and GUI, smoothed. Vsync is on, so it is capped by the display."
                                        : "Whole frame incl. rendering and GUI, smoothed. Vsync is off.");
            std::snprintf(b, sizeof b, "%.1f us", cur.total.median());
            kv(*this, "Query", b, "Median of the last 60 timed queries: sort + cast + emit. Excludes overlay tracing and drawing.");
            std::snprintf(b, sizeof b, "%.1f / %.1f", cur.sort.median(), cur.cast.median());
            kv(*this, "  sort / cast (us)", b);
            std::snprintf(b, sizeof b, "%zu + 4", scene.userWallCount());
            kv(*this, "Walls", b, "user walls + 4 boundary walls");
            std::snprintf(b, sizeof b, "%zu", scene.intersections().size());
            kv(*this, "Crossings k", b, "pairwise proper intersections");
            std::snprintf(b, sizeof b, "%zu", scene.targets().size());
            kv(*this, "Targets T", b, "de-duplicated endpoints + crossings");
            std::snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(st.raysCast));
            kv(*this, "Rays cast", b);
            std::snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(st.wallTests));
            kv(*this, "Wall tests", b, "actual rayHit calls");
            std::snprintf(b, sizeof b, "%llu", static_cast<unsigned long long>(st.cellsVisited));
            kv(*this, "Cells walked", b, "0 in Baseline");
            std::snprintf(b, sizeof b, "%zu", result.polygon.size());
            kv(*this, "Polygon vertices", b);
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Last edit cost (paid on edit, not per frame)");
        if (ImGui::BeginTable("edit", 2)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            std::snprintf(b, sizeof b, "%.1f us", rb.gridBuildUs);
            kv(*this, "Grid rebuild", b);
            std::snprintf(b, sizeof b, "%.1f us", rb.intersectUs);
            kv(*this, "Crossings + targets", b);
            ImGui::EndTable();
        }
        ImGui::Spacing();
        if (ImGui::Checkbox("Compare both modes", &compare)) dirty = true;
        if (compare) {
            const ModeStats& base = stats[0];
            const ModeStats& grid = stats[1];
            if (ImGui::BeginTable("cmp", 3)) {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.8f);
                ImGui::TableSetupColumn("Baseline", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Grid", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableHeadersRow();
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("query us");
                ImGui::PushFont(monoFont, 0.0f);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.1f", base.total.median());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.1f", grid.total.median());
                ImGui::PopFont();
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("wall tests");
                ImGui::PushFont(monoFont, 0.0f);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%llu", static_cast<unsigned long long>(base.last.wallTests));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%llu", static_cast<unsigned long long>(grid.last.wallTests));
                ImGui::PopFont();
                ImGui::EndTable();
            }
            const double bm = base.total.median(), gm = grid.total.median();
            ImGui::Text("Baseline / Grid time: ");
            ImGui::SameLine();
            ImGui::PushFont(monoFont, 0.0f);
            ImGui::Text("%.1fx", gm > 0 ? bm / gm : 0.0);
            ImGui::PopFont();
            const bool ok = identical;
            ImGui::TextUnformatted("Outputs identical: ");
            ImGui::SameLine();
            ImGui::TextColored(ok ? ImVec4(0.45f, 0.9f, 0.55f, 1) : ImVec4(1, 0.4f, 0.4f, 1), ok ? "yes" : "NO");
            sectionNote("Runs both back ends every frame: frame time is higher while enabled.");
        }
    }

    // ---- Grid ----
    if (ImGui::CollapsingHeader("Grid")) {
        bool a = autoGrid;
        if (ImGui::Checkbox("Automatic resolution", &a)) {
            autoGrid = a;
            applyGridSettings();
        }
        ImGui::BeginDisabled(autoGrid);
        bool edited = false;
        ImGui::SetNextItemWidth(full * 0.62f);
        edited |= ImGui::SliderInt("cols", &gridCols, 1, 256, "%d", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(full * 0.62f);
        edited |= ImGui::SliderInt("rows", &gridRows, 1, 256, "%d", ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
        if (edited && !autoGrid) applyGridSettings();
        const vis::Grid& g = scene.grid();
        if (ImGui::BeginTable("grid", 2)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            std::snprintf(b, sizeof b, "%d x %d", g.cols(), g.rows());
            kv(*this, "Cells", b);
            std::snprintf(b, sizeof b, "%.1f x %.1f", g.cellWidth(), g.cellHeight());
            kv(*this, "Cell size (world)", b);
            std::snprintf(b, sizeof b, "%zu", g.registrations());
            kv(*this, "Registrations", b, "(wall, cell) pairs in the grid");
            std::snprintf(b, sizeof b, "%zu", maxCellWalls);
            kv(*this, "Max walls in a cell", b);
            ImGui::EndTable();
        }
    }

    // ---- Controls ----
    if (ImGui::CollapsingHeader("Controls")) {
        const char* lines[] = {
            "Drag the light handle      move the light",
            "Drag on empty canvas       draw a wall",
            "  + Shift                  snap to wall ends",
            "Right-click a wall         delete it",
            "Tab / M                    Baseline <-> Grid",
            "R                          reset demo",
            "C                          clear walls",
            "O                          cycle overlay",
            "1 2 3 4                    stress scenes",
            "Esc                        cancel wall",
        };
        ImGui::PushFont(monoFont, 0.0f);
        for (const char* l : lines) ImGui::TextUnformatted(l);
        ImGui::PopFont();
    }
    ImGui::End();
}

void App::drawStatusLine() {
    ImGui::SetNextWindowPos({0.0f, displaySize.y - statusHeight()});
    ImGui::SetNextWindowSize({displaySize.x - panelWidth(), statusHeight()});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f * uiScale, 5.0f * uiScale});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.06f, 0.078f, 1.0f));
    ImGui::Begin("status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    if (now < statusUntil && !status.empty()) {
        ImGui::TextColored(statusIsError ? ImVec4(1.0f, 0.55f, 0.45f, 1) : ImVec4(0.55f, 0.85f, 1.0f, 1), "%s", status.c_str());
    } else {
        ImGui::TextDisabled("Drag the light or draw walls. Right-click deletes. Tab: mode, O: overlay, R: reset.");
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
