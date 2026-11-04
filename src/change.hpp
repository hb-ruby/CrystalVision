#pragma once

// Cheap screen change detection, so the monitor can skip Calm scans of a screen that hasn't
// changed since it was last scanned. No model, no X server: pure functions on a Frame.
//
// The screen is split into coarse cells (60 px by default, so 32x18 at 1920x1080). Each cell
// keeps its mean brightness and mean colour, sampled from every few pixels. Two screens are
// compared by their largest single-cell change, not the whole-screen average, so a small
// image appearing on an otherwise static screen isn't diluted away.
//
// Colour (chroma) is the main signal. Scrolling grey text barely changes it (at most 3.8 on
// the captured test screens), while even a 60 px colour thumbnail moves it 6 or more, and
// skin in a colour photo is never grey. Brightness is a backstop for big grey or black-and-
// white changes. It needs a high threshold, because scrolling text moves the gaps between
// lines through each cell and shifts its brightness by up to about 36. Contrast was tried and
// dropped: scrolling text moves it as much as a thumbnail does.

#include "frame.hpp"

#include <chrono>
#include <optional>
#include <vector>

namespace crystal {

// One cell's summary, on a 0..255 scale.
struct CellStats {
    float luma = 0; // mean brightness (BT.601 Y)
    float cb = 0;   // mean blue-difference colour
    float cr = 0;   // mean red-difference colour
};

struct ScreenGrid {
    int width = 0, height = 0; // of the frame it summarises
    int cols = 0, rows = 0;
    std::vector<CellStats> cells; // row-major, cols * rows
};

// Summarises `frame` in cells of `cellSize` pixels (the last row and column may be smaller),
// sampling every `step`-th pixel in each direction. One pass over the frame.
ScreenGrid summarize(const Frame& frame, int cellSize = 60, int step = 3);

// The largest change of any one cell between two summaries, for brightness and colour
// separately (they may come from different cells). Colour is the larger of the Cb and Cr
// changes. Both are infinity if the grids don't match (the screen size changed).
struct GridChange {
    float luma = 0;
    float chroma = 0;
};
GridChange compareGrids(const ScreenGrid& a, const ScreenGrid& b);

// Above either of these, the screen counts as changed. Tuned on captured screens (text PDF,
// scrolling browser text, thumbnails pasted onto them).
struct ChangeThresholds {
    float luma = 45.0f;
    float chroma = 5.0f;
};

constexpr bool exceeds(const GridChange& c, const ChangeThresholds& t) {
    return c.luma > t.luma || c.chroma > t.chroma;
}

// Decides whether a Calm scan can be skipped because the screen hasn't changed.
//
//   A scan at any scale is compared with the last scan that ran (Calm or Alert), not the last
//   capture, so slow scrolling can't creep under the threshold 5 s at a time.
//   A full-resolution scan is compared with the last *full-resolution* scan that ran. Without
//   that, a small image that appears and is missed by a 0.333 scan would never be looked at
//   sharply: the screen stays "unchanged" since that 0.333 scan, so every 1.0 scan is skipped.
//   A scan is never skipped once maxSkip has passed since the last scan that ran, so a missed
//   change or a bad threshold can't leave the screen unscanned for long.
class ChangeGate {
  public:
    using Clock = std::chrono::steady_clock;

    struct Options {
        ChangeThresholds threshold;
        std::chrono::milliseconds maxSkip{60000};
    };

    ChangeGate() : ChangeGate{Options{}} {}
    explicit ChangeGate(Options opts) : opts_{opts} {}

    // How much `now` differs from the screen this scan at `scale` is compared with; nullopt if
    // there's nothing to compare with yet.
    std::optional<GridChange> change(const ScreenGrid& now, float scale) const;

    // Whether the scan at `scale` can be skipped at `time`, given the screen `now`.
    bool canSkip(const ScreenGrid& now, float scale, Clock::time_point time) const;

    // Records a scan at `scale` that ran on the screen `now` at `time`.
    void ran(const ScreenGrid& now, float scale, Clock::time_point time);

  private:
    const ScreenGrid* reference(float scale) const;

    Options opts_;
    std::optional<ScreenGrid> last_;     // the last scan that ran
    std::optional<ScreenGrid> lastFull_; // the last full-resolution scan that ran
    Clock::time_point lastRan_{};
};

} // namespace crystal
