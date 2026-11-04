#include "change.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace crystal {

namespace {

struct Sums {
    std::int64_t y = 0, cb = 0, cr = 0;
    std::int32_t n = 0;
};

} // namespace

ScreenGrid summarize(const Frame& frame, int cellSize, int step) {
    if (cellSize <= 0 || step <= 0) throw std::invalid_argument{"summarize: bad cell or step"};
    ScreenGrid g;
    g.width = frame.width;
    g.height = frame.height;
    g.cols = (frame.width + cellSize - 1) / cellSize;
    g.rows = (frame.height + cellSize - 1) / cellSize;
    std::vector<Sums> sums(static_cast<std::size_t>(g.cols) * static_cast<std::size_t>(g.rows));

    for (int y = 0; y < frame.height; y += step) {
        Sums* rowCells = sums.data() + static_cast<std::ptrdiff_t>(y / cellSize) * g.cols;
        const std::uint8_t* row = frame.rgb.data() + static_cast<std::size_t>(y) *
                                                         static_cast<std::size_t>(frame.width) * 3;
        for (int x = 0; x < frame.width; x += step) {
            const std::uint8_t* p = row + static_cast<std::ptrdiff_t>(x) * 3;
            const int r = p[0], gr = p[1], b = p[2];
            // BT.601 in fixed point: Y in 0..255, Cb and Cr in about -128..127.
            const int luma = (77 * r + 150 * gr + 29 * b) >> 8;
            const int cb = (144 * (b - luma)) >> 8; // 0.564 (B - Y)
            const int cr = (183 * (r - luma)) >> 8; // 0.713 (R - Y)
            Sums& s = rowCells[x / cellSize];
            s.y += luma;
            s.cb += cb;
            s.cr += cr;
            ++s.n;
        }
    }

    g.cells.resize(sums.size());
    for (std::size_t i = 0; i < sums.size(); ++i) {
        const Sums& s = sums[i];
        if (s.n == 0) continue;
        const double n = s.n;
        g.cells[i] = {static_cast<float>(static_cast<double>(s.y) / n),
                      static_cast<float>(static_cast<double>(s.cb) / n),
                      static_cast<float>(static_cast<double>(s.cr) / n)};
    }
    return g;
}

GridChange compareGrids(const ScreenGrid& a, const ScreenGrid& b) {
    if (a.width != b.width || a.height != b.height || a.cols != b.cols || a.rows != b.rows ||
        a.cells.size() != b.cells.size()) {
        constexpr float inf = std::numeric_limits<float>::infinity();
        return {inf, inf};
    }
    GridChange most;
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        const CellStats& p = a.cells[i];
        const CellStats& q = b.cells[i];
        most.luma = std::max(most.luma, std::abs(p.luma - q.luma));
        most.chroma = std::max({most.chroma, std::abs(p.cb - q.cb), std::abs(p.cr - q.cr)});
    }
    return most;
}

const ScreenGrid* ChangeGate::reference(float scale) const {
    const auto& ref = scale >= 1.0f ? lastFull_ : last_;
    return ref ? &*ref : nullptr;
}

std::optional<GridChange> ChangeGate::change(const ScreenGrid& now, float scale) const {
    const ScreenGrid* ref = reference(scale);
    if (!ref) return std::nullopt;
    return compareGrids(*ref, now);
}

bool ChangeGate::canSkip(const ScreenGrid& now, float scale, Clock::time_point time) const {
    if (!last_ || time - lastRan_ >= opts_.maxSkip) return false;
    const auto c = change(now, scale);
    return c && !exceeds(*c, opts_.threshold);
}

void ChangeGate::ran(const ScreenGrid& now, float scale, Clock::time_point time) {
    last_ = now;
    if (scale >= 1.0f) lastFull_ = now;
    lastRan_ = time;
}

} // namespace crystal
