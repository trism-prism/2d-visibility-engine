// Milestone 1 smoke app: demo scene, filled visibility polygon, draggable light, tiny stats panel.
//   vis_app                               interactive window
//   vis_app --capture out.bmp [--frames N]  render N frames (default 3), save the framebuffer, exit 0
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "bmp.h"
#include "render.h"
#include "scenes.h"
#include "visibility.h"

namespace {

struct Options {
    std::string capturePath;  // empty: interactive
    int frames = 3;
};

bool parseArgs(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--capture" && i + 1 < argc) {
            opt.capturePath = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            opt.frames = std::max(1, std::atoi(argv[++i]));
        } else {
            std::fprintf(stderr, "usage: vis_app [--capture <file.bmp> [--frames N]]\n");
            return false;
        }
    }
    return true;
}

void onGlfwError(int code, const char* message) { std::fprintf(stderr, "GLFW error %d: %s\n", code, message); }

// Reads the (resolved) back buffer and writes it as BMP; prints how much of it differs from the clear colour.
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
    std::size_t differing = 0;
    for (std::size_t i = 0; i < rgb.size(); i += 3)
        if (std::memcmp(&rgb[i], &rgb[0], 3) != 0) ++differing;
    std::printf("captured %dx%d, %zu of %zu pixels differ from the corner pixel\n", w, h, differing,
                static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    if (!writeBmp24(path, w, h, rgb)) {
        std::fprintf(stderr, "capture: cannot write %s\n", path.c_str());
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) return 2;
    const bool capture = !opt.capturePath.empty();

    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_VISIBLE, capture ? GLFW_FALSE : GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "2D Visibility & Lighting Engine", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(capture ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;  // no imgui.ini written into the working directory
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    vis::Scene scene = vis::makeDemoScene();
    vis::Vec2 light = vis::demoLight();
    vis::VisibilityResult result;
    int exitCode = 0;
    int frame = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuiIO& io = ImGui::GetIO();

        const View view = fitView(scene, io.DisplaySize, 24.0f);
        if (!capture && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.WantCaptureMouse) {
            light = view.toWorld(io.MousePos);
        }

        vis::computeVisibility(scene, light, vis::Mode::Grid, result);
        drawWorld(ImGui::GetBackgroundDrawList(), view, scene, result);

        ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.7f);
        ImGui::Begin("stats", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("frame  %.2f ms (%.0f fps)", static_cast<double>(io.DeltaTime) * 1000.0, static_cast<double>(io.Framerate));
        ImGui::Text("query  %.1f us (sort %.1f, cast %.1f)", result.stats.totalUs, result.stats.sortUs, result.stats.castUs);
        ImGui::Text("rays %llu, wall tests %llu, cells %llu, vertices %zu",
                    static_cast<unsigned long long>(result.stats.raysCast),
                    static_cast<unsigned long long>(result.stats.wallTests),
                    static_cast<unsigned long long>(result.stats.cellsVisited), result.stats.outputVertices);
        ImGui::Text("walls %zu, targets %zu  (drag to move the light)", scene.userWallCount(), scene.targets().size());
        ImGui::End();

        ImGui::Render();
        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.055f, 0.06f, 0.075f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ++frame;
        if (capture && frame >= opt.frames) {
            if (!captureFramebuffer(window, opt.capturePath)) exitCode = 1;
            break;
        }
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
