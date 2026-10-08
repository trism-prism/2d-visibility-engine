#pragma once
// Canvas legend box and the info strip below the world that holds it (and the tour card).
#include <string>
#include <vector>

#include "app.h"

enum class Swatch { None, Line, Square, Dot, DotPair, Heat };
struct LegendRow {
    Swatch sw;
    ImU32 col;
    std::string text;
    bool dim = false;  // drawn faded: the row does not apply to what is on screen right now
};
struct LegendModel {
    std::string title;
    std::vector<LegendRow> rows;
};

// Same plain wording as the panel caption; swatches match what is drawn. `condensed` keeps only the
// caption (narrow windows). The set of rows and their text depend on overlay and method only, never
// on the probe ray, so the strip height does not change while the mouse moves. The two-colour row is
// the one exception in looks only: it is dimmed while the probed ray's two sides end at the same wall.
LegendModel buildLegend(const App& app, bool condensed);
ImVec2 legendSize(const App& app, const LegendModel& m, float width);
void drawLegend(ImDrawList* dl, const App& app, const LegendModel& m, ImVec2 pos, float width);
