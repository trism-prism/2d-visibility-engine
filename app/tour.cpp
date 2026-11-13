// Guided tour: six steps, each sets the scene / method / overlay it talks about and advances on
// "Next" (it never waits for the user to perform the action).
#include <algorithm>
#include <cstdio>

#include "app.h"
#include "widgets.h"

using namespace ui;

namespace {

struct TourStep {
    const char* title;
    const char* text;
    vis::SceneKind scene;
    bool generated;  // 1,000 generated walls instead of the demo room
    vis::Mode mode;
    Overlay overlay;
};

const TourStep kSteps[kTourSteps] = {
    {"Move the light",
     "Drag the light; it's recomputed on every move.",
     vis::SceneKind::Demo, false, vis::Mode::Grid, Overlay::Off},
    {"Draw walls",
     "Drag on the floor to draw, right-click a wall to erase.",
     vis::SceneKind::Demo, false, vis::Mode::Grid, Overlay::Off},
    {"One ray",
     "Hover to pick a ray; orange walls are the only ones it checked.",
     vis::SceneKind::Demo, false, vis::Mode::Grid, Overlay::Probe},
    {"All walls",
     "Now it checks every wall: same picture, more work.",
     vis::SceneKind::Demo, false, vis::Mode::Brute, Overlay::Probe},
    {"More walls",
     "With 1,000 walls the grid does a fraction of the work.",
     vis::SceneKind::Scatter, true, vis::Mode::Grid, Overlay::Off},
    {"Worst case",
     "Everything is bunched in one corner, so the grid helps less.",
     vis::SceneKind::Cluster, true, vis::Mode::Grid, Overlay::Off},
};

}  // namespace

void App::tourGo(int step) {
    tourStep = std::clamp(step, 0, kTourSteps - 1);
    const TourStep& s = kSteps[tourStep];
    // Method first, so loading a big scene never runs a slow Baseline query.
    mode = s.mode;
    overlay = s.overlay;
    dirty = true;
    if (s.generated) {
        stressWalls = 1000;
        if (kind != s.scene || scene.userWallCount() != 1000) loadScene(s.scene);
    } else if (kind != s.scene) {
        loadScene(s.scene);
    }
}

void App::tourStart() {
    tourActive = true;
    resetDemo();  // step 1 starts from the demo room, whatever was on screen
    tourGo(0);
    status.clear();
}

void App::tourNext() {
    if (!tourActive) return;
    if (tourStep + 1 >= kTourSteps) tourClose();
    else tourGo(tourStep + 1);
}

void App::tourBack() {
    if (tourActive && tourStep > 0) tourGo(tourStep - 1);
}

void App::tourClose() { tourActive = false; }

void App::drawTourCard() {
    if (!tourActive) return;
    const float ui = uiScale;
    const float w = strip.tourW > 0.0f ? strip.tourW : 520.0f * ui;
    ImGui::SetNextWindowPos(strip.tourPos, ImGuiCond_Always, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.0f * ui, 10.0f * ui});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * ui);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f * ui);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.095f, 0.115f, 0.165f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.37f, 0.77f, 1.0f, 0.75f));
    ImGui::Begin("tour", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    const TourStep& s = kSteps[tourStep];
    const float inner = w - 28.0f * ui;
    ImGui::Dummy({inner, 0.0f});
    const float left = ImGui::GetCursorPosX();  // positions below are relative to this, never to the (auto-sized) window width

    // step counter and progress dots
    {
        FontScope f(*this, 12.5f);
        char b[48];
        std::snprintf(b, sizeof b, "%d / %d", tourStep + 1, kTourSteps);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kCyan));
        ImGui::TextUnformatted(b);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        const float r = 3.5f * ui, step = 12.0f * ui;
        const float dotsW = step * static_cast<float>(kTourSteps);
        ImGui::SetCursorPosX(left + inner - dotsW);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < kTourSteps; ++i) {
            const ImVec2 c{p.x + step * static_cast<float>(i) + r + 1.0f, p.y + ImGui::GetTextLineHeight() * 0.5f};
            if (i == tourStep) dl->AddCircleFilled(c, r, kCyan, 12);
            else dl->AddCircle(c, r, IM_COL32(120, 140, 175, 255), 12, 1.2f * ui);
        }
        ImGui::Dummy({dotsW, ImGui::GetTextLineHeight()});
    }
    {
        FontScope f(*this, 17.0f);
        ImGui::TextUnformatted(s.title);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(IM_COL32(210, 218, 234, 255)));
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + inner);
    ImGui::TextUnformatted(s.text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy({0, 2.0f * ui});

    const bool last = tourStep + 1 >= kTourSteps;
    ImGui::BeginDisabled(tourStep == 0);
    if (button(*this, "Back", {70.0f * ui, 0})) tourBack();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (button(*this, last ? "Done" : "Next", {last ? 80.0f * ui : 90.0f * ui, 0})) tourNext();
    if (last) registerWidget(*this, "Next", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::SameLine();
    ImGui::SetCursorPosX(left + inner - 70.0f * ui);
    if (button(*this, "Close", {70.0f * ui, 0})) tourClose();
    tooltip("Esc closes; Tour reopens.");

    tourCardMin = ImGui::GetWindowPos();
    const ImVec2 sz = ImGui::GetWindowSize();
    tourCardMax = {tourCardMin.x + sz.x, tourCardMin.y + sz.y};
    tourCardH = sz.y;
    const float midY = (tourCardMin.y + tourCardMax.y) * 0.5f;
    registerWidget(*this, "tour card", {tourCardMin.x + 4.0f, midY - 2.0f}, {tourCardMin.x + 8.0f, midY + 2.0f});
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
