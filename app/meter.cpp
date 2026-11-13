// The work meter: baseline-to-improvement at a glance. Both bars are drawn from WorkMeter, which comes
// from real counters (see app.h): rays x walls for "Check every wall" (exact arithmetic) and the wall
// tests the grid method needs for the current light position.
#include <algorithm>
#include <cmath>

#include "app.h"
#include "panel.h"
#include "widgets.h"

using namespace ui;

std::string App::meterVerdict() const {
    if (!meter.valid || meter.baselineTests == 0) return {};
    if (!meter.gridKnown())
        return "Not measured: switch to the grid to see it.";
    const double ratio = static_cast<double>(meter.gridTests) / static_cast<double>(meter.baselineTests);
    const long pct = std::lround(ratio * 100.0);
    if (ratio >= 0.9) {
        std::string s = "The grid saves almost nothing here";
        const std::size_t user = scene.userWallCount();
        const double perWall = static_cast<double>(scene.grid().registrations()) / static_cast<double>(std::max<std::size_t>(1, scene.wallCount()));
        if (user >= 20 && maxCellWalls * 5 >= user) s += ": the walls are packed into a few squares.";
        else if (perWall >= 6.0) s += ": the walls are long, so each one sits in many squares.";
        else s += ".";
        return s;
    }
    if (ratio > 0.0 && pct < 1) return "The grid does less than 1% of the work here.";
    return "The grid does " + std::to_string(pct) + "% of the work here.";
}

namespace {

// One labelled bar. `value` is on the shared scale [0, maxValue]; a non-zero value never draws
// narrower than 3 px, its true number is printed in the row above.
void bar(App& app, const char* label, const std::string& number, std::uint64_t value, std::uint64_t maxValue,
         ImU32 accent, bool active, bool known) {
    const float ui = app.uiScale;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = cardWidth();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float lineH = ImGui::GetTextLineHeight();
    const ImU32 labelCol = active ? IM_COL32(255, 255, 255, 255) : kTextDim;
    dl->AddText(p, labelCol, label);
    float tagRight = p.x + ImGui::CalcTextSize(label).x;
    if (active) {
        FontScope f(app, 11.5f);
        const char* tag = "in use";
        const ImVec2 ts = ImGui::CalcTextSize(tag);
        const ImVec2 lo{tagRight + 6.0f * ui, p.y + (lineH - ts.y) * 0.5f - 1.0f * ui};
        dl->AddRectFilled(lo, {lo.x + ts.x + 8.0f * ui, lo.y + ts.y + 2.0f * ui}, accent, 3.0f * ui);
        dl->AddText({lo.x + 4.0f * ui, lo.y + 1.0f * ui}, IM_COL32(12, 14, 20, 255), tag);
    }
    {
        MonoScope m(app);
        const ImVec2 ns = ImGui::CalcTextSize(number.c_str());
        dl->AddText({p.x + w - ns.x, p.y}, known ? (active ? IM_COL32(255, 255, 255, 255) : kText) : kTextDim, number.c_str());
    }
    const float barH = 10.0f * ui;
    const ImVec2 b0{p.x, p.y + lineH + 3.0f * ui};
    const ImVec2 b1{p.x + w, b0.y + barH};
    dl->AddRectFilled(b0, b1, kTrack, 3.0f * ui);
    if (known) {
        const float frac = maxValue > 0 ? static_cast<float>(static_cast<double>(value) / static_cast<double>(maxValue)) : 0.0f;
        float fw = w * std::clamp(frac, 0.0f, 1.0f);
        if (value > 0) fw = std::max(fw, 3.0f * ui);
        ImU32 fill = accent;
        if (!active) fill = (accent & 0x00FFFFFFu) | (110u << IM_COL32_A_SHIFT);
        dl->AddRectFilled(b0, {b0.x + fw, b1.y}, fill, 3.0f * ui);
        if (active) dl->AddRect(b0, b1, IM_COL32(255, 255, 255, 120), 3.0f * ui, 0, 1.0f * ui);
    } else {
        for (float x = b0.x; x < b1.x; x += 8.0f * ui)
            dl->AddLine({x, b1.y - 1.0f}, {std::min(x + 4.0f * ui, b1.x), b1.y - 1.0f}, kTextDim, 1.0f);
    }
    ImGui::Dummy({w, lineH + 3.0f * ui + barH});
}

}  // namespace

void drawWorkMeter(App& app) {
    cardBegin(app, "How much work?", gloss::meter);
    const WorkMeter& m = app.meter;
    if (!m.valid) {
        cardText(kTextDim, "Measuring…");
        cardEnd(app);
        return;
    }
    const bool brute = app.mode == vis::Mode::Brute;
    const std::uint64_t maxV = std::max<std::uint64_t>(1, std::max(m.baselineTests, m.gridKnown() ? m.gridTests : 0));
    bar(app, "Check every wall", fmtInt(m.baselineTests), m.baselineTests, maxV, kAmber, brute, true);
    bar(app, "Use the grid", m.gridKnown() ? fmtInt(m.gridTests) : "not measured", m.gridTests, maxV, kCyan, !brute, m.gridKnown());
    cardTextSmall(app, kTextDim, "%s rays × %s walls = %s checks", fmtInt(m.rays).c_str(), fmtInt(m.walls).c_str(),
                  fmtInt(m.baselineTests).c_str());
    tooltip(gloss::wallTest);
    cardText(kText, "%s", app.meterVerdict().c_str());

    // Time for the active method: the median of real timed queries (nothing from the untimed passes).
    const ModeStats& cur = app.stats[static_cast<int>(app.mode)];
    const double us = cur.total.n > 0 ? cur.total.median() : app.result.stats.totalUs;
    ImGui::TextUnformatted("Time to work out the lit area:");
    ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    ImGui::SameLine(0.0f, 5.0f * app.uiScale);
    {
        MonoScope mono(app);
        ImGui::TextUnformatted((app.detail == Detail::Simple ? fmtMs(us) : fmtDuration(us)).c_str());
    }
    hi.x = std::max(hi.x, ImGui::GetItemRectMax().x);
    if (ImGui::IsMouseHoveringRect(lo, hi) && ImGui::IsWindowHovered()) {
        if (ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
            ImGui::TextUnformatted(app.detail == Detail::Simple
                                       ? "1 ms = one thousandth of a second. For comparison, a 60 fps screen draws a new frame every 16.7 ms."
                                       : gloss::query);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    cardEnd(app);
}
