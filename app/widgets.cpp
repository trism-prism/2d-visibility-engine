#include "widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace ui {

// ---------------------------------------------------------------------------------------------
// formatting
// ---------------------------------------------------------------------------------------------

static std::string groupDigits(const std::string& digits) {
    std::string out;
    const int n = static_cast<int>(digits.size());
    for (int i = 0; i < n; ++i) {
        out += digits[static_cast<std::size_t>(i)];
        const int left = n - 1 - i;
        if (left > 0 && left % 3 == 0) out += ',';
    }
    return out;
}

std::string fmtInt(std::uint64_t v) { return groupDigits(std::to_string(v)); }

std::string fmtNum(double v, int decimals) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals, std::fabs(v));
    std::string s = b;
    const std::size_t dot = s.find('.');
    const std::string whole = dot == std::string::npos ? s : s.substr(0, dot);
    const std::string frac = dot == std::string::npos ? std::string() : s.substr(dot);
    return (v < 0 ? "-" : "") + groupDigits(whole) + frac;
}

std::string fmtDuration(double us) {
    if (!(us >= 1.0)) return "under 1 µs";
    if (us < 9.95) return fmtNum(us, 1) + " µs";
    if (us < 999.5) return fmtNum(us, 0) + " µs";
    const double ms = us / 1000.0;
    if (ms < 9.995) return fmtNum(ms, 2) + " ms";
    if (ms < 99.95) return fmtNum(ms, 1) + " ms";
    return fmtNum(ms, 0) + " ms";
}

std::string fmtMs(double us) {
    const double ms = us / 1000.0;
    if (!(ms >= 0.0005)) return "under 0.001 ms";
    if (ms < 0.9995) return fmtNum(ms, 3) + " ms";
    if (ms < 9.995) return fmtNum(ms, 2) + " ms";
    if (ms < 99.95) return fmtNum(ms, 1) + " ms";
    return fmtNum(ms, 0) + " ms";
}

// ---------------------------------------------------------------------------------------------
// glossary (definitions follow README.md, docs/ARCHITECTURE.md and docs/WALKTHROUGH.md)
// ---------------------------------------------------------------------------------------------

namespace gloss {
const char* const ray =
    "A line from the light through a wall end or crossing; where it hits becomes a corner of the lit area.";
const char* const target =
    "A wall end or crossing; each gets a ray, shared if they line up.";
const char* const crossing =
    "Where two walls cross; the lit area can change shape there.";
const char* const wallTest =
    "One ray checked against one wall; the meter counts these.";
const char* const square =
    "One square of the grid, listing the walls touching it.";
const char* const outlinePoints =
    "The corners of the lit area's outline (the visibility polygon).";
const char* const sortCast =
    "Sort puts targets in angle order; cast shoots each ray and builds the outline.";
const char* const editCost =
    "An edit rebuilds the grid, crossings and targets, once per edit.";
const char* const compare =
    "Runs both methods every update and checks they match, doubling the work.";
const char* const frame =
    "The whole frame, drawing included; capped by vsync.";
const char* const query =
    "Median of the last 60 updates (sort + cast), without drawing, overlays or the meter's extra pass.";
const char* const walls = "Your walls plus the 4 outer ones.";
const char* const cellsWalked = "Squares the rays passed through, added up; always 0 for All walls.";
const char* const registrations = "Every wall is listed in each square it touches. This is the total of those listings.";
const char* const tMinus =
    "How far along the ray (1 = the target) the nearest wall is, going clockwise.";
const char* const tPlus = "Same, going counter-clockwise.";
const char* const meter =
    "One check is one ray against one wall. All walls is rays × walls, Grid is counted at this light "
    "spot, and both bars share a scale.";
const char* const baseline = "Baseline: every ray checks every wall.";
const char* const grid =
    "Grid: a ray only checks walls in the squares it passes.";
}  // namespace gloss

// ---------------------------------------------------------------------------------------------
// generic helpers
// ---------------------------------------------------------------------------------------------

void registerWidget(App& app, const char* label, ImVec2 lo, ImVec2 hi) {
    app.widgets[label] = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f};
}

bool button(App& app, const char* label, ImVec2 size) {
    const bool pressed = ImGui::Button(label, size);
    registerWidget(app, label, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    return pressed;
}

void tooltip(const char* text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void helpMarker(const char* text) {
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 0.7f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kTextDim));
    ImGui::TextUnformatted("(?)");
    ImGui::PopStyleColor();
    tooltip(text);
}

ImVec2 textSize(const char* text, float wrap) { return ImGui::CalcTextSize(text, nullptr, false, wrap); }

FontScope::FontScope(const App& app, float px) { ImGui::PushFont(app.uiFont, px * app.uiScale); }
FontScope::~FontScope() { ImGui::PopFont(); }
MonoScope::MonoScope(const App& app) { ImGui::PushFont(app.monoFont, 0.0f); }
MonoScope::~MonoScope() { ImGui::PopFont(); }

// ---------------------------------------------------------------------------------------------
// cards: a bordered, filled block behind a group of widgets (drawn on a background channel)
// ---------------------------------------------------------------------------------------------

namespace {

struct CardState {
    ImDrawListSplitter split;
    ImVec2 start{0, 0};
    float width = 0.0f, inner = 0.0f, pad = 0.0f;
    bool active = false;
} g_card;

float availWidth() { return g_card.active ? g_card.inner : ImGui::GetContentRegionAvail().x; }

}  // namespace

void cardBegin(App& app, const char* title, const char* help) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    g_card.pad = 6.0f * app.uiScale;
    g_card.start = ImGui::GetCursorScreenPos();
    g_card.width = ImGui::GetContentRegionAvail().x;
    g_card.inner = g_card.width - 2.0f * g_card.pad;
    g_card.active = true;
    g_card.split.Split(dl, 2);
    g_card.split.SetCurrentChannel(dl, 1);
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + g_card.pad - ImGui::GetStyle().ItemSpacing.y * 0.5f);
    ImGui::Indent(g_card.pad);
    if (title) {
        {
            FontScope f(app, 15.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kText));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
        }
        if (help) helpMarker(help);
    }
}

void cardEnd(App& app) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::Unindent(g_card.pad);
    ImGui::Dummy({0.0f, g_card.pad - ImGui::GetStyle().ItemSpacing.y * 0.5f});
    ImGui::EndGroup();
    const ImVec2 mx{g_card.start.x + g_card.width, ImGui::GetItemRectMax().y};
    g_card.split.SetCurrentChannel(dl, 0);
    const float r = 6.0f * app.uiScale;
    dl->AddRectFilled(g_card.start, mx, kCardBg, r);
    dl->AddRect(g_card.start, mx, kCardBorder, r, 0, 1.0f * app.uiScale);
    g_card.split.Merge(dl);
    g_card.active = false;
    ImGui::Dummy({0.0f, 1.0f * app.uiScale});
}

float cardWidth() { return availWidth(); }

bool beginTable(const char* id, int columns) { return ImGui::BeginTable(id, columns, 0, ImVec2(availWidth(), 0.0f)); }

static void wrappedV(ImU32 col, const char* fmt, va_list args) {
    char buf[768];
    std::vsnprintf(buf, sizeof buf, fmt, args);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col));
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + availWidth());
    ImGui::TextUnformatted(buf);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void cardText(ImU32 col, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    wrappedV(col, fmt, args);
    va_end(args);
}

void cardTextSmall(const App& app, ImU32 col, const char* fmt, ...) {
    FontScope f(app, 13.0f);
    va_list args;
    va_start(args, fmt);
    wrappedV(col, fmt, args);
    va_end(args);
}

// ---------------------------------------------------------------------------------------------
// controls
// ---------------------------------------------------------------------------------------------

bool segmented(App& app, const char* const* labels, int n, int cols, int& sel, float height) {
    const float ui = app.uiScale;
    const float gap = 4.0f * ui;
    const float w = availWidth();
    const float cw = (w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    const float h = height * ui;
    const int rows = (n + cols - 1) / cols;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const int r = i / cols, c = i % cols;
        const ImVec2 lo{p0.x + static_cast<float>(c) * (cw + gap), p0.y + static_cast<float>(r) * (h + gap)};
        const ImVec2 hi{lo.x + cw, lo.y + h};
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", {cw, h}) && sel != i) {
            sel = i;
            changed = true;
        }
        ImGui::PopID();
        const bool hov = ImGui::IsItemHovered();
        const bool on = sel == i;
        const ImU32 bg = on ? IM_COL32(36, 84, 126, 255) : hov ? IM_COL32(46, 55, 74, 255) : IM_COL32(33, 39, 53, 255);
        const ImU32 bd = on ? kCyan : IM_COL32(74, 84, 108, 255);
        dl->AddRectFilled(lo, hi, bg, 5.0f * ui);
        dl->AddRect(lo, hi, bd, 5.0f * ui, 0, on ? 1.6f * ui : 1.0f * ui);
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        dl->AddText({lo.x + (cw - ts.x) * 0.5f, lo.y + (h - ts.y) * 0.5f}, on ? IM_COL32(255, 255, 255, 255) : kText, labels[i]);
        registerWidget(app, labels[i], lo, hi);
    }
    ImGui::SetCursorScreenPos({p0.x, p0.y + static_cast<float>(rows) * (h + gap) - gap});
    ImGui::Dummy({w, 0.0f});
    return changed;
}

int methodCards(App& app, const char* const* titles, const char* const* subtitles, const ImU32* accents, int n, int selected) {
    const float ui = app.uiScale;
    const float gap = 8.0f * ui;
    const float w = availWidth();
    const float cw = (w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
    const float padX = 9.0f * ui, padY = 7.0f * ui;
    const float titlePx = 15.0f * ui, subPx = 12.5f * ui;
    const float wrap = cw - 2.0f * padX;
    ImFont* font = app.uiFont;
    // Height: the tallest card decides, so both cards line up.
    float h = 0.0f;
    for (int i = 0; i < n; ++i) {
        const ImVec2 sub = font->CalcTextSizeA(subPx, FLT_MAX, wrap, subtitles[i]);
        h = std::max(h, padY * 2.0f + titlePx * 1.25f + 2.0f * ui + sub.y);
    }
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int clicked = -1;
    for (int i = 0; i < n; ++i) {
        const ImVec2 lo{p0.x + static_cast<float>(i) * (cw + gap), p0.y};
        const ImVec2 hi{lo.x + cw, lo.y + h};
        ImGui::SetCursorScreenPos(lo);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##method", {cw, h})) clicked = i;
        ImGui::PopID();
        const bool hov = ImGui::IsItemHovered();
        const bool on = selected == i;
        const ImVec4 ac = ImGui::ColorConvertU32ToFloat4(accents[i]);
        const ImU32 bg = on ? ImGui::ColorConvertFloat4ToU32({ac.x * 0.20f + 0.05f, ac.y * 0.20f + 0.06f, ac.z * 0.20f + 0.08f, 1.0f})
                            : hov ? IM_COL32(40, 47, 63, 255) : IM_COL32(31, 36, 49, 255);
        dl->AddRectFilled(lo, hi, bg, 6.0f * ui);
        dl->AddRect(lo, hi, on ? accents[i] : IM_COL32(74, 84, 108, 255), 6.0f * ui, 0, on ? 2.0f * ui : 1.0f * ui);
        // radio dot, top right
        const ImVec2 dot{hi.x - padX - 5.0f * ui, lo.y + padY + titlePx * 0.55f};
        dl->AddCircle(dot, 5.5f * ui, on ? accents[i] : IM_COL32(120, 130, 156, 255), 16, 1.4f * ui);
        if (on) dl->AddCircleFilled(dot, 3.0f * ui, accents[i], 12);
        dl->AddText(font, titlePx, {lo.x + padX, lo.y + padY}, on ? IM_COL32(255, 255, 255, 255) : kText, titles[i]);
        dl->AddText(font, subPx, {lo.x + padX, lo.y + padY + titlePx * 1.25f + 2.0f * ui}, on ? kTextSub : kTextDim,
                    subtitles[i], nullptr, wrap);
        registerWidget(app, titles[i], lo, hi);
    }
    ImGui::SetCursorScreenPos({p0.x, p0.y + h});
    ImGui::Dummy({w, 0.0f});
    return clicked;
}

bool sceneRow(App& app, const char* name, const char* blurb, const char* key, bool selected) {
    const float ui = app.uiScale;
    const float w = availWidth();
    const float padX = 8.0f * ui, padY = 2.5f * ui;
    const float namePx = 14.0f * ui, blurbPx = 12.0f * ui;
    ImFont* font = app.uiFont;
    const float wrap = w - 2.0f * padX;
    const ImVec2 bs = font->CalcTextSizeA(blurbPx, FLT_MAX, wrap, blurb);
    const float h = padY * 2.0f + namePx * 1.2f + bs.y;
    const ImVec2 lo = ImGui::GetCursorScreenPos();
    const ImVec2 hi{lo.x + w, lo.y + h};
    const bool pressed = ImGui::InvisibleButton(name, {w, h});
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(lo, hi, selected ? IM_COL32(36, 84, 126, 255) : hov ? IM_COL32(44, 52, 70, 255) : IM_COL32(31, 36, 49, 255), 5.0f * ui);
    dl->AddRect(lo, hi, selected ? kCyan : IM_COL32(60, 69, 92, 255), 5.0f * ui, 0, selected ? 1.6f * ui : 1.0f * ui);
    dl->AddText(font, namePx, {lo.x + padX, lo.y + padY}, selected ? IM_COL32(255, 255, 255, 255) : kText, name);
    if (key && *key) {
        const ImVec2 ks = font->CalcTextSizeA(blurbPx, FLT_MAX, 0.0f, key);
        const ImVec2 kl{hi.x - padX - ks.x - 8.0f * ui, lo.y + padY};
        dl->AddRectFilled(kl, {kl.x + ks.x + 8.0f * ui, kl.y + ks.y + 2.0f * ui}, IM_COL32(18, 21, 30, 255), 3.0f * ui);
        dl->AddText(font, blurbPx, {kl.x + 4.0f * ui, kl.y + 1.0f * ui}, kTextDim, key);
    }
    dl->AddText(font, blurbPx, {lo.x + padX, lo.y + padY + namePx * 1.2f}, selected ? kTextSub : kTextDim, blurb,
                nullptr, wrap);
    registerWidget(app, name, lo, hi);
    return pressed;
}

void kvRow(App& app, const char* label, const char* value, const char* hint, ImU32 swatch) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    if (swatch) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float r = 4.0f * app.uiScale;
        ImGui::GetWindowDrawList()->AddCircleFilled({p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f}, r, swatch, 12);
        ImGui::Dummy({r * 2.0f + 4.0f * app.uiScale, 0.0f});
        ImGui::SameLine(0.0f, 0.0f);
    }
    ImGui::TextUnformatted(label);
    if (hint) {
        const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (float x = lo.x; x < hi.x; x += 3.0f * app.uiScale)
            dl->AddRectFilled({x, hi.y - 1.0f}, {x + 1.0f * app.uiScale, hi.y}, IM_COL32(120, 132, 160, 170));
        tooltip(hint);
    }
    ImGui::TableSetColumnIndex(1);
    MonoScope m(app);
    const float tw = ImGui::CalcTextSize(value).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - tw));
    ImGui::TextUnformatted(value);
}

}  // namespace ui
