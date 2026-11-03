#pragma once

#include "detect.hpp"
#include "x11.hpp"

#include <xcb/xcb.h>

#include <span>
#include <vector>

namespace crystal {

// Solid black rectangles drawn over the screen.
//
// One override-redirect window covers the whole screen (the window manager leaves it alone:
// no frame, no taskbar entry, not focusable). Its bounding shape is cut down to just the
// rectangles, so everything else shows through, and its input shape is empty, so clicks
// and scrolling pass through to the windows below. The capturer must exclude window()
// (see the feedback-loop gotcha in CLAUDE.md).
class Overlay {
  public:
    explicit Overlay(x11::Connection& x); // throws std::runtime_error
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    xcb_window_t window() const { return window_; }

    // Covers exactly these boxes (screen pixels), replacing what was covered before, and
    // raises the overlay above any window opened since. No boxes hides it.
    void show(std::span<const Box> boxes);

  private:
    x11::Connection& x_;
    xcb_window_t window_ = 0;
    bool mapped_ = false;
    int width_ = 0;
    int height_ = 0;
    std::vector<xcb_rectangle_t> rects_; // reused between calls
};

} // namespace crystal
