#include "script.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

ImGuiKey keyFromName(const std::string& n) {
    if (n == "Tab") return ImGuiKey_Tab;
    if (n == "R") return ImGuiKey_R;
    if (n == "C") return ImGuiKey_C;
    if (n == "O") return ImGuiKey_O;
    if (n == "M") return ImGuiKey_M;
    if (n == "1") return ImGuiKey_1;
    if (n == "2") return ImGuiKey_2;
    if (n == "3") return ImGuiKey_3;
    if (n == "4") return ImGuiKey_4;
    if (n == "Escape") return ImGuiKey_Escape;
    return ImGuiKey_None;
}

bool parseNumber(const std::string& s, double& v) {
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

}  // namespace

bool Script::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ss(line);
        Cmd c;
        c.line = n;
        for (std::string w; ss >> w;) c.w.push_back(w);
        if (c.w.empty()) continue;
        // "move @Reset demo" : everything after @ is a (possibly multi-word) label
        if (c.w[0] == "move" && c.w.size() > 1 && c.w[1][0] == '@') {
            std::string label = c.w[1].substr(1);
            for (std::size_t i = 2; i < c.w.size(); ++i) label += " " + c.w[i];
            c.w = {"move", "@" + label};
        }
        if (c.w[0] == "expect" && c.w.size() > 4) {  // value with spaces, e.g. a status text
            std::string v = c.w[3];
            for (std::size_t i = 4; i < c.w.size(); ++i) v += " " + c.w[i];
            c.w.resize(3);
            c.w.push_back(v);
        }
        cmds_.push_back(c);
    }
    return true;
}

void Script::expect(App& app, const Cmd& c) {
    if (c.w.size() < 4) {
        std::printf("EXPECT FAIL line %d: malformed\n", c.line);
        ++failures;
        return;
    }
    const std::string actual = app.field(c.w[1]);
    std::string want = c.w[3];
    if (!want.empty() && want[0] == '$') {
        const std::string name = want.substr(1);
        want = "<undefined>";
        for (const auto& kv : vars_)
            if (kv.first == name) want = kv.second;
    }
    const std::string& op = c.w[2];
    bool ok = false;
    double a = 0, b = 0;
    const bool numeric = parseNumber(actual, a) && parseNumber(want, b);
    if (op == "==") ok = numeric ? a == b : actual == want;
    else if (op == "!=") ok = numeric ? a != b : actual != want;
    else if (op == "<") ok = numeric && a < b;
    else if (op == "<=") ok = numeric && a <= b;
    else if (op == ">") ok = numeric && a > b;
    else if (op == ">=") ok = numeric && a >= b;
    else if (op == "has") ok = actual.find(want) != std::string::npos;
    std::printf("%s line %d: %s %s %s (actual: %s)\n", ok ? "expect ok  " : "EXPECT FAIL", c.line, c.w[1].c_str(),
                op.c_str(), want.c_str(), actual.c_str());
    if (!ok) ++failures;
}

bool Script::tick(App& app, ImGuiIO& io) {
    if (releaseKey_ != ImGuiKey_None) {
        io.AddKeyEvent(releaseKey_, false);
        releaseKey_ = ImGuiKey_None;
    }
    if (wait_ > 0) {
        --wait_;
        return true;
    }
    while (next_ < cmds_.size()) {
        const Cmd& c = cmds_[next_++];
        const std::string& op = c.w[0];
        const auto arg = [&](std::size_t i) { return i < c.w.size() ? c.w[i] : std::string(); };
        if (op == "move") {
            if (arg(1)[0] == '@') {
                const auto it = app.widgets.find(arg(1).substr(1));
                if (it == app.widgets.end()) {
                    std::printf("EXPECT FAIL line %d: unknown button %s\n", c.line, arg(1).c_str());
                    ++failures;
                    continue;
                }
                io.AddMousePosEvent(it->second.x, it->second.y);
            } else {
                io.AddMousePosEvent(static_cast<float>(std::atof(arg(1).c_str())), static_cast<float>(std::atof(arg(2).c_str())));
            }
            wait_ = 2;
            return true;
        }
        if (op == "wmove") {
            const ImVec2 p = app.view.toScreen({std::atof(arg(1).c_str()), std::atof(arg(2).c_str())});
            io.AddMousePosEvent(p.x, p.y);
            wait_ = 2;
            return true;
        }
        if (op == "down" || op == "up") {
            io.AddMouseButtonEvent(arg(1) == "right" ? 1 : 0, op == "down");
            wait_ = 2;
            return true;
        }
        if (op == "key") {
            const ImGuiKey k = keyFromName(arg(1));
            if (k == ImGuiKey_None) {
                std::printf("EXPECT FAIL line %d: unknown key %s\n", c.line, arg(1).c_str());
                ++failures;
                continue;
            }
            io.AddKeyEvent(k, true);
            releaseKey_ = k;  // released on the next tick
            wait_ = 3;
            return true;
        }
        if (op == "wait") {
            wait_ = std::atoi(arg(1).c_str());
            return true;
        }
        if (op == "capture") {
            pendingCapture = arg(1);
            wait_ = 1;
            return true;
        }
        if (op == "log") {
            std::printf("log line %d: %s\n", c.line, app.logLine().c_str());
            std::fflush(stdout);
            continue;
        }
        if (op == "set") {
            const std::string k = arg(1), v = arg(2);
            if (k == "walls") app.stressWalls = std::atoi(v.c_str());
            else if (k == "seed") app.seed = std::atoi(v.c_str());
            else if (k == "compare") { app.compare = v == "on"; app.markDirty(); }
            else if (k == "mode") { app.mode = v == "brute" ? vis::Mode::Brute : vis::Mode::Grid; app.markDirty(); }
            else if (k == "overlay") {
                app.overlay = v == "grid" ? Overlay::Grid : v == "probe" ? Overlay::Probe : v == "rays" ? Overlay::Rays : Overlay::Off;
                app.markDirty();
            }
            continue;
        }
        if (op == "print") {  // print FIELD... : one line of "field=value"
            std::string out;
            for (std::size_t i = 1; i < c.w.size(); ++i) out += (i > 1 ? " " : "") + c.w[i] + "=" + app.field(c.w[i]);
            std::printf("print line %d: %s\n", c.line, out.c_str());
            std::fflush(stdout);
            continue;
        }
        if (op == "save") {
            vars_.emplace_back(arg(1), app.field(arg(2)));
            continue;
        }
        if (op == "expect") {
            expect(app, c);
            continue;
        }
        if (op == "quit") return false;
        std::printf("EXPECT FAIL line %d: unknown command %s\n", c.line, op.c_str());
        ++failures;
    }
    return false;
}
