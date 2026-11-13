#pragma once
// Small widget toolkit for the panel: number formatting, tooltips with plain-language definitions,
// section cards, a segmented control and the method cards. Every widget that a script may click
// registers its centre in App::widgets under its label.
#include <cstdint>
#include <string>

#include <imgui.h>

#include "app.h"

namespace ui {

// ---- colours shared by the panel and the canvas ----
constexpr ImU32 kAmber = IM_COL32(255, 168, 64, 255);   // "Check every wall" / tested walls
constexpr ImU32 kCyan = IM_COL32(94, 196, 255, 255);    // "Use the grid" / accent
constexpr ImU32 kGreen = IM_COL32(120, 255, 170, 255);  // tMinus marker
constexpr ImU32 kPink = IM_COL32(255, 130, 220, 255);   // tPlus marker
constexpr ImU32 kGold = IM_COL32(255, 230, 120, 255);   // both sides agree
constexpr ImU32 kTextDim = IM_COL32(190, 200, 220, 255);   // secondary text, about 9:1 on the cards
constexpr ImU32 kTextSub = IM_COL32(224, 231, 245, 255);   // secondary text on a selected (tinted) card
constexpr ImU32 kText = IM_COL32(230, 235, 245, 255);
constexpr ImU32 kCardBg = IM_COL32(26, 30, 40, 255);
constexpr ImU32 kCardBorder = IM_COL32(58, 66, 86, 255);
constexpr ImU32 kTrack = IM_COL32(15, 17, 24, 255);

// ---- formatting ----
std::string fmtInt(std::uint64_t v);                   // 1,234,567
std::string fmtNum(double v, int decimals);            // 1,234.5
std::string fmtDuration(double us);                    // "52 µs", "0.31 ms"; never "0"
std::string fmtMs(double us);                          // always milliseconds: "0.025 ms", "6.56 ms"; never "0"

// ---- glossary: plain definitions taken from README.md and docs/ ----
namespace gloss {
extern const char* const ray;
extern const char* const target;
extern const char* const crossing;
extern const char* const wallTest;
extern const char* const square;
extern const char* const outlinePoints;
extern const char* const sortCast;
extern const char* const editCost;
extern const char* const compare;
extern const char* const frame;
extern const char* const query;
extern const char* const walls;
extern const char* const cellsWalked;
extern const char* const registrations;
extern const char* const tMinus;
extern const char* const tPlus;
extern const char* const meter;
extern const char* const baseline;
extern const char* const grid;
}  // namespace gloss

// ---- generic ----
void registerWidget(App& app, const char* label, ImVec2 lo, ImVec2 hi);
bool button(App& app, const char* label, ImVec2 size = ImVec2(0, 0));  // registers itself for scripts
void tooltip(const char* text);                  // wrapped tooltip for the item just submitted
void helpMarker(const char* text);               // "(?)" on the same line, hover for the text
ImVec2 textSize(const char* text, float wrap = 0.0f);

// RAII font size change (same UI font, different size).
struct FontScope {
    FontScope(const App& app, float px);
    ~FontScope();
};
struct MonoScope {
    explicit MonoScope(const App& app);
    ~MonoScope();
};

// ---- cards ----
void cardBegin(App& app, const char* title, const char* help = nullptr);
void cardEnd(App& app);
float cardWidth();                                   // usable width inside the current card
void cardText(ImU32 col, const char* fmt, ...);      // wrapped to the card, in the current font
void cardTextSmall(const App& app, ImU32 col, const char* fmt, ...);

// ---- controls ----
// Equal-width choices in `cols` columns. Returns true when the selection changed.
bool segmented(App& app, const char* const* labels, int n, int cols, int& sel, float height);
// Two method cards side by side. Returns the clicked index or -1.
int methodCards(App& app, const char* const* titles, const char* const* subtitles, const ImU32* accents, int n,
                int selected);
// Name + one-line description row; returns true when clicked.
bool sceneRow(App& app, const char* name, const char* blurb, const char* key, bool selected);

// Two-column table row: label (optionally with tooltip) and a right-aligned monospace value.
// Table as wide as the card content, so right-aligned values stay inside the card padding.
bool beginTable(const char* id, int columns);

void kvRow(App& app, const char* label, const char* value, const char* hint = nullptr, ImU32 swatch = 0);

}  // namespace ui
