#include "render.h"

View fitView(const vis::Scene& scene, ImVec2 canvas, float margin) {
    View v;
    const float sx = (canvas.x - 2 * margin) / static_cast<float>(scene.width());
    const float sy = (canvas.y - 2 * margin) / static_cast<float>(scene.height());
    v.scale = sx < sy ? sx : sy;
    if (v.scale < 0.01f) v.scale = 0.01f;
    v.origin = {(canvas.x - static_cast<float>(scene.width()) * v.scale) * 0.5f,
                (canvas.y - static_cast<float>(scene.height()) * v.scale) * 0.5f};
    return v;
}

void drawWorld(ImDrawList* dl, const View& view, const vis::Scene& scene, const vis::VisibilityResult& result) {
    const ImU32 floorCol = IM_COL32(24, 27, 36, 255);
    const ImU32 litCol = IM_COL32(255, 214, 140, 96);
    const ImU32 wallCol = IM_COL32(232, 236, 245, 255);
    const ImU32 frameCol = IM_COL32(96, 104, 128, 255);

    const ImVec2 lo = view.toScreen({0.0, 0.0});
    const ImVec2 hi = view.toScreen({scene.width(), scene.height()});
    dl->AddRectFilled(lo, hi, floorCol);

    // The fan triangles share edges exactly; with anti-aliased fill on, every triangle would get its
    // own feathered rim and show seams, so the fill is un-smoothed here (4x MSAA smooths the outline).
    const ImDrawListFlags saved = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    const auto& poly = result.polygon;
    if (poly.size() >= 3) {
        const ImVec2 c = view.toScreen(result.light);
        for (std::size_t i = 0; i < poly.size(); ++i) {
            dl->AddTriangleFilled(c, view.toScreen(poly[i]), view.toScreen(poly[(i + 1) % poly.size()]), litCol);
        }
    }
    dl->Flags = saved;

    dl->AddRect(lo, hi, frameCol, 0.0f, 0, 1.5f);
    const auto& walls = scene.walls();
    for (std::size_t i = vis::Scene::kBoundaryWalls; i < walls.size(); ++i) {
        dl->AddLine(view.toScreen(walls[i].a), view.toScreen(walls[i].b), wallCol, 2.5f);
    }

    const ImVec2 lp = view.toScreen(result.light);
    dl->AddCircleFilled(lp, 9.0f, IM_COL32(255, 196, 80, 90), 24);
    dl->AddCircleFilled(lp, 5.0f, IM_COL32(255, 244, 214, 255), 20);
}
