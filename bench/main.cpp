// vis_bench: Release measurement of Baseline (brute force) vs Grid and of edit costs.
//   vis_bench --out <results.csv> [--quick] [--budget SECONDS] [--samples N]
// Writes <results.csv>, grid_sweep.csv next to it and <results>.meta.txt. See DESIGN section 7, check 8.
//
// Method, per row (scene kind, n, seed):
//   * the scene is built once and the light path (P positions on a closed Lissajous-like curve) is
//     computed once; both modes use exactly the same positions in the same order;
//   * correctness gate: Brute and Grid polygons must be bitwise equal at every position used, else the
//     row is not written and the program exits non-zero;
//   * timing: steady_clock around the loop of computeVisibility calls only, one sample = one pass over
//     the positions, reported per query; warm-up passes first; execution order alternates between
//     samples; the polygon size and wall-test count of every timed query are accumulated into a sink
//     that is printed at the end (so the queries cannot be optimised away), and the CSV column
//     `checksum` is the vertex checksum of the gate pass;
//   * Brute can be very slow on big scenes: a per-row time budget reduces its samples (minimum 3) and,
//     if one pass alone is too long, the number of positions (both modes then use the same subset).
//     The positions and sample counts actually used are recorded in the CSV.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "bench_config.h"
#include "scenes.h"
#include "visibility.h"

using namespace vis;
using Clock = std::chrono::steady_clock;

namespace {

struct Config {
    bool quick = false;
    double budget = 10.0;  // seconds per mode per row, for the timed samples
    int samples = 15;
    int positions = 64;
    int editReps = 15;
};

double seconds(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b - a).count(); }
double micros(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::micro>(b - a).count(); }

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}
// With at most 15 samples a nearest-rank 95th percentile is the maximum, so the maximum and the
// minimum are reported instead.
double minOf(const std::vector<double>& v) { return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end()); }
double maxOf(const std::vector<double>& v) { return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()); }

std::vector<Vec2> lightPath(const Scene& s, int count) {
    std::vector<Vec2> path;
    const double twoPi = 6.283185307179586;
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / count;
        path.push_back({s.width() * (0.5 + 0.42 * std::sin(twoPi * 3.0 * t + 0.6)),
                        s.height() * (0.5 + 0.42 * std::sin(twoPi * 2.0 * t))});
    }
    return path;
}

struct PassResult {
    double perQueryUs = 0, sortUs = 0, castUs = 0, tests = 0, cells = 0, rays = 0, vertices = 0;
};

std::uint64_t g_sink = 0;  // consumed outputs: keeps the compiler from discarding the queries

PassResult runPass(const Scene& scene, const std::vector<Vec2>& pos, const std::vector<int>& subset, Mode mode,
                   VisibilityResult& r) {
    PassResult out;
    const auto t0 = Clock::now();
    for (int idx : subset) {
        computeVisibility(scene, pos[static_cast<std::size_t>(idx)], mode, r);
        out.sortUs += r.stats.sortUs;
        out.castUs += r.stats.castUs;
        out.tests += static_cast<double>(r.stats.wallTests);
        out.cells += static_cast<double>(r.stats.cellsVisited);
        out.rays += static_cast<double>(r.stats.raysCast);
        out.vertices += static_cast<double>(r.polygon.size());
        g_sink += r.polygon.size() + r.stats.wallTests;
    }
    const auto t1 = Clock::now();
    const double q = static_cast<double>(subset.size());
    out.perQueryUs = micros(t0, t1) / q;
    out.sortUs /= q; out.castUs /= q; out.tests /= q; out.cells /= q; out.rays /= q; out.vertices /= q;
    return out;
}

double vertexChecksum(const std::vector<Vec2>& poly) {
    double s = 0;
    for (Vec2 v : poly) s += v.x * 31.0 + v.y;
    return s;
}

struct Meta {
    std::vector<std::pair<std::string, std::string>> items;
};

std::string cpuBrand() {
#if defined(_MSC_VER)
    int regs[4];
    __cpuid(regs, static_cast<int>(0x80000000));
    if (static_cast<unsigned>(regs[0]) < 0x80000004u) return "unknown";
    char brand[49] = {};
    for (unsigned i = 0; i < 3; ++i) {
        __cpuid(regs, static_cast<int>(0x80000002u + i));
        std::copy_n(reinterpret_cast<char*>(regs), 16, brand + 16 * i);
    }
    std::string s(brand);
    const auto b = s.find_first_not_of(' ');
    return b == std::string::npos ? s : s.substr(b);
#else
    return "unknown";
#endif
}

std::string osVersion() {
#if defined(_WIN32)
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE h = GetModuleHandleW(L"ntdll.dll")) {
        if (FARPROC p = GetProcAddress(h, "RtlGetVersion")) {
            RTL_OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof vi;
            if (reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(p))(&vi) == 0)
                return "Windows " + std::to_string(vi.dwMajorVersion) + "." + std::to_string(vi.dwMinorVersion) + " build " +
                       std::to_string(vi.dwBuildNumber);
        }
    }
#endif
    return "unknown";
}

std::string compilerId() {
#if defined(_MSC_VER)
    return "MSVC _MSC_FULL_VER " + std::to_string(_MSC_FULL_VER);
#elif defined(__VERSION__)
    return std::string("GNU-compatible ") + __VERSION__;
#else
    return "unknown";
#endif
}

Meta makeMeta(const Config& cfg) {
    Meta m;
    char when[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S %z", std::localtime(&now));
    m.items = {
        {"date", when},
        {"compiler", compilerId()},
        {"build_type", VIS_BENCH_BUILD_TYPE},
        {"flags", VIS_BENCH_FLAGS},
        {"cpu", cpuBrand()},
        {"logical_cores", std::to_string(std::thread::hardware_concurrency())},
        {"os", osVersion()},
        {"mode", cfg.quick ? "quick" : "full"},
        {"positions_per_pass", std::to_string(cfg.positions)},
        {"samples_per_mode", std::to_string(cfg.samples)},
        {"time_budget_s_per_mode", std::to_string(cfg.budget)},
        {"world", std::to_string(static_cast<int>(kStressWidth)) + "x" + std::to_string(static_cast<int>(kStressHeight))},
        {"threads", "1 (query and rebuild are single-threaded)"},
    };
    return m;
}

const char* kHeader =
    "kind,n_requested,walls_inserted,seed,grid_scale,cols,rows,registrations,max_walls_per_cell,k,T,"
    "positions_used,positions_total,brute_samples,grid_samples,rays_mean,vertices_mean,"
    "brute_med_us,brute_min_us,brute_max_us,brute_sort_med_us,brute_cast_med_us,brute_tests_mean,"
    "grid_med_us,grid_min_us,grid_max_us,grid_sort_med_us,grid_cast_med_us,grid_tests_mean,grid_cells_mean,"
    "speedup_total,speedup_cast,rebuild_grid_med_us,rebuild_isect_med_us,addremove_pair_med_us,checksum";

struct Writer {
    std::ofstream f;
    bool open(const std::filesystem::path& path, const Meta& meta) {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        f.open(path);
        if (!f) return false;
        for (const auto& kv : meta.items) f << "# " << kv.first << ": " << kv.second << "\n";
        f << kHeader << "\n";
        return true;
    }
};

// Measures one row. Returns false (and writes nothing) if the Brute/Grid gate fails.
bool measureRow(Scene& scene, SceneKind kind, int nRequested, int seed, const std::string& scaleLabel, const Config& cfg,
                Writer& out) {
    const std::vector<Vec2> path = lightPath(scene, cfg.positions);
    VisibilityResult rb, rg;

    // Pick the positions: Brute governs how many fit in the budget. The per-query time is estimated
    // from up to five queries spread along the path (the first one also warms the caches); when a
    // single Brute query already exceeds a third of the budget the remaining probes are skipped,
    // because only one position will be used anyway.
    double tBrute = 0, tGrid = 0;
    int probes = 0;
    for (int pi = 0; pi < 5; ++pi) {
        const std::size_t idx = static_cast<std::size_t>(pi * cfg.positions / 5);
        auto t0 = Clock::now();
        computeVisibility(scene, path[idx], Mode::Brute, rb);
        const double b = seconds(t0, Clock::now());
        t0 = Clock::now();
        computeVisibility(scene, path[idx], Mode::Grid, rg);
        const double g = seconds(t0, Clock::now());
        tBrute += b;
        tGrid += g;
        ++probes;
        if (b > cfg.budget / 3.0) break;
    }
    tBrute /= probes;
    tGrid /= probes;
    int m = static_cast<int>(std::clamp(cfg.budget / (3.0 * std::max(tBrute, 1e-9)), 1.0, static_cast<double>(cfg.positions)));
    std::vector<int> subset;
    for (int i = 0; i < m; ++i) subset.push_back(static_cast<int>(static_cast<long long>(i) * cfg.positions / m));

    // Gate (also the first warm-up pass for both modes).
    double checksum = 0;
    for (int idx : subset) {
        computeVisibility(scene, path[static_cast<std::size_t>(idx)], Mode::Brute, rb);
        computeVisibility(scene, path[static_cast<std::size_t>(idx)], Mode::Grid, rg);
        if (rb.polygon != rg.polygon) {
            std::fprintf(stderr, "CORRECTNESS GATE FAILED: kind=%s n=%d seed=%d position=%d light=(%.17g,%.17g) brute=%zu grid=%zu vertices\n",
                         sceneKindName(kind), nRequested, seed, idx, path[static_cast<std::size_t>(idx)].x,
                         path[static_cast<std::size_t>(idx)].y, rb.polygon.size(), rg.polygon.size());
            return false;
        }
        checksum += vertexChecksum(rg.polygon);
    }
    // Extra warm-up pass when it is cheap.
    if (tBrute * m < cfg.budget / 10.0) runPass(scene, path, subset, Mode::Brute, rb);
    if (tGrid * m < cfg.budget / 10.0) runPass(scene, path, subset, Mode::Grid, rg);

    const int sb = static_cast<int>(std::clamp(cfg.budget / (std::max(tBrute, 1e-9) * m), 3.0, static_cast<double>(cfg.samples)));
    const int sg = static_cast<int>(std::clamp(cfg.budget / (std::max(tGrid, 1e-9) * m), 3.0, static_cast<double>(cfg.samples)));
    std::vector<double> bt, bs, bc, gt, gs, gc;
    PassResult lastB, lastG;
    for (int i = 0; i < std::max(sb, sg); ++i) {
        const auto doBrute = [&] {
            if (i >= sb) return;
            lastB = runPass(scene, path, subset, Mode::Brute, rb);
            bt.push_back(lastB.perQueryUs); bs.push_back(lastB.sortUs); bc.push_back(lastB.castUs);
        };
        const auto doGrid = [&] {
            if (i >= sg) return;
            lastG = runPass(scene, path, subset, Mode::Grid, rg);
            gt.push_back(lastG.perQueryUs); gs.push_back(lastG.sortUs); gc.push_back(lastG.castUs);
        };
        if (i % 2 == 0) { doBrute(); doGrid(); }
        else { doGrid(); doBrute(); }
    }

    // Edit costs on the loaded scene.
    std::vector<Segment> userWalls(scene.walls().begin() + Scene::kBoundaryWalls, scene.walls().end());
    std::vector<double> rebuildGrid, rebuildIsect, pair;
    int reps = cfg.editReps;
    {
        const auto e0 = Clock::now();
        scene.setWalls(userWalls);
        const double one = seconds(e0, Clock::now());
        reps = static_cast<int>(std::clamp(cfg.budget / std::max(one, 1e-9), 3.0, static_cast<double>(cfg.editReps)));
    }
    for (int i = 0; i < reps; ++i) {
        scene.setWalls(userWalls);
        rebuildGrid.push_back(scene.lastRebuild().gridBuildUs);
        rebuildIsect.push_back(scene.lastRebuild().intersectUs);
    }
    for (int i = 0; i < reps; ++i) {
        const auto e0 = Clock::now();
        const AddStatus st = scene.addWall({scene.width() * 0.5, scene.height() * 0.5}, {scene.width() * 0.5 + 20, scene.height() * 0.5 + 6});
        scene.removeWall(scene.wallCount() - 1);
        pair.push_back(micros(e0, Clock::now()));
        if (st != AddStatus::Added) std::fprintf(stderr, "warning: probe wall rejected\n");
    }

    const Grid& g = scene.grid();
    std::size_t maxCell = 0;
    for (std::size_t c = 0; c < g.cellCount(); ++c) maxCell = std::max(maxCell, g.cellWalls(c).size());
    const double bm = median(bt), gm = median(gt), bcm = median(bc), gcm = median(gc);

    char line[2048];
    std::snprintf(line, sizeof line,
                  "%s,%d,%zu,%d,%s,%d,%d,%zu,%zu,%zu,%zu,%zu,%d,%zu,%zu,%.1f,%.1f,"
                  "%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,%.2f,%.2f,%.1f,%.1f,%.1f,%.6g",
                  sceneKindName(kind), nRequested, scene.userWallCount(), seed, scaleLabel.c_str(), g.cols(), g.rows(),
                  g.registrations(), maxCell, scene.intersections().size(), scene.targets().size(), subset.size(),
                  cfg.positions, bt.size(), gt.size(), lastG.rays, lastG.vertices, bm, minOf(bt), maxOf(bt), median(bs), bcm, lastB.tests,
                  gm, minOf(gt), maxOf(gt), median(gs), gcm, lastG.tests, lastG.cells, gm > 0 ? bm / gm : 0.0, gcm > 0 ? bcm / gcm : 0.0,
                  median(rebuildGrid), median(rebuildIsect), median(pair), checksum);
    out.f << line << "\n";
    out.f.flush();
    std::printf("%-9s n=%-4d seed=%d scale=%-5s pos=%zu/%d samples=%zu/%zu  brute %.1f us  grid %.1f us  x%.1f\n", sceneKindName(kind),
                nRequested, seed, scaleLabel.c_str(), subset.size(), cfg.positions, bt.size(), gt.size(), bm, gm,
                gm > 0 ? bm / gm : 0.0);
    std::fflush(stdout);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    std::string outPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) outPath = argv[++i];
        else if (a == "--quick") cfg.quick = true;
        else if (a == "--budget" && i + 1 < argc) cfg.budget = std::atof(argv[++i]);
        else if (a == "--samples" && i + 1 < argc) cfg.samples = std::max(3, std::atoi(argv[++i]));
        else {
            std::fprintf(stderr, "usage: vis_bench --out <results.csv> [--quick] [--budget SECONDS] [--samples N]\n");
            return 2;
        }
    }
    if (outPath.empty()) {
        std::fprintf(stderr, "usage: vis_bench --out <results.csv> [--quick] [--budget SECONDS] [--samples N]\n");
        return 2;
    }
    if (cfg.quick) {
        cfg.positions = 16;
        cfg.samples = 3;
        cfg.budget = std::min(cfg.budget, 1.0);
        cfg.editReps = 5;
    }
    const std::filesystem::path out(outPath);
    const Meta meta = makeMeta(cfg);
    {
        std::filesystem::path metaPath = out;
        metaPath.replace_extension(".meta.txt");
        if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path());
        std::ofstream mf(metaPath);
        for (const auto& kv : meta.items) mf << kv.first << ": " << kv.second << "\n";
    }
    Writer main, sweep;
    if (!main.open(out, meta) || !sweep.open(out.parent_path() / "grid_sweep.csv", meta)) {
        std::fprintf(stderr, "cannot open output files\n");
        return 2;
    }

    const auto start = Clock::now();
    const std::vector<SceneKind> kinds = {SceneKind::Scatter, SceneKind::Maze, SceneKind::LongWalls, SceneKind::Cluster};
    const std::vector<int> sizes = cfg.quick ? std::vector<int>{64, 250} : std::vector<int>{250, 500, 1000, 2000, 4000};
    const std::vector<int> seeds = cfg.quick ? std::vector<int>{1} : std::vector<int>{1, 2, 3};

    for (SceneKind kind : kinds)
        for (int n : sizes)
            for (int seed : seeds) {
                Scene scene = makeScene(kind, n, static_cast<std::uint64_t>(seed));
                if (!measureRow(scene, kind, n, seed, "auto", cfg, main)) return 1;
            }
    for (int n : cfg.quick ? std::vector<int>{16} : std::vector<int>{16, 64})
        for (int seed : seeds) {
            Scene scene = makeScene(SceneKind::Scatter, n, static_cast<std::uint64_t>(seed));
            if (!measureRow(scene, SceneKind::Scatter, n, seed, "auto", cfg, main)) return 1;
        }

    // Grid-resolution sweep: cols x rows scaled from the automatic choice.
    const std::vector<double> scales = cfg.quick ? std::vector<double>{0.5, 1.0, 2.0} : std::vector<double>{0.25, 0.5, 1.0, 2.0, 4.0};
    const int sweepN = cfg.quick ? 250 : 1000;
    for (SceneKind kind : cfg.quick ? std::vector<SceneKind>{SceneKind::Scatter} : std::vector<SceneKind>{SceneKind::Scatter, SceneKind::LongWalls}) {
        Scene scene = makeScene(kind, sweepN, 1);
        const int autoCols = scene.grid().cols(), autoRows = scene.grid().rows();
        for (double s : scales) {
            scene.setGridResolution(std::max(1, static_cast<int>(std::lround(autoCols * s))),
                                    std::max(1, static_cast<int>(std::lround(autoRows * s))));
            char label[16];
            std::snprintf(label, sizeof label, "%g", s);
            if (!measureRow(scene, kind, sweepN, 1, label, cfg, sweep)) return 1;
        }
    }

    std::printf("done in %.1f s; output checksum sink %llu\n", seconds(start, Clock::now()), static_cast<unsigned long long>(g_sink));
    return 0;
}
