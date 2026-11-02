#pragma once

#include "frame.hpp"
#include "x11.hpp"

#include <xcb/shm.h>
#include <xcb/xcb.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace crystal {

// Rebuilds the screen image window by window from the off-screen copies that the
// Composite extension keeps, so chosen windows (our own overlay) can be left out.
//
// It asks for *automatic* redirection: if a compositor (e.g. Muffin) is running this
// changes nothing; if not, the X server keeps window contents and paints the screen
// itself. It also keeps windows readable when a compositor unredirects fullscreen ones.
class Capturer {
  public:
    struct Stats {
        bool wallpaperDrawn = false;
        int windowsDrawn = 0;
        int windowsSkipped = 0; // unmapped, input-only, unsupported depth, vanished mid-grab
    };

    explicit Capturer(x11::Connection& x); // throws std::runtime_error
    ~Capturer();
    Capturer(const Capturer&) = delete;
    Capturer& operator=(const Capturer&) = delete;

    // Leaves this top-level window out of every capture (e.g. our overlay).
    void exclude(xcb_window_t window);

    // Captures the whole screen (all monitors) into `out`, resizing it if the screen
    // size changed.
    Stats capture(Frame& out);

  private:
    void ensureShm(std::size_t bytes);
    void releaseShm();
    bool grabWindow(Frame& out, xcb_window_t window, const xcb_get_geometry_reply_t& geo);
    bool grabWallpaper(Frame& out);

    x11::Connection& x_;
    // Root-window properties where desktops publish the wallpaper pixmap. Cinnamon draws
    // the wallpaper itself (not in any window), so without this the background is black.
    xcb_atom_t wallpaperAtoms_[2] = {XCB_ATOM_NONE, XCB_ATOM_NONE};
    std::vector<xcb_window_t> excluded_;
    xcb_pixmap_t pixmap_ = 0; // id reused for each window's named pixmap

    // Shared-memory segment the X server writes window pixels into.
    xcb_shm_seg_t seg_ = 0;
    int shmId_ = -1;
    std::uint8_t* shmAddr_ = nullptr;
    std::size_t shmSize_ = 0;
};

} // namespace crystal
