// Unit tests for the pixel-pasting logic in frame.cpp (no X server needed).

#include "check.hpp"
#include "frame.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct Rgb {
    int r, g, b;
};

Rgb at(const crystal::Frame& f, int x, int y) {
    const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(f.width) +
                    static_cast<std::size_t>(x)) *
                   3;
    return {f.rgb[i], f.rgb[i + 1], f.rgb[i + 2]};
}

bool is(Rgb p, int r, int g, int b) {
    return p.r == r && p.g == g && p.b == b;
}

// A solid w*h image in X's B,G,R,A byte order.
std::vector<std::uint8_t> solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                std::uint8_t a = 255) {
    std::vector<std::uint8_t> px;
    for (int i = 0; i < w * h; ++i) px.insert(px.end(), {b, g, r, a});
    return px;
}

void convertsBgrxToRgb() {
    crystal::Frame f;
    f.reset(2, 2);
    auto px = solid(2, 2, 10, 20, 30, 0); // alpha byte is padding for opaque images
    crystal::paste(f, {px.data(), 2, 2, 8, false}, 0, 0);
    CHECK(is(at(f, 0, 0), 10, 20, 30));
    CHECK(is(at(f, 1, 1), 10, 20, 30));
}

void clipsAtEveryEdge() {
    crystal::Frame f;
    f.reset(4, 4);
    auto px = solid(3, 3, 255, 0, 0);
    crystal::paste(f, {px.data(), 3, 3, 12, false}, -2, -2); // only (0,0) lands
    CHECK(is(at(f, 0, 0), 255, 0, 0));
    CHECK(is(at(f, 1, 0), 0, 0, 0));
    CHECK(is(at(f, 0, 1), 0, 0, 0));

    crystal::paste(f, {px.data(), 3, 3, 12, false}, 3, 3); // only (3,3) lands
    CHECK(is(at(f, 3, 3), 255, 0, 0));
    CHECK(is(at(f, 2, 3), 0, 0, 0));

    crystal::paste(f, {px.data(), 3, 3, 12, false}, 10, 10); // fully off-screen: no crash
    crystal::paste(f, {px.data(), 3, 3, 12, false}, -10, 0);
}

void respectsStride() {
    // 1x2 image with 4 bytes of row padding: rows are 8 bytes apart.
    std::vector<std::uint8_t> px = {1, 2, 3, 0, 99, 99, 99, 99, 4, 5, 6, 0, 99, 99, 99, 99};
    crystal::Frame f;
    f.reset(1, 2);
    crystal::paste(f, {px.data(), 1, 2, 8, false}, 0, 0);
    CHECK(is(at(f, 0, 0), 3, 2, 1));
    CHECK(is(at(f, 0, 1), 6, 5, 4));
}

void blendsPremultipliedAlpha() {
    crystal::Frame f;
    f.reset(1, 1);
    auto white = solid(1, 1, 200, 200, 200);
    crystal::paste(f, {white.data(), 1, 1, 4, false}, 0, 0);

    auto clear = solid(1, 1, 0, 0, 0, 0); // fully transparent: background unchanged
    crystal::paste(f, {clear.data(), 1, 1, 4, true}, 0, 0);
    CHECK(is(at(f, 0, 0), 200, 200, 200));

    auto halfRed = solid(1, 1, 128, 0, 0, 128); // 50% red, premultiplied
    crystal::paste(f, {halfRed.data(), 1, 1, 4, true}, 0, 0);
    const Rgb p = at(f, 0, 0);
    CHECK(p.r == 128 + 100 && p.g == 100 && p.b == 100); // 200 * 127/255 rounds to 100
}

} // namespace

int main() {
    convertsBgrxToRgb();
    clipsAtEveryEdge();
    respectsStride();
    blendsPremultipliedAlpha();
    return check::finish("frame_test");
}
