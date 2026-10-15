// Control panel (two levels of detail), status line, fonts and theme.
#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "app.h"
#include "panel.h"
#include "widgets.h"

using namespace ui;

namespace {

constexpr int kWallStops[] = {100, 250, 500, 1000, 2000, 3000, 5000};
constexpr int kWallStopCount = static_cast<int>(sizeof kWallStops / sizeof kWallStops[0]);

int nearestStop(int walls) {
    int best = 0;
    for (int i = 1; i < kWallStopCount; ++i)
        if (std::abs(kWallStops[i] - walls) < std::abs(kWallStops[best] - walls)) best = i;
    return best;
}

void explainerCard(App& app) {
    cardBegin(app, nullptr);
    {
        FontScope f(app, 17.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kText));
        ImGui::TextUnformatted("Light and walls");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {7.0f * app.uiScale, 1.0f * app.uiScale});
        FontScope small(app, 13.0f);
        const float bw = ImGui::CalcTextSize("Tour").x + 14.0f * app.uiScale;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - bw));
        if (button(app, "Tour")) app.tourStart();
        ImGui::PopStyleVar();
    }
    cardText(IM_COL32(204, 212, 230, 255),
             "Bright = what the light sees. Updates live.");
    {
        FontScope f(app, 12.0f);
        cardText(kTextDim, "Drag the light \u00B7 drag the floor to draw \u00B7 right-click to erase");
    }
    cardEnd(app);
}

void methodCard(App& app) {
    cardBegin(app, nullptr);
    const char* titles[] = {"All walls", "Grid"};
    const char* subs[] = {"Baseline: every ray, every wall.", "Each ray tests nearby walls only."};
    const ImU32 accents[] = {kAmber, kCyan};
    const int clicked = methodCards(app, titles, subs, accents, 2, app.mode == vis::Mode::Brute ? 0 : 1);
    if (clicked >= 0) app.setMode(clicked == 0 ? vis::Mode::Brute : vis::Mode::Grid);
    cardTextSmall(app, kTextDim, "Same picture. Less work.");
    cardEnd(app);
}

void showCard(App& app) {
    cardBegin(app, "Show");
    const char* labels[] = {"Light only", "Squares", "One ray", "All rays"};
    int o = static_cast<int>(app.overlay);
    if (segmented(app, labels, 4, 2, o, 23.0f)) app.setOverlay(static_cast<Overlay>(o));
    ImGui::Checkbox("Outline", &app.outline);
    helpMarker(gloss::outlinePoints);
    cardText(IM_COL32(204, 212, 230, 255), "%s", app.overlayCaption().c_str());
    if (app.detail == Detail::Engine) drawProbeNumbers(app);
    cardEnd(app);
}

void scenesCard(App& app) {
    cardBegin(app, nullptr);
    int n = 0;
    const SceneInfo* infos = sceneInfos(n);
    for (int i = 0; i < n; ++i)
        if (sceneRow(app, infos[i].name, infos[i].blurb, infos[i].key, app.kind == infos[i].kind)) app.loadScene(infos[i].kind);

    int idx = nearestStop(app.stressWalls);
    // "Walls" caption, slider, value: the value sits outside the track so the grab never covers it.
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float capW = ImGui::CalcTextSize("Walls").x, valW = ImGui::CalcTextSize("5,000").x + 2.0f * app.uiScale;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Walls");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(cardWidth() - capW - valW - 2.0f * sp);
    if (ImGui::SliderInt("##walls", &idx, 0, kWallStopCount - 1, " ", ImGuiSliderFlags_NoInput)) app.stressWalls = kWallStops[idx];
    const bool slid = ImGui::IsItemDeactivatedAfterEdit();
    tooltip("Walls in generated scenes (100-5,000). Release to rebuild.");
    if (slid && app.kind != vis::SceneKind::Demo) app.loadScene(app.kind);

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(fmtInt(static_cast<std::uint64_t>(app.stressWalls)).c_str());
    const float half = (cardWidth() - sp) * 0.5f;
    if (button(app, "Clear", {half, 0})) app.clearWalls();
    tooltip("Remove all walls. Key: C");
    ImGui::SameLine();
    if (button(app, "Reset", {half, 0})) app.resetDemo();
    tooltip("Back to the demo. Key: R");

    if (app.detail == Detail::Engine) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Seed");
        tooltip("Same count + seed = same scene.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(cardWidth() * 0.4f);
        if (ImGui::InputInt("##seed", &app.seed, 0)) app.seed = std::max(app.seed, 0);
        if (ImGui::IsItemDeactivatedAfterEdit() && app.kind != vis::SceneKind::Demo) app.loadScene(app.kind);
    }

    const std::string note = app.slowNote();
    if (!note.empty()) cardTextSmall(app, IM_COL32(255, 196, 120, 255), "%s", note.c_str());
    cardEnd(app);
}

}  // namespace

void setupUi(App& app, float scale) {
    ImGuiIO& io = ImGui::GetIO();
    app.uiScale = scale;
    const auto exists = [](const char* p) { return std::filesystem::exists(p); };
    ImFont* ui = nullptr;
    if (exists("C:/Windows/Fonts/segoeui.ttf")) ui = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 14.5f * scale);
    if (!ui) ui = io.Fonts->AddFontDefault();
    app.uiFont = ui;  // other sizes (headings, captions) are the same font pushed at another size
    app.monoFont = nullptr;
    if (exists("C:/Windows/Fonts/consola.ttf")) app.monoFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/consola.ttf", 14.0f * scale);
    if (!app.monoFont) app.monoFont = ui;
    io.FontDefault = ui;

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.FrameRounding = 4.0f * scale;
    s.GrabRounding = 4.0f * scale;
    s.ChildRounding = 4.0f * scale;
    s.PopupRounding = 5.0f * scale;
    s.WindowPadding = {8.0f * scale, 6.0f * scale};
    s.FramePadding = {8.0f * scale, 3.0f * scale};
    s.ItemSpacing = {8.0f * scale, 2.5f * scale};
    s.CellPadding = {4.0f * scale, 2.0f * scale};
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 1.0f * scale;
    s.ScrollbarSize = 10.0f * scale;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.082f, 0.105f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.10f, 0.13f, 0.98f);
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.62f, 0.72f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.14f, 0.16f, 0.21f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.19f, 0.22f, 0.29f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.22f, 0.26f, 0.34f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.20f, 0.28f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.30f, 0.42f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.38f, 0.54f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.34f, 0.44f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.20f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.21f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.26f, 0.34f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(0.40f, 0.78f, 1.0f, 1.0f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.52f, 0.78f, 0.80f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.52f, 0.80f, 1.0f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.20f, 0.23f, 0.30f, 1.0f);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.14f, 0.19f, 1.0f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.17f, 0.19f, 0.25f, 1.0f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.17f, 0.19f, 0.25f, 1.0f);
}

void App::drawPanel() {
    ImGui::SetNextWindowPos({displaySize.x - panelWidth(), 0.0f});
    ImGui::SetNextWindowSize({panelWidth(), displaySize.y - statusHeight()});
    ImGui::Begin("controls", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    panelScrollMax = ImGui::GetScrollMaxY();
    panelOverflow = panelScrollMax > 1.0f;  // from the previous frame's content size

    const char* levels[] = {"Simple", "Engine"};
    int d = detail == Detail::Engine ? 1 : 0;
    if (segmented(*this, levels, 2, 2, d, 24.0f)) detail = d == 1 ? Detail::Engine : Detail::Simple;
    explainerCard(*this);
    methodCard(*this);
    showCard(*this);
    drawWorkMeter(*this);
    scenesCard(*this);
    if (detail == Detail::Engine) {
        ImGui::Dummy({0, 4.0f * uiScale});
        drawEngineDetails(*this);
    }
    panelSlack = ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - ImGui::GetStyle().WindowPadding.y;
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
        ImGui::TextDisabled("Tab method \u00B7 O show \u00B7 1-4 scenes \u00B7 R reset \u00B7 C clear");
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
