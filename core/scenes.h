#pragma once
#include <cstdint>

#include "geometry.h"
#include "scene.h"

namespace vis {

// xoshiro256** seeded through splitmix64. Integer arithmetic plus exact double scaling, so a given
// seed gives the same stream on every compiler and standard library.
class Rng {
public:
    explicit Rng(std::uint64_t seed);
    std::uint64_t next();
    double uniform();                   // [0, 1)
    double range(double lo, double hi);  // [lo, hi)
    int below(int n);                   // [0, n), n > 0
    Vec2 direction();                   // unit vector, uniformly distributed, no trigonometry

private:
    std::uint64_t s_[4];
};

std::uint64_t splitmix64(std::uint64_t& state);

enum class SceneKind { Demo, Scatter, Maze, LongWalls, Cluster };
const char* sceneKindName(SceneKind kind);

constexpr double kStressWidth = 1600.0;
constexpr double kStressHeight = 1000.0;

// Hand-designed 1600 x 1000 scene (about 55 walls) and the light position it was designed for.
Scene makeDemoScene();
Vec2 demoLight();

// Stress scenes in the same 1600 x 1000 world. wallCount is the number of accepted user walls
// (the four boundary walls come on top). All walls pass through Scene's insertion rules.
Scene makeScatterScene(int wallCount, std::uint64_t seed);    // many short walls, uniform: grid-friendly
Scene makeMazeScene(int wallCount, std::uint64_t seed);       // lattice rooms with doors + long diagonals
Scene makeLongWallsScene(int wallCount, std::uint64_t seed);  // two near-parallel long families: grid-hostile
Scene makeClusterScene(int wallCount, std::uint64_t seed);    // everything inside a few cells: grid-hostile
Scene makeScene(SceneKind kind, int wallCount, std::uint64_t seed);  // Demo ignores the arguments

// Deterministic light placement for a loaded scene: among a 7 x 7 lattice of candidates (spacing 70)
// around `preferred`, those inside the world and at least 8 units from every user wall, the one whose
// visibility polygon (real Grid query) has the largest area, so the scene opens with a meaningful lit
// region. Ties go to the earliest candidate; `preferred` itself is candidate 0.
Vec2 chooseLight(const Scene& scene, Vec2 preferred);

}  // namespace vis
