#include "grid.h"

#include <algorithm>
#include <cmath>

namespace vis {

namespace {

// Smallest index range [lo, hi] of cells (1D) whose padded extent [edge(i)-pad, edge(i+1)+pad]
// intersects [vlo, vhi]. The division gives a first guess; the loops make it exact.
template <class Edge>
void cellRange(double vlo, double vhi, int n, double cell, double pad, Edge edge, int& lo, int& hi) {
    lo = std::clamp(static_cast<int>(std::floor(vlo / cell)), 0, n - 1);
    hi = std::clamp(static_cast<int>(std::floor(vhi / cell)), 0, n - 1);
    while (lo > 0 && edge(lo) + pad >= vlo) --lo;
    while (lo < n - 1 && edge(lo + 1) + pad < vlo) ++lo;
    while (hi < n - 1 && edge(hi + 1) - pad <= vhi) ++hi;
    while (hi > 0 && edge(hi) - pad > vhi) --hi;
    if (hi < lo) hi = lo;
}

}  // namespace

int Grid::cellXOf(double x) const {
    return std::clamp(static_cast<int>(std::floor(x / cellW_)), 0, cols_ - 1);
}
int Grid::cellYOf(double y) const {
    return std::clamp(static_cast<int>(std::floor(y / cellH_)), 0, rows_ - 1);
}

void Grid::build(const std::vector<Segment>& walls, double w, double h, int cols, int rows) {
    cols_ = std::max(cols, 1);
    rows_ = std::max(rows, 1);
    w_ = w;
    h_ = h;
    cellW_ = w / cols_;
    cellH_ = h / rows_;
    const double padX = kGridPad * cellW_;
    const double padY = kGridPad * cellH_;
    const auto xe = [this](int i) { return xEdge(i); };
    const auto ye = [this](int j) { return yEdge(j); };

    // Pass 1: per wall, the cells it touches.
    wallStart_.assign(walls.size() + 1, 0);
    wallCells_.clear();
    for (std::size_t i = 0; i < walls.size(); ++i) {
        const Segment& s = walls[i];
        int x0, x1, y0, y1;
        cellRange(std::min(s.a.x, s.b.x), std::max(s.a.x, s.b.x), cols_, cellW_, padX, xe, x0, x1);
        cellRange(std::min(s.a.y, s.b.y), std::max(s.a.y, s.b.y), rows_, cellH_, padY, ye, y0, y1);
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                if (segmentOverlapsAABB(s, xEdge(cx) - padX, yEdge(cy) - padY, xEdge(cx + 1) + padX,
                                        yEdge(cy + 1) + padY)) {
                    wallCells_.push_back(static_cast<std::uint32_t>(cellIndex(cx, cy)));
                }
            }
        }
        wallStart_[i + 1] = static_cast<std::uint32_t>(wallCells_.size());
    }

    // Pass 2: counting sort into per-cell lists. Walking walls in index order keeps each list ascending.
    start_.assign(cellCount() + 1, 0);
    for (std::uint32_t c : wallCells_) ++start_[c + 1];
    for (std::size_t c = 0; c < cellCount(); ++c) start_[c + 1] += start_[c];
    walls_.resize(wallCells_.size());
    std::vector<std::uint32_t> cursor(start_.begin(), start_.end() - 1);
    for (std::size_t i = 0; i < walls.size(); ++i) {
        for (std::uint32_t k = wallStart_[i]; k < wallStart_[i + 1]; ++k) {
            walls_[cursor[wallCells_[k]]++] = static_cast<std::uint32_t>(i);
        }
    }
}

GridWalker::GridWalker(const Grid& g, Vec2 o, Vec2 d) : g_(g), cx_(g.cellXOf(o.x)), cy_(g.cellYOf(o.y)) {
    if (d.x > 0.0) {
        stepX_ = 1;
        tMaxX_ = (g.xEdge(cx_ + 1) - o.x) / d.x;
        tDeltaX_ = g.cellWidth() / d.x;
    } else if (d.x < 0.0) {
        stepX_ = -1;
        tMaxX_ = (g.xEdge(cx_) - o.x) / d.x;
        tDeltaX_ = g.cellWidth() / -d.x;
    }
    if (d.y > 0.0) {
        stepY_ = 1;
        tMaxY_ = (g.yEdge(cy_ + 1) - o.y) / d.y;
        tDeltaY_ = g.cellHeight() / d.y;
    } else if (d.y < 0.0) {
        stepY_ = -1;
        tMaxY_ = (g.yEdge(cy_) - o.y) / d.y;
        tDeltaY_ = g.cellHeight() / -d.y;
    }
}

void GridWalker::advance() {
    // On a tie the ray passes through a cell corner; stepping y first then x visits the cell the
    // ray only touches, which is harmless (walls through a corner are registered in all four).
    if (tMaxX_ < tMaxY_) {
        cx_ += stepX_;
        tMaxX_ += tDeltaX_;
    } else {
        cy_ += stepY_;
        tMaxY_ += tDeltaY_;
    }
}

}  // namespace vis
