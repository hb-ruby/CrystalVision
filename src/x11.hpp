#pragma once

#include <xcb/xcb.h>

#include <cstdlib>
#include <memory>

namespace crystal::x11 {

// Owns an XCB reply (or error); XCB allocates them with malloc.
struct FreeDeleter {
    void operator()(void* p) const { std::free(p); }
};
template <class T> using Reply = std::unique_ptr<T, FreeDeleter>;

// A connection to the X server named by $DISPLAY, plus its default screen.
class Connection {
  public:
    Connection(); // throws std::runtime_error if the server can't be reached

    xcb_connection_t* get() const { return conn_.get(); }
    const xcb_screen_t& screen() const { return *screen_; }
    xcb_window_t root() const { return screen_->root; }
    int screenNumber() const { return screenNum_; }

    bool hasExtension(xcb_extension_t* ext) const;

    // Frees every event and error waiting on the connection; returns how many were errors.
    // Nothing here selects events, but the server sends some to every client (MappingNotify
    // on a keyboard layout change), and errors from unchecked requests queue up the same way.
    // Left unread they pile up in memory, so a long-running loop must call this regularly.
    int discardEvents();

  private:
    struct Disconnect {
        void operator()(xcb_connection_t* c) const { xcb_disconnect(c); }
    };
    std::unique_ptr<xcb_connection_t, Disconnect> conn_;
    const xcb_screen_t* screen_ = nullptr;
    int screenNum_ = 0;
};

} // namespace crystal::x11
