// Interactive 2D visibility / lighting application (GLFW + Dear ImGui + OpenGL3).
//   vis_app [--size WxH] [--scene demo|scatter|maze|long|cluster] [--walls N] [--seed S]
//           [--mode brute|grid] [--overlay off|grid|probe|rays] [--light x,y] [--compare]
//           [--capture file.bmp [--frames N]] [--script file]
// Capture and script runs use a hidden window and no vsync.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "app.h"
#include "bmp.h"
#include "script.h"

namespace {

struct Options {
    int width = 1280, height = 800;
    vis::SceneKind scene = vis::SceneKind::Demo;
    int walls = 1000, seed = 1;
    vis::Mode mode = vis::Mode::Grid;
    Overlay overlay = Overlay::Off;
    bool hasLight = false;
    double lightX = 0, lightY = 0;
    bool compare = false;
    std::string capturePath, scriptPath;
    int frames = 3;
};

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--size") {
            if (std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height) != 2) return false;
        } else if (a == "--scene") {
            const std::string s = next();
            if (s == "demo") o.scene = vis::SceneKind::Demo;
            else if (s == "scatter") o.scene = vis::SceneKind::Scatter;
            else if (s == "maze") o.scene = vis::SceneKind::Maze;
            else if (s == "long") o.scene = vis::SceneKind::LongWalls;
            else if (s == "cluster") o.scene = vis::SceneKind::Cluster;
            else return false;
        } else if (a == "--walls") o.walls = std::clamp(std::atoi(next().c_str()), 100, 5000);
        else if (a == "--seed") o.seed = std::max(0, std::atoi(next().c_str()));
        else if (a == "--mode") {
            const std::string s = next();
            if (s == "brute") o.mode = vis::Mode::Brute;
            else if (s == "grid") o.mode = vis::Mode::Grid;
            else return false;
        } else if (a == "--overlay") {
            const std::string s = next();
            if (s == "off") o.overlay = Overlay::Off;
            else if (s == "grid") o.overlay = Overlay::Grid;
            else if (s == "probe") o.overlay = Overlay::Probe;
            else if (s == "rays") o.overlay = Overlay::Rays;
            else return false;
        } else if (a == "--light") {
            if (std::sscanf(next().c_str(), "%lf,%lf", &o.lightX, &o.lightY) != 2) return false;
            o.hasLight = true;
        } else if (a == "--compare") o.compare = true;
        else if (a == "--capture") o.capturePath = next();
        else if (a == "--frames") o.frames = std::max(1, std::atoi(next().c_str()));
        else if (a == "--script") o.scriptPath = next();
        else return false;
    }
    o.width = std::clamp(o.width, 400, 7680);
    o.height = std::clamp(o.height, 300, 4320);
    return true;
}

void onGlfwError(int code, const char* message) { std::fprintf(stderr, "GLFW error %d: %s\n", code, message); }

// Reads the resolved back buffer of the frame just rendered and writes a 24-bit BMP.
bool captureFramebuffer(GLFWwindow* window, const std::string& path) {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    if (w <= 0 || h <= 0) {
        std::fprintf(stderr, "capture: framebuffer is %dx%d\n", w, h);
        return false;
    }
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    if (glGetError() != GL_NO_ERROR) {
        std::fprintf(stderr, "capture: glReadPixels failed\n");
        return false;
    }
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    if (!writeBmp24(path, w, h, rgb)) {
        std::fprintf(stderr, "capture: cannot write %s\n", path.c_str());
        return false;
    }
    std::printf("captured %s (%dx%d)\n", path.c_str(), w, h);
    std::fflush(stdout);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        std::fprintf(stderr,
                     "usage: vis_app [--size WxH] [--scene demo|scatter|maze|long|cluster] [--walls N] [--seed S]\n"
                     "               [--mode brute|grid] [--overlay off|grid|probe|rays] [--light x,y] [--compare]\n"
                     "               [--capture file.bmp [--frames N]] [--script file]\n");
        return 2;
    }
    const bool headless = !opt.capturePath.empty() || !opt.scriptPath.empty();

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_VISIBLE, headless ? GLFW_FALSE : GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(opt.width, opt.height, "2D Visibility & Lighting Engine", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwSetWindowSizeLimits(window, 900, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(headless ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // no imgui.ini in the working directory
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(window, &sx, &sy);
    App app;
    setupUi(app, headless ? 1.0f : std::max(sx, 1.0f));
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    app.vsync = !headless;
    app.stressWalls = opt.walls;
    app.seed = opt.seed;
    app.mode = opt.mode;
    app.overlay = opt.overlay;
    app.compare = opt.compare;
    app.loadScene(opt.scene);
    if (opt.hasLight) app.setLight({opt.lightX, opt.lightY});
    app.status.clear();

    Script script;
    const bool scripted = !opt.scriptPath.empty();
    if (scripted && !script.load(opt.scriptPath)) {
        std::fprintf(stderr, "cannot read script %s\n", opt.scriptPath.c_str());
        return 2;
    }

    int exitCode = 0;
    int frame = 0;
    while (!glfwWindowShouldClose(window) && !app.wantQuit) {
        glfwPollEvents();
        if (scripted && !script.tick(app, io)) app.wantQuit = true;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.frame(io, ImGui::GetTime());
        drawCanvas(ImGui::GetBackgroundDrawList(), app);
        app.drawPanel();
        app.drawStatusLine();
        ImGui::Render();

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.047f, 0.051f, 0.067f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ++frame;
        if (!script.pendingCapture.empty()) {
            if (!captureFramebuffer(window, script.pendingCapture)) exitCode = 1;
            script.pendingCapture.clear();
        }
        if (!opt.capturePath.empty() && !scripted && frame >= opt.frames) {
            if (!captureFramebuffer(window, opt.capturePath)) exitCode = 1;
            break;
        }
        glfwSwapBuffers(window);
    }

    if (scripted && script.failures > 0) {
        std::printf("SCRIPT FAILED: %d failing expectation(s)\n", script.failures);
        exitCode = 1;
    } else if (scripted) {
        std::printf("script ok\n");
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
