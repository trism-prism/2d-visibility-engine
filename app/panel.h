#pragma once
// Panel sections that live in their own files. All are called from App::drawPanel (ui.cpp).
#include "app.h"

void drawWorkMeter(App& app);      // meter.cpp: the two-bar "how much work" card (both detail levels)
void drawProbeNumbers(App& app);   // details.cpp: probe-ray numbers (Engine details only)
void drawEngineDetails(App& app);  // details.cpp: dashboard, compare, grid, keys (Engine details only)
