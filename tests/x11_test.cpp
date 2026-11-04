// Integration tests for screen capture and the overlay against a real X server: the code that
// owns X resources and shared memory. Skipped when there's no $DISPLAY. CI runs it under Xvfb;
// locally it uses the desktop, briefly showing a small red window and a black box.
//
// Run it in the asan preset to check memory handling: ASan catches bad reads of the shared
// segment, LeakSanitizer catches leaked replies, and the checks below catch what neither sees
// (segments left in the kernel, events piling up on the connection, X errors).

#include "capture.hpp"
#include "check.hpp"
#include "frame.hpp"
#include "overlay.hpp"
#include "x11.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

using crystal::Box;
using crystal::Capturer;
using crystal::Frame;
using crystal::Overlay;
using crystal::x11::Connection;
using crystal::x11::Reply;

// Shared memory segments this process created that the kernel still holds.
int ownShmSegments() {
    std::ifstream in{"/proc/sysvipc/shm"};
    std::string line;
    std::getline(in, line); // header
    int n = 0;
    while (std::getline(in, line)) {
        std::istringstream f{line};
        long key, id, perms, size, cpid;
        if (f >> key >> id >> perms >> size >> cpid && cpid == getpid()) ++n;
    }
    return n;
}

long rssKb() {
    std::ifstream in{"/proc/self/statm"};
    long pages = 0, resident = 0;
    in >> pages >> resident;
    return resident * (sysconf(_SC_PAGESIZE) / 1024);
}

// Waits until the server has handled everything sent so far, and drawn it.
void settle(Connection& x) {
    Reply<xcb_get_input_focus_reply_t> r{
        xcb_get_input_focus_reply(x.get(), xcb_get_input_focus(x.get()), nullptr)};
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
}

struct Rgb {
    int r, g, b;
    bool operator==(const Rgb&) const = default;
};

Rgb pixel(const Frame& f, int x, int y) {
    const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(f.width) +
                    static_cast<std::size_t>(x)) *
                   3;
    return {f.rgb[i], f.rgb[i + 1], f.rgb[i + 2]};
}

// A solid-colour override-redirect window (the window manager leaves it where it's put).
class TestWindow {
  public:
    TestWindow(Connection& x, int left, int top, int w, int h, std::uint32_t rgb)
        : x_{x}, id_{xcb_generate_id(x.get())} {
        const std::uint32_t values[] = {rgb, 1};
        xcb_create_window(x.get(), XCB_COPY_FROM_PARENT, id_, x.root(),
                          static_cast<std::int16_t>(left), static_cast<std::int16_t>(top),
                          static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), 0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, x.screen().root_visual,
                          XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT, values);
        xcb_map_window(x.get(), id_);
        settle(x_);
    }
    ~TestWindow() {
        xcb_destroy_window(x_.get(), id_);
        settle(x_);
    }
    TestWindow(const TestWindow&) = delete;
    TestWindow& operator=(const TestWindow&) = delete;

    void resize(int w, int h) {
        const std::uint32_t size[] = {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)};
        xcb_configure_window(x_.get(), id_, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                             size);
        xcb_clear_area(x_.get(), 0, id_, 0, 0, 0, 0); // repaint the background
        settle(x_);
    }

  private:
    Connection& x_;
    xcb_window_t id_;
};

void connectingToNoServerThrows() {
    const std::string display = std::getenv("DISPLAY");
    setenv("DISPLAY", ":4242", 1);
    bool threw = false;
    try {
        Connection x;
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    setenv("DISPLAY", display.c_str(), 1);
}

void discardsQueuedErrors(Connection& x) {
    CHECK(x.discardEvents() == 0);
    xcb_free_pixmap(x.get(), xcb_generate_id(x.get())); // unchecked, and no such pixmap
    settle(x);
    CHECK(x.discardEvents() == 1); // the error was queued, and is now freed
    CHECK(x.discardEvents() == 0);
}

void capturersReleaseEverything(Connection& x) {
    CHECK(ownShmSegments() == 0);
    for (int i = 0; i < 20; ++i) {
        Capturer cap{x};
        Frame f;
        const auto stats = cap.capture(f);
        CHECK(f.width == x.screen().width_in_pixels && f.height == x.screen().height_in_pixels);
        CHECK(f.rgb.size() == static_cast<std::size_t>(f.width) * f.height * 3);
        CHECK(stats.windowsDrawn + stats.windowsSkipped > 0);
        CHECK(ownShmSegments() == 1); // one reused segment, not one per window
    }
    CHECK(ownShmSegments() == 0); // each destructor detached it, so the kernel freed it
}

void capturesWindowsButNotTheOverlay(Connection& x) {
    constexpr Rgb red{255, 0, 0};
    TestWindow win{x, 40, 40, 80, 60, 0xff0000};
    Capturer cap{x};
    Frame f;
    cap.capture(f);
    CHECK(pixel(f, 80, 70) == red);

    // The overlay covers the window: the capture that leaves it out still sees red, and one
    // that doesn't sees the black box (so the box really is there).
    Overlay overlay{x};
    cap.exclude(overlay.window());
    overlay.show(std::vector<Box>{{30, 30, 100, 80}});
    settle(x);
    cap.capture(f);
    CHECK(pixel(f, 80, 70) == red);
    {
        Capturer plain{x};
        Frame g;
        plain.capture(g);
        CHECK((pixel(g, 80, 70) == Rgb{0, 0, 0}));
    }

    // Out-of-range, empty and non-finite boxes are clipped or dropped without an X error.
    const float huge = 1e30f, nan = std::numeric_limits<float>::quiet_NaN();
    overlay.show(std::vector<Box>{{-100, -100, 50, 50},
                                  {-50, -50, 100, 100},
                                  {0, 0, 0, 0},
                                  {5000, 5000, 10, 10},
                                  {-huge, -huge, 2 * huge, 2 * huge},
                                  {nan, 0, 10, 10},
                                  {0, 0, nan, nan}});
    overlay.show({});
    settle(x);
    cap.capture(f);
    CHECK(pixel(f, 80, 70) == red);

    // A window bigger than any so far makes the shared segment grow; still only one.
    win.resize(x.screen().width_in_pixels - 40, x.screen().height_in_pixels - 40);
    cap.capture(f);
    CHECK(pixel(f, x.screen().width_in_pixels - 10, x.screen().height_in_pixels - 10) == red);
    CHECK(ownShmSegments() == 1);
}

// Many monitor-like ticks: memory and the event queue must stay flat.
void longRunStaysFlat(Connection& x) {
    Capturer cap{x};
    Overlay overlay{x};
    cap.exclude(overlay.window());
    Frame f;
    long warm = 0;
    for (int i = 0; i < 60; ++i) {
        cap.capture(f);
        if (i % 3 == 0)
            overlay.show(std::vector<Box>{{static_cast<float>(i), 10, 40, 30}});
        else
            overlay.show({});
        CHECK(x.discardEvents() == 0);
        if (i == 10) warm = rssKb();
    }
    const long grown = rssKb() - warm;
    if (!check::kRssIsMeaningful) return;
    if (grown > 1024) std::fprintf(stderr, "RSS grew %ld KiB over 50 ticks\n", grown);
    CHECK(grown <= 1024);
}

} // namespace

int main() {
    const char* display = std::getenv("DISPLAY");
    if (!display || !*display) return check::skip("x11_test", "no DISPLAY");

    connectingToNoServerThrows();
    {
        Connection x;
        discardsQueuedErrors(x);
        capturersReleaseEverything(x);
        capturesWindowsButNotTheOverlay(x);
        longRunStaysFlat(x);
        settle(x);
        CHECK(x.discardEvents() == 0); // nothing we sent made the server report an error
    }
    CHECK(ownShmSegments() == 0);
    return check::finish("x11_test");
}
