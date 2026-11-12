#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "geometry.h"

namespace vis {

// Uniform grid over [0,w] x [0,h]. Cell lists are flat (CSR): contiguous, walls ascending within a
// cell. A wall is registered in every cell whose box, inflated by kGridPad * cellSize, it touches
// (inclusive test), so a wall through a cell corner is in all four adjacent cells.
class Grid {
public:
    void build(const std::vector<Segment>& walls, double w, double h, int cols, int rows);

    int cols() const { return cols_; }
    int rows() const { return rows_; }
    std::size_t cellCount() const { return static_cast<std::size_t>(cols_) * static_cast<std::size_t>(rows_); }
    double cellWidth() const { return cellW_; }
    double cellHeight() const { return cellH_; }
    double width() const { return w_; }
    double height() const { return h_; }

    // Cell boundaries; the last one is exactly the world edge.
    double xEdge(int i) const { return i >= cols_ ? w_ : i * cellW_; }
    double yEdge(int j) const { return j >= rows_ ? h_ : j * cellH_; }
    std::size_t cellIndex(int cx, int cy) const {
        return static_cast<std::size_t>(cy) * static_cast<std::size_t>(cols_) + static_cast<std::size_t>(cx);
    }
    int cellXOf(double x) const;
    int cellYOf(double y) const;

    std::span<const std::uint32_t> cellWalls(std::size_t cell) const {
        return {walls_.data() + start_[cell], start_[cell + 1] - start_[cell]};
    }
    // Cells a wall is registered in (the inverse of cellWalls).
    std::span<const std::uint32_t> wallCells(std::size_t wall) const {
        return {wallCells_.data() + wallStart_[wall], wallStart_[wall + 1] - wallStart_[wall]};
    }
    std::size_t registrations() const { return walls_.size(); }

private:
    int cols_ = 1;
    int rows_ = 1;
    double w_ = 1.0;
    double h_ = 1.0;
    double cellW_ = 1.0;
    double cellH_ = 1.0;
    std::vector<std::uint32_t> start_;       // cellCount + 1
    std::vector<std::uint32_t> walls_;       // wall indices, grouped by cell
    std::vector<std::uint32_t> wallStart_;   // wallCount + 1
    std::vector<std::uint32_t> wallCells_;   // cell indices, grouped by wall
};

// Amanatides-Woo traversal for a ray o + t*d with d NOT normalised, so every parameter is in the
// same t units as rayHit. Visits cells outward from the light; exitT() is the parameter at which
// the ray leaves the current cell.
class GridWalker {
public:
    GridWalker(const Grid& g, Vec2 o, Vec2 d);

    bool inside() const { return cx_ >= 0 && cx_ < g_.cols() && cy_ >= 0 && cy_ < g_.rows(); }
    std::size_t cell() const { return g_.cellIndex(cx_, cy_); }
    double exitT() const { return tMaxX_ < tMaxY_ ? tMaxX_ : tMaxY_; }
    void advance();

private:
    const Grid& g_;
    int cx_;
    int cy_;
    int stepX_ = 0;
    int stepY_ = 0;
    double tMaxX_ = kInf;
    double tMaxY_ = kInf;
    double tDeltaX_ = kInf;
    double tDeltaY_ = kInf;
};

}  // namespace vis
