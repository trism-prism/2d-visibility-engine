#pragma once
// Application state and behaviour. Rendering (render.cpp), the control panel (ui.cpp), the script
// runner (script.cpp) and the GLFW loop (main.cpp) all work on this one struct; none of them touch
// core/ except through it.
#include <array>
#include <cstdint>
#include <map>
#include <string>

#include <imgui.h>

#include "scene.h"
#include "scenes.h"
#include "visibility.h"

enum class Overlay { Off, Grid, Probe, Rays };
const char* overlayName(Overlay o);

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

// Median of the last 60 samples: robust against the occasional scheduler hiccup, and every sample
// is a real measurement of a real query.
struct Stat {
    std::array<double, 60> v{};
    int n = 0;
    int head = 0;
    void add(double x) {
        v[static_cast<std::size_t>(head)] = x;
        head = (head + 1) % 60;
        if (n < 60) ++n;
    }
    void clear() { n = 0; head = 0; }
    double median() const;
};

struct ModeStats {
    Stat total, sort, cast;
    vis::VisibilityStats last;  // counters of the most recent query in this mode
    bool valid = false;
    void clear() { total.clear(); sort.clear(); cast.clear(); valid = false; }
};

struct App {
    // ---- model ----
    vis::Scene scene{vis::kStressWidth, vis::kStressHeight};
    vis::Vec2 light{};
    vis::Mode mode = vis::Mode::Grid;
    Overlay overlay = Overlay::Off;
    bool compare = false;
    bool outline = false;
    bool vsync = true;
    vis::SceneKind kind = vis::SceneKind::Demo;
    int stressWalls = 1000;
    int seed = 1;
    bool autoGrid = true;
    int gridCols = 10, gridRows = 6;

    // ---- results ----
    vis::VisibilityResult result;       // timed query, current mode
    vis::VisibilityResult otherResult;  // timed query, the other mode (Compare only)
    vis::VisibilityResult traceResult;  // untimed pass that fills `trace`
    vis::Trace trace;
    bool haveOther = false;
    bool compareFlip = false;  // alternates which mode runs first in Compare
    bool identical = true;
    ModeStats stats[2];  // indexed by static_cast<int>(vis::Mode)
    double frameMs = 16.0;  // exponential moving average
    std::uint64_t polyHash = 0;
    std::size_t maxCellWalls = 0;
    bool dirty = true;
    std::uint64_t epoch = 0, infoEpoch = ~0ull;  // epoch changes whenever the scene object changes
    int settle = 0;  // extra re-queries after a change, to collect more timing samples

    // ---- interaction ----
    ImVec2 displaySize{1280, 800};
    float uiScale = 1.0f;
    ImVec2 canvasMin{0, 0}, canvasMax{960, 780};
    View view;
    bool draggingLight = false;
    bool drawing = false;
    vis::Vec2 drawStart{}, drawEnd{};
    int hoverWall = -1;       // user wall a right-click would delete
    bool hoverBoundary = false;
    bool hoverLight = false;
    int probeRay = -1;        // index into trace.rays
    std::string status;
    bool statusIsError = false;
    double statusUntil = 0.0;
    double now = 0.0;
    std::map<std::string, ImVec2> widgets;  // panel button centres, so scripts can click them
    ImFont* monoFont = nullptr;
    bool wantQuit = false;

    // ---- setup / actions ----
    void init();
    void loadScene(vis::SceneKind k);   // Demo or one of the stress scenes, light placed per scene
    void resetDemo() { loadScene(vis::SceneKind::Demo); }
    void clearWalls();
    void toggleMode();
    void cycleOverlay();
    void cancelWall();
    void setLight(vis::Vec2 p);
    void setStatus(const std::string& msg, bool error);
    void markDirty() { dirty = true; }
    void sceneChanged();                // scene edited or replaced: stats and grid info are stale
    void applyGridSettings();           // autoGrid / gridCols / gridRows -> Scene

    // ---- per frame ----
    void layout(ImVec2 display);                // panel / canvas rectangles and the world view
    void handleInput(ImGuiIO& io);              // canvas mouse + keys, honouring ImGui capture
    void updateProbe(const ImGuiIO& io);         // after updateVisibility: picks the ray from the drawn trace
    const vis::Trace::RayRecord* probe() const;  // the probe ray, or nullptr if none / not valid for this trace
    std::size_t probeWalls(const vis::Trace::RayRecord& r) const;  // walls tested by that ray
    void updateVisibility();                    // timed query (+ compare, + untimed trace)
    void frame(ImGuiIO& io, double time);       // layout, input, visibility; call after NewFrame
    void drawPanel();                           // ui.cpp
    void drawStatusLine();                      // ui.cpp

    // ---- helpers ----
    float panelWidth() const { return 330.0f * uiScale; }
    float statusHeight() const { return 28.0f * uiScale; }
    std::string field(const std::string& name) const;  // state snapshot for scripts (`log`, `expect`)
    std::string logLine() const;
    void refreshGridInfo();
    int nearestWall(ImVec2 mouse, float maxPixels, bool userOnly) const;  // index or -1
    vis::Vec2 snapped(ImVec2 mouse, bool snap) const;
};

// ui.cpp
void setupUi(App& app, float contentScale);   // fonts + theme

// render.cpp
void drawCanvas(ImDrawList* dl, const App& app);
