#include "legend.h"

#include <algorithm>
#include <cfloat>

#include "widgets.h"

namespace {

constexpr float kPx = 13.0f, kTitlePx = 14.5f, kPad = 9.0f, kSwatchW = 22.0f, kRowGap = 3.0f;

float rowHeight(const App& app, const LegendRow& r, float wrap, bool caption) {
    const float px = kPx * app.uiScale;
    const float w = wrap - (r.sw == Swatch::None ? 0.0f : kSwatchW * app.uiScale);
    float h = std::max(app.uiFont->CalcTextSizeA(px, FLT_MAX, w, r.text.c_str()).y, px);
    if (caption) h = std::max(h, 2.0f * px * 1.2f);  // always room for two lines: the height never jumps
    return h;
}

float wrapWidth(const App& app, float width) { return width - 2.0f * kPad * app.uiScale; }

}  // namespace

LegendModel buildLegend(const App& app, bool condensed) {
    LegendModel m;
    const std::string caption = app.overlayCaption();
    m.rows.push_back({Swatch::None, 0, caption});
    const bool grid = app.mode == vis::Mode::Grid;
    switch (app.overlay) {
        case Overlay::Off: break;
        case Overlay::Grid:
            m.title = "The grid";
            if (condensed) break;
            if (grid) {
                std::uint32_t mx = 0;
                for (std::uint32_t v : app.trace.cellVisits) mx = std::max(mx, v);
                m.rows.push_back({Swatch::Heat, 0, "Brighter square: more rays crossed it (busiest: " + ui::fmtInt(mx) + ")"});
                m.rows.push_back({Swatch::Dot, IM_COL32(190, 200, 225, 220), "A dot marks a square that holds at least one wall"});
            } else {
                m.rows.push_back({Swatch::Line, IM_COL32(120, 210, 255, 220), "One ray"});
            }
            break;
        case Overlay::Probe:
            m.title = "Follow one ray";
            if (condensed) break;
            m.rows.push_back({Swatch::Line, ui::kAmber, grid ? "Walls this ray had to test" : "Every wall is tested"});
            if (grid) {
                m.rows.push_back({Swatch::Line, IM_COL32(120, 128, 150, 255), "Walls it skipped"});
                m.rows.push_back({Swatch::Square, ui::kCyan, "Squares it crossed, numbered in order"});
            }
            m.rows.push_back({Swatch::Dot, ui::kGold, "Where the ray hits a wall"});
            m.rows.push_back({Swatch::DotPair, 0, "Two colours: the two sides of the ray end at different walls"});
            break;
        case Overlay::Rays:
            m.title = "Every ray";
            if (condensed) break;
            m.rows.push_back({Swatch::Line, IM_COL32(120, 210, 255, 220), "One ray, drawn to where it stops"});
            break;
    }
    return m;
}

ImVec2 legendSize(const App& app, const LegendModel& m, float width) {
    const float ui = app.uiScale;
    const float wrap = wrapWidth(app, width);
    float h = kTitlePx * ui * 1.3f;
    for (std::size_t i = 0; i < m.rows.size(); ++i) h += rowHeight(app, m.rows[i], wrap, i == 0) + kRowGap * ui;
    return {width, h + 2.0f * kPad * ui - kRowGap * ui};
}

void drawLegend(ImDrawList* dl, const App& app, const LegendModel& m, ImVec2 lo, float width) {
    const float ui = app.uiScale;
    const float pad = kPad * ui, px = kPx * ui, titlePx = kTitlePx * ui, swW = kSwatchW * ui;
    ImFont* font = app.uiFont;
    const float wrap = wrapWidth(app, width);
    const ImVec2 size = legendSize(app, m, width);
    dl->AddRectFilled(lo, {lo.x + size.x, lo.y + size.y}, IM_COL32(18, 21, 30, 245), 6.0f * ui);
    dl->AddRect(lo, {lo.x + size.x, lo.y + size.y}, IM_COL32(70, 78, 100, 255), 6.0f * ui);
    float y = lo.y + pad;
    dl->AddText(font, titlePx, {lo.x + pad, y}, IM_COL32(240, 244, 255, 255), m.title.c_str());
    y += titlePx * 1.3f;
    for (std::size_t i = 0; i < m.rows.size(); ++i) {
        const LegendRow& r = m.rows[i];
        const float cy = y + px * 0.6f;
        const float sx = lo.x + pad;
        switch (r.sw) {
            case Swatch::None: break;
            case Swatch::Line: dl->AddLine({sx, cy}, {sx + 16.0f * ui, cy}, r.col, 3.0f * ui); break;
            case Swatch::Square:
                dl->AddRectFilled({sx, cy - 6.0f * ui}, {sx + 14.0f * ui, cy + 6.0f * ui}, (r.col & 0x00FFFFFFu) | (70u << IM_COL32_A_SHIFT));
                dl->AddRect({sx, cy - 6.0f * ui}, {sx + 14.0f * ui, cy + 6.0f * ui}, (r.col & 0x00FFFFFFu) | (200u << IM_COL32_A_SHIFT));
                break;
            case Swatch::Dot: dl->AddCircleFilled({sx + 7.0f * ui, cy}, 4.5f * ui, r.col, 14); break;
            case Swatch::DotPair:
                dl->AddCircleFilled({sx + 4.0f * ui, cy}, 4.0f * ui, ui::kGreen, 14);
                dl->AddCircleFilled({sx + 14.0f * ui, cy}, 4.0f * ui, ui::kPink, 14);
                break;
            case Swatch::Heat:
                for (int k = 0; k < 3; ++k)
                    dl->AddRectFilled({sx + static_cast<float>(k) * 6.0f * ui, cy - 6.0f * ui},
                                      {sx + static_cast<float>(k + 1) * 6.0f * ui, cy + 6.0f * ui}, heatColour(0.15f + 0.4f * static_cast<float>(k)));
                break;
        }
        const bool none = r.sw == Swatch::None;
        const float tx = lo.x + pad + (none ? 0.0f : swW);
        dl->AddText(font, px, {tx, y}, IM_COL32(204, 212, 230, 255), r.text.c_str(), nullptr, wrap - (none ? 0.0f : swW));
        y += rowHeight(app, r, wrap, i == 0) + kRowGap * ui;
    }
}

// The strip below the world: tour card and legend side by side when they fit, stacked otherwise.
StripLayout App::stripLayout() const {
    StripLayout s;
    s.legend = overlay != Overlay::Off;
    s.tour = tourActive;
    if (!s.legend && !s.tour) return s;
    const float margin = 16.0f * uiScale, gap = 10.0f * uiScale, bottomPad = 10.0f * uiScale;
    const float avail = canvasMax.x - canvasMin.x - 2.0f * margin;
    float legW = std::min(400.0f * uiScale, avail);
    s.condensed = s.legend && avail < 700.0f * uiScale;
    float legH = 0.0f;
    if (s.legend) {
        if (s.condensed) legW = avail;
        legH = legendSize(*this, buildLegend(*this, s.condensed), legW).y;
    }
    const float tourH = tourCardH;
    float x0 = canvasMin.x + margin;
    float legX = x0, legY = 0.0f, tourX = x0, tourY = 0.0f;
    float h = 0.0f;
    if (s.legend && s.tour) {
        if (avail >= legW + gap + 430.0f * uiScale) {
            s.tourW = std::min(640.0f * uiScale, avail - legW - gap);
            const float total = s.tourW + gap + legW;
            x0 += (avail - total) * 0.5f;
            tourX = x0;
            legX = x0 + s.tourW + gap;
            h = std::max(legH, tourH);
        } else {
            s.tourW = avail;
            legW = avail;
            h = legH + gap + tourH;
            legY = 0.0f;
            tourY = legH + gap;
        }
    } else if (s.tour) {
        s.tourW = std::min(640.0f * uiScale, avail);
        tourX = x0 + (avail - s.tourW) * 0.5f;
        h = tourH;
    } else {
        h = legH;
    }
    s.height = h;
    const float top = canvasMax.y - bottomPad - h;
    s.legendPos = {legX, top + legY};
    s.legendSize = {legW, legH};
    s.tourPos = {tourX, top + tourY};
    return s;
}
