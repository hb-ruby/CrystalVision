// Unit tests for the screen change detection. No model or X server needed.

#include "change.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

using namespace std::chrono_literals;
using Clock = crystal::ChangeGate::Clock;

void fill(crystal::Frame& f, int x0, int y0, int w, int h, std::uint8_t r, std::uint8_t g,
          std::uint8_t b) {
    for (int y = y0; y < y0 + h && y < f.height; ++y)
        for (int x = x0; x < x0 + w && x < f.width; ++x) {
            std::uint8_t* p = &f.rgb[(static_cast<std::size_t>(y) * f.width + x) * 3];
            p[0] = r;
            p[1] = g;
            p[2] = b;
        }
}

crystal::Frame page(int width = 400, int height = 300) {
    crystal::Frame f;
    f.reset(width, height);
    fill(f, 0, 0, width, height, 255, 255, 255);
    return f;
}

// Lines of dark grey "text" every 20 px, starting `offset` px down.
crystal::Frame text(int offset) {
    crystal::Frame f = page();
    for (int y = offset; y < f.height; y += 20) fill(f, 20, y, 360, 6, 60, 60, 60);
    return f;
}

void summarisesCells() {
    crystal::Frame f = page(130, 70); // 3 x 2 cells, the last column and row partial
    fill(f, 0, 0, 60, 60, 0, 0, 0);
    const auto g = crystal::summarize(f);
    CHECK(g.cols == 3 && g.rows == 2 && g.cells.size() == 6);
    CHECK(g.cells[0].luma == 0);
    CHECK(std::abs(g.cells[1].luma - 255) < 1 && std::abs(g.cells[5].luma - 255) < 1);
    CHECK(std::abs(g.cells[1].cb) < 1 && std::abs(g.cells[1].cr) < 1); // white is grey
}

void identicalScreensDontChange() {
    const auto a = crystal::summarize(text(0));
    const auto c = crystal::compareGrids(a, crystal::summarize(text(0)));
    CHECK(c.luma == 0 && c.chroma == 0);
}

void sizeChangeAlwaysCounts() {
    const auto c =
        crystal::compareGrids(crystal::summarize(page()), crystal::summarize(page(401, 300)));
    CHECK(std::isinf(c.luma) && std::isinf(c.chroma));
    CHECK(crystal::exceeds(c, {}));
}

void scrolledGreyTextStaysUnder() {
    const auto c = crystal::compareGrids(crystal::summarize(text(0)), crystal::summarize(text(9)));
    CHECK(c.chroma < 1);
    CHECK(!crystal::exceeds(c, {}));
}

void smallColourImageCounts() {
    crystal::Frame f = text(0);
    fill(f, 200, 100, 40, 30, 220, 160, 130); // a 40 x 30 skin-coloured patch
    const auto c = crystal::compareGrids(crystal::summarize(text(0)), crystal::summarize(f));
    CHECK(c.chroma > crystal::ChangeThresholds{}.chroma);
    CHECK(crystal::exceeds(c, {}));
}

void bigGreyImageCounts() {
    crystal::Frame f = page();
    fill(f, 100, 60, 120, 120, 90, 90, 90);
    const auto c = crystal::compareGrids(crystal::summarize(page()), crystal::summarize(f));
    CHECK(c.chroma < 1 && c.luma > crystal::ChangeThresholds{}.luma);
}

void gateSkipsOnlyUnchangedScreens() {
    crystal::ChangeGate gate;
    const auto a = crystal::summarize(page());
    crystal::Frame bf = page();
    fill(bf, 0, 0, 100, 100, 200, 40, 40);
    const auto b = crystal::summarize(bf);
    const Clock::time_point t0{};

    CHECK(!gate.canSkip(a, 0.333f, t0)); // nothing to compare with yet
    CHECK(!gate.change(a, 0.333f));
    gate.ran(a, 0.333f, t0);
    CHECK(gate.canSkip(a, 0.333f, t0 + 5s));
    CHECK(!gate.canSkip(b, 0.333f, t0 + 5s));
}

void gateComparesWithTheLastScanThatRan() {
    // Slow scrolling: each step is small against the one before, but the gate keeps comparing
    // with the screen the last scan saw, so the steps add up and a scan runs.
    crystal::ChangeGate gate;
    const Clock::time_point t0{};
    gate.ran(crystal::summarize(page()), 0.333f, t0);
    int ranAt = -1;
    for (int step = 1; step <= 10 && ranAt < 0; ++step) {
        crystal::Frame f = page();
        fill(f, 100, 100, 60, 60, 255, static_cast<std::uint8_t>(255 - 4 * step),
             static_cast<std::uint8_t>(255 - 8 * step)); // slowly turning orange
        if (!gate.canSkip(crystal::summarize(f), 0.333f, t0 + step * 5s)) ranAt = step;
    }
    CHECK(ranAt > 1); // not the first small step
    CHECK(ranAt > 0); // but some step, well before the safety net
}

void gateComparesFullScansWithTheLastFullScan() {
    crystal::ChangeGate gate;
    const auto a = crystal::summarize(page());
    crystal::Frame bf = page();
    fill(bf, 0, 0, 100, 100, 200, 40, 40);
    const auto b = crystal::summarize(bf);
    const Clock::time_point t0{};

    gate.ran(a, 1.0f, t0);
    gate.ran(b, 0.333f, t0 + 5s); // b appeared; only a cheap scan has looked at it
    CHECK(gate.canSkip(b, 0.333f, t0 + 10s));
    CHECK(!gate.canSkip(b, 1.0f, t0 + 10s)); // the next full scan still runs
    gate.ran(b, 1.0f, t0 + 10s);
    CHECK(gate.canSkip(b, 1.0f, t0 + 15s));
}

void gateForcesAScanAfterMaxSkip() {
    crystal::ChangeGate gate;
    const auto a = crystal::summarize(page());
    const Clock::time_point t0{};
    gate.ran(a, 0.333f, t0);
    CHECK(gate.canSkip(a, 0.333f, t0 + 55s));
    CHECK(!gate.canSkip(a, 0.333f, t0 + 60s));
    gate.ran(a, 0.333f, t0 + 60s);
    CHECK(gate.canSkip(a, 0.333f, t0 + 65s));
}

} // namespace

int main() {
    summarisesCells();
    identicalScreensDontChange();
    sizeChangeAlwaysCounts();
    scrolledGreyTextStaysUnder();
    smallColourImageCounts();
    bigGreyImageCounts();
    gateSkipsOnlyUnchangedScreens();
    gateComparesWithTheLastScanThatRan();
    gateComparesFullScansWithTheLastFullScan();
    gateForcesAScanAfterMaxSkip();
    if (failures == 0) std::printf("change_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
