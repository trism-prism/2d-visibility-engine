#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "geometry.h"
#include "grid.h"

namespace vis {

enum class AddStatus {
    Added,
    RejectedNonFinite,
    RejectedOutside,   // entirely outside the world rectangle
    RejectedTooShort   // clipped length < kMinWallLength (includes zero-length input)
};

// Counts and timings of the most recent rebuild of the derived data.
struct RebuildStats {
    double gridBuildUs = 0.0;     // grid resolution choice + cell registration
    double intersectUs = 0.0;     // intersection finding + target construction
    std::size_t walls = 0;        // including the four boundary walls
    std::size_t gridCells = 0;
    std::size_t registrations = 0;
    std::size_t pairsTested = 0;  // wall pairs actually handed to intersectSegments
    std::size_t intersections = 0;  // proper crossings
    std::size_t targets = 0;        // de-duplicated endpoints + crossings
};

// Owns the walls and everything derived from them. Walls 0..3 are the world boundary (bottom,
// right, top, left) and cannot be removed. Every mutator rebuilds the grid, intersections and
// targets before returning, so derived data is never older than the wall list (invariant 4).
class Scene {
public:
    static constexpr std::size_t kBoundaryWalls = 4;

    explicit Scene(double width = 1600.0, double height = 1000.0);

    double width() const { return w_; }
    double height() const { return h_; }

    const std::vector<Segment>& walls() const { return walls_; }
    std::size_t wallCount() const { return walls_.size(); }
    std::size_t userWallCount() const { return walls_.size() - kBoundaryWalls; }

    // Applies the insertion rules to one wall without touching any scene (used by generators).
    static AddStatus sanitizeWall(Vec2 a, Vec2 b, double w, double h, Segment& out);

    AddStatus addWall(Vec2 a, Vec2 b);  // on success the wall is at index wallCount() - 1
    bool removeWall(std::size_t index);  // false for boundary walls and out-of-range indices
    void clear();                        // removes all user walls
    // Replaces all user walls in one go (single rebuild). Returns how many were accepted.
    std::size_t setWalls(std::span<const Segment> userWalls);

    // Manual override; clamped to [1, kMaxGridDim] per axis. setGridAuto restores the heuristic.
    static constexpr int kMaxGridDim = 2048;
    void setGridResolution(int cols, int rows);
    void setGridAuto();
    bool gridIsAuto() const { return autoGrid_; }
    // cols x rows with roughly square cells and cols*rows ~ wallCount (clamped).
    static void autoResolution(std::size_t wallCount, double w, double h, int& cols, int& rows);

    const Grid& grid() const { return grid_; }
    const std::vector<Vec2>& intersections() const { return intersections_; }  // proper crossings
    const std::vector<Vec2>& targets() const { return targets_; }              // sorted, unique
    const RebuildStats& lastRebuild() const { return stats_; }
    std::uint64_t revision() const { return revision_; }  // increments on every rebuild

private:
    void rebuild();

    double w_;
    double h_;
    std::vector<Segment> walls_;
    bool autoGrid_ = true;
    int cols_ = 1;
    int rows_ = 1;
    Grid grid_;
    std::vector<Vec2> intersections_;
    std::vector<Vec2> targets_;
    RebuildStats stats_;
    std::uint64_t revision_ = 0;
};

}  // namespace vis
