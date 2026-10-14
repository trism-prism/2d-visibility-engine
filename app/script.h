#pragma once
// Plain-text event scripts replayed through ImGui's input queue (io.AddMousePosEvent /
// AddMouseButtonEvent / AddKeyEvent), so the real canvas input handling, ImGui capture rules and
// capture/overlay code run exactly as they do for a user.
//
// NOT covered: delivery of real OS mouse/keyboard events through GLFW's callbacks into ImGui's
// backend; scripts inject events one layer above that.
//
// One command per line, '#' starts a comment:
//   move X Y | move @Button Label | wmove X Y   mouse position: window pixels / panel button centre /
//                                                world coordinates (through the app's real transform)
//   down left|right, up left|right               mouse buttons
//   key Tab|R|C|O|M|1|2|3|4|Escape               press and release
//   shift on|off                                 hold / release Shift (snap while drawing)
//   wait N                                       N frames
//   capture file.bmp                             save the framebuffer of the current frame
//   log                                          print one state line
//   print FIELD...                               print the named state fields as field=value
//   set walls N | seed N | compare on|off | mode brute|grid | overlay off|grid|probe|rays
//       | detail simple|details | tour on|off
//   save NAME FIELD                              remember a state field
//   expect FIELD OP VALUE                        OP: == != < <= > >= has; VALUE may be $NAME
//   quit
#include <string>
#include <vector>

#include <imgui.h>

#include "app.h"

class Script {
public:
    bool load(const std::string& path);
    // Call once per frame before ImGui::NewFrame(). Returns false when the script has finished.
    bool tick(App& app, ImGuiIO& io);
    std::string pendingCapture;  // set by `capture`; main saves it after rendering this frame, then clears
    int failures = 0;

private:
    struct Cmd {
        std::vector<std::string> w;
        int line = 0;
    };
    std::vector<Cmd> cmds_;
    std::size_t next_ = 0;
    int wait_ = 0;
    ImGuiKey releaseKey_ = ImGuiKey_None;
    std::vector<std::pair<std::string, std::string>> vars_;
    void expect(App& app, const Cmd& c);
};
