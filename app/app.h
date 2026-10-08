#pragma once
// Application state and behaviour. Rendering (render.cpp), the control panel (ui.cpp, meter.cpp,
// details.cpp, widgets.cpp), the guided tour (tour.cpp), the script runner (script.cpp) and the GLFW
// loop (main.cpp) all work on this one struct; none of them touch core/ except through it.
#include <array>
#include <cstdint>
#include <map>
#include <string>

#include <imgui.h>

#include "scene.h"
#include "scenes.h"
#include "visibility.h"

enum class Overlay { Off, Grid, Probe, Rays };
const char* overlayName(Overlay o);  // script / log name: off, grid, probe, rays

enum class Detail { Simple, Engine };
enum class Cursor { Arrow, Hand, Move, Cross };  // what main.cpp should show over the canvas

// Plain-language names of the scene kinds (panel, status line, tour).
struct SceneInfo {
    vis::SceneKind kind;
    const char* name;
    const char* blurb;
    const char* key;  // keyboard shortcut
};
const SceneInfo& sceneInfo(vis::SceneKind k);
const SceneInfo* sceneInfos(int& count);  // in panel order

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

// The numbers behind the work meter, for the light position of the latest query.
//   baselineTests = rays x walls. This is exact arithmetic, not an estimate: in Brute mode
//   core/visibility.cpp tests every wall for every ray (`stats.wallTests += walls.size()` per ray),
//   and tests_brute == rays * wallCount is checked by the interaction script.
//   gridTests     = wall tests the Grid method needs for this same light position. Measured by the
//   timed Grid query when Grid is active (or in Compare); with Baseline active it comes from an extra
//   Grid pass that is never timed (and is skipped when the scene is too heavy for that).
struct WorkMeter {
    enum class GridSource { Measured, ExtraPass, Skipped };
    bool valid = false;
    std::uint64_t rays = 0;
    std::size_t walls = 0;  // including the 4 outer walls
    std::uint64_t baselineTests = 0;
    std::uint64_t gridTests = 0;
    GridSource gridSource = GridSource::Skipped;
    bool gridKnown() const { return gridSource != GridSource::Skipped; }
};

constexpr int kTourSteps = 6;

// The strip below the world holds the legend and the tour card (side by side when they fit, stacked
// otherwise); the world is sized to the height that remains. height == 0: nothing in the strip.
struct StripLayout {
    bool legend = false, tour = false, condensed = false;
    ImVec2 legendPos{0, 0}, legendSize{0, 0}, tourPos{0, 0};
    float tourW = 0.0f, height = 0.0f;
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
    vis::VisibilityResult meterResult;  // untimed Grid pass for the work meter while Baseline is active
    vis::Trace trace;
    WorkMeter meter;
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
    double bruteNsPerTest = 0.0;  // measured cost of one wall test in Baseline (for the slow-scene note)
    double baselineEstMs = 0.0;   // time one Baseline update takes for this scene and light
    bool slowBaseline = false;    // baselineEstMs is above the interactivity budget

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
    bool lightMoved = false;  // the user has dragged the light once: the "Drag me" hint goes away
    Cursor cursor = Cursor::Arrow;
    int probeRay = -1;        // index into trace.rays
    std::string status;
    bool statusIsError = false;
    double statusUntil = 0.0;
    double now = 0.0;
    std::map<std::string, ImVec2> widgets;  // panel button centres, so scripts can click them
    ImFont* uiFont = nullptr;
    ImFont* monoFont = nullptr;
    bool wantQuit = false;

    // ---- panel and tour ----
    Detail detail = Detail::Simple;
    bool panelOverflow = false;  // the panel needs a scroll bar
    float panelScrollMax = 0.0f; // how far it would scroll (px)
    bool tourActive = false;
    int tourStep = 0;            // 0 .. kTourSteps - 1
    float tourCardH = 120.0f;    // measured height of the tour card (px), for the canvas layout
    ImVec2 tourCardMin{0, 0}, tourCardMax{0, 0};  // last drawn rectangle (screen)
    StripLayout strip;           // computed in layout()
    float panelSlack = 0.0f;     // free pixels left at the bottom of the panel (negative: it scrolls)

    // ---- setup / actions ----
    void init();
    void loadScene(vis::SceneKind k);   // Demo or one of the stress scenes, light placed per scene
    void resetDemo() { loadScene(vis::SceneKind::Demo); }
    void clearWalls();
    void setMode(vis::Mode m);
    void toggleMode();
    void setOverlay(Overlay o);
    void cycleOverlay();
    void cancelWall();
    void setLight(vis::Vec2 p);
    void setStatus(const std::string& msg, bool error);
    void markDirty() { dirty = true; }
    void sceneChanged();                // scene edited or replaced: stats and grid info are stale
    void applyGridSettings();           // autoGrid / gridCols / gridRows -> Scene

    // ---- tour (tour.cpp) ----
    void tourStart();                   // from step 1, demo scene, Grid method, nothing overlaid
    void tourGo(int step);              // sets the scene / method / overlay that step wants
    void tourNext();                    // past the last step closes the tour
    void tourBack();
    void tourClose();
    void drawTourCard();

    // ---- per frame ----
    void layout(ImVec2 display);                // panel / canvas rectangles and the world view
    void handleInput(ImGuiIO& io);              // canvas mouse + keys, honouring ImGui capture
    void updateProbe(const ImGuiIO& io);         // after updateVisibility: picks the ray from the drawn trace
    const vis::Trace::RayRecord* probe() const;  // the probe ray, or nullptr if none / not valid for this trace
    std::size_t probeWalls(const vis::Trace::RayRecord& r) const;  // walls tested by that ray
    void updateVisibility();                    // timed query (+ compare, + untimed passes)
    void updateMeter();                         // work meter + slow-scene estimate for the latest query
    void frame(ImGuiIO& io, double time);       // layout, input, visibility; call after NewFrame
    void drawPanel();                           // ui.cpp
    void drawStatusLine();                      // ui.cpp

    // ---- helpers ----
    float panelWidth() const { return 360.0f * uiScale; }
    float statusHeight() const { return 28.0f * uiScale; }
    StripLayout stripLayout() const;            // legend.cpp
    float grabRadius() const;                   // hit radius of the light handle (px)
    std::string field(const std::string& name) const;  // state snapshot for scripts (`log`, `expect`)
    std::string logLine() const;
    std::string overlayCaption() const;         // one live sentence describing what is on screen
    std::string meterVerdict() const;           // meter.cpp: "The grid does 22% of the work here."
    std::string slowNote() const;               // "" when the scene is not slow with Baseline
    void refreshGridInfo();
    int nearestWall(ImVec2 mouse, float maxPixels, bool userOnly) const;  // index or -1
    vis::Vec2 snapped(ImVec2 mouse, bool snap) const;
};

// ui.cpp
void setupUi(App& app, float contentScale);   // fonts + theme

// render.cpp
ImU32 heatColour(float t);  // 0 -> faint blue, 1 -> hot orange (grid-cell tint and its legend swatch)
void drawCanvas(ImDrawList* dl, const App& app);
