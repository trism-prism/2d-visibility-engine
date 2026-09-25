#pragma once
#include <imgui.h>

#include "scene.h"
#include "visibility.h"

// World <-> screen mapping: uniform scale, world y grows downwards on screen.
struct View {
    float scale = 1.0f;
    ImVec2 origin{0.0f, 0.0f};  // screen position of world (0, 0)

    ImVec2 toScreen(vis::Vec2 p) const {
        return {origin.x + static_cast<float>(p.x) * scale, origin.y + static_cast<float>(p.y) * scale};
    }
    vis::Vec2 toWorld(ImVec2 s) const {
        return {static_cast<double>((s.x - origin.x) / scale), static_cast<double>((s.y - origin.y) / scale)};
    }
};

// Largest centred view of the world inside `canvas` with `margin` pixels around it.
View fitView(const vis::Scene& scene, ImVec2 canvas, float margin);

// Draws world panel, lit area (triangle fan from the light), walls and the light handle.
void drawWorld(ImDrawList* dl, const View& view, const vis::Scene& scene, const vis::VisibilityResult& result);
