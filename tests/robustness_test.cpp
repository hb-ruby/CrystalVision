// Randomised tests for every function that indexes into pixel or tensor buffers: thousands of
// random sizes, offsets and scales (fixed seed, so failures reproduce), plus hostile input
// (NaN and infinite model output, broken PPM files). Each result is checked against a simple
// reference or against invariants that must always hold.
//
// Run it in the asan preset: there, any out-of-bounds read or write, leak or undefined
// behaviour fails the test even where the result happens to look right.

#include "change.hpp"
#include "check.hpp"
#include "detect.hpp"
#include "frame.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using crystal::Frame;

std::mt19937 rng{20261104};

int randInt(int lo, int hi) { // inclusive
    return std::uniform_int_distribution<int>{lo, hi}(rng);
}

float randFloat(float lo, float hi) {
    return std::uniform_real_distribution<float>{lo, hi}(rng);
}

std::uint8_t randByte() {
    return static_cast<std::uint8_t>(randInt(0, 255));
}

Frame randomFrame(int w, int h) {
    Frame f;
    f.reset(w, h);
    for (auto& v : f.rgb) v = randByte();
    return f;
}

Frame solidFrame(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    Frame f;
    f.reset(w, h);
    for (std::size_t i = 0; i < f.rgb.size(); i += 3) {
        f.rgb[i] = r;
        f.rgb[i + 1] = g;
        f.rgb[i + 2] = b;
    }
    return f;
}

std::size_t idx(int w, int x, int y) {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
            static_cast<std::size_t>(x)) *
           3;
}

long peakRssKb() {
    std::ifstream in{"/proc/self/status"};
    std::string key;
    while (in >> key)
        if (key == "VmHWM:") {
            long kb = 0;
            in >> kb;
            return kb;
        }
    return 0;
}

void pastesLikeAPixelByPixelReference() {
    for (int iter = 0; iter < 2000; ++iter) {
        const int fw = randInt(0, 40), fh = randInt(0, 40);
        const int sw = randInt(0, 40), sh = randInt(0, 40);
        const int stride = sw * 4 + 4 * randInt(0, 2);
        const int x = randInt(-60, 60), y = randInt(-60, 60);
        const bool alpha = randInt(0, 1) == 1;

        Frame f = randomFrame(fw, fh);
        // Exactly the bytes the image claims (the last row needs no padding), so ASan sees
        // any read past it.
        std::vector<std::uint8_t> src(
            sh == 0 ? 0 : static_cast<std::size_t>((sh - 1) * stride + sw * 4));
        for (auto& v : src) v = randByte();

        Frame want = f;
        for (int sy = 0; sy < sh; ++sy)
            for (int sx = 0; sx < sw; ++sx) {
                const int dx = x + sx, dy = y + sy;
                if (dx < 0 || dy < 0 || dx >= fw || dy >= fh) continue;
                const std::uint8_t* s = &src[static_cast<std::size_t>(sy * stride + sx * 4)];
                std::uint8_t* d = &want.rgb[idx(fw, dx, dy)];
                for (int c = 0; c < 3; ++c) {
                    const unsigned sv = s[2 - c];
                    d[c] = alpha ? static_cast<std::uint8_t>(
                                       std::min(sv + (d[c] * (255u - s[3]) + 127u) / 255u, 255u))
                                 : static_cast<std::uint8_t>(sv);
                }
            }

        crystal::paste(f, {src.data(), sw, sh, stride, alpha}, x, y);
        CHECK(f.rgb == want.rgb);
    }
}

void drawsRectanglesLikeAReference() {
    for (int iter = 0; iter < 2000; ++iter) {
        const int fw = randInt(0, 30), fh = randInt(0, 30);
        const int x = randInt(-40, 40), y = randInt(-40, 40);
        const int w = randInt(-5, 50), h = randInt(-5, 50), thick = randInt(-2, 8);
        Frame f = solidFrame(fw, fh, 0, 0, 0);
        crystal::drawRect(f, x, y, w, h, 255, 1, 2, thick);

        const int t = std::max(1, std::min({thick, w, h}));
        for (int py = 0; py < fh; ++py)
            for (int px = 0; px < fw; ++px) {
                const bool inside = px >= x && px < x + w && py >= y && py < y + h;
                const bool edge = py < y + t || py >= y + h - t || px < x + t || px >= x + w - t;
                CHECK((f.rgb[idx(fw, px, py)] == 255) == (inside && edge));
            }
    }
}

void scalesAnySizeIntoAPaddedTensor() {
    std::vector<float> out;
    for (int iter = 0; iter < 600; ++iter) {
        const int w = randInt(1, 200), h = randInt(1, 200);
        const float scale = iter % 7 == 0 ? 1.0f : randFloat(0.001f, 1.0f);
        const int stride = std::array{1, 4, 32}[static_cast<std::size_t>(randInt(0, 2))];
        const bool solid = iter % 2 == 0;
        const std::uint8_t r = randByte(), g = randByte(), b = randByte();
        const Frame f = solid ? solidFrame(w, h, r, g, b) : randomFrame(w, h);

        const auto in = crystal::scaleToInput(f, scale, stride, out);
        const int sw = std::max(1, static_cast<int>(std::lround(w * double{scale})));
        const int sh = std::max(1, static_cast<int>(std::lround(h * double{scale})));
        CHECK(in.width % stride == 0 && in.width >= sw && in.width < sw + stride);
        CHECK(in.height % stride == 0 && in.height >= sh && in.height < sh + stride);
        CHECK(out.size() ==
              3u * static_cast<std::size_t>(in.width) * static_cast<std::size_t>(in.height));

        const std::size_t plane = out.size() / 3;
        const float want[3] = {r / 255.0f, g / 255.0f, b / 255.0f};
        bool ok = true;
        for (std::size_t c = 0; c < 3 && ok; ++c)
            for (int yy = 0; yy < in.height && ok; ++yy)
                for (int xx = 0; xx < in.width && ok; ++xx) {
                    const float v = out[c * plane + static_cast<std::size_t>(yy * in.width + xx)];
                    if (xx >= sw || yy >= sh)
                        ok = v == 0.0f; // padding
                    else if (solid)
                        ok = std::fabs(v - want[c]) < 1e-4f; // area weights sum to one
                    else
                        ok = v >= 0.0f && v <= 1.0f + 1e-5f;
                }
        CHECK(ok);
    }
    const auto empty = crystal::scaleToInput(Frame{}, 0.5f, 32, out);
    CHECK(empty.width == 0 && empty.height == 0 && out.empty());
}

void letterboxesAnySize() {
    std::vector<float> out;
    for (int iter = 0; iter < 600; ++iter) {
        const int w = randInt(1, 200), h = randInt(1, 200), size = randInt(1, 96);
        const std::uint8_t r = randByte(), g = randByte(), b = randByte();
        crystal::letterbox(solidFrame(w, h, r, g, b), size, out);
        CHECK(out.size() == 3u * static_cast<std::size_t>(size) * static_cast<std::size_t>(size));

        // Where each output pixel samples the padded square (OpenCV INTER_LINEAR).
        const float ratio = static_cast<float>(std::max(w, h)) / static_cast<float>(size);
        auto sample = [&](int d) {
            return std::max((static_cast<float>(d) + 0.5f) * ratio - 0.5f, 0.0f);
        };
        const std::size_t plane = out.size() / 3;
        const float want[3] = {r / 255.0f, g / 255.0f, b / 255.0f};
        bool ok = true;
        for (int yy = 0; yy < size && ok; ++yy)
            for (int xx = 0; xx < size && ok; ++xx) {
                const float sx = sample(xx), sy = sample(yy);
                for (std::size_t c = 0; c < 3; ++c) {
                    const float v = out[c * plane + static_cast<std::size_t>(yy * size + xx)];
                    if (sx >= static_cast<float>(w) || sy >= static_cast<float>(h))
                        ok = ok && v == 0.0f; // only padding
                    else if (sx <= static_cast<float>(w - 1) && sy <= static_cast<float>(h - 1))
                        ok = ok && std::fabs(v - want[c]) < 1e-4f; // only image
                    else
                        ok = ok && v >= 0.0f && v <= want[c] + 1e-4f; // blended with padding
                }
            }
        CHECK(ok);
    }
    CHECK(crystal::letterbox(Frame{}, 640, out).srcWidth == 0);
    crystal::letterbox(solidFrame(4, 4, 1, 2, 3), 0, out);
    CHECK(out.empty());
}

// Model output as it should never be, but a broken model or runtime could produce.
float hostileValue() {
    switch (randInt(0, 9)) {
    case 0:
        return std::numeric_limits<float>::quiet_NaN();
    case 1:
        return randInt(0, 1) ? std::numeric_limits<float>::infinity()
                             : -std::numeric_limits<float>::infinity();
    case 2:
        return randFloat(-1e30f, 1e30f);
    case 3:
        return randFloat(-100, 0);
    default:
        return randFloat(0, 700);
    }
}

void decodesHostileOutputIntoSaneDetections() {
    constexpr std::size_t classes = crystal::kLabels.size();
    for (int iter = 0; iter < 400; ++iter) {
        const std::size_t anchors = static_cast<std::size_t>(randInt(1, 300));
        std::vector<float> out((4 + classes) * anchors);
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = i < 4 * anchors ? hostileValue()
                                     : (randInt(0, 9) == 0 ? hostileValue() : randFloat(0, 1));
        const crystal::Letterbox lb{randFloat(0.05f, 2), randFloat(0.05f, 2), randInt(1, 4000),
                                    randInt(1, 4000)};

        const auto dets = crystal::decode(out, classes, anchors, lb, 0.25f);
        CHECK(dets.size() <= anchors);
        for (const auto& d : dets) {
            CHECK(d.classId >= 0 && d.classId < static_cast<int>(classes));
            CHECK(std::isfinite(d.score) && d.score >= 0.25f);
            CHECK(std::isfinite(d.box.x) && std::isfinite(d.box.y) && std::isfinite(d.box.w) &&
                  std::isfinite(d.box.h));
            CHECK(d.box.x >= 0 && d.box.y >= 0 && d.box.w > 0 && d.box.h > 0);
            CHECK(d.box.x + d.box.w <= static_cast<float>(lb.srcWidth) * 1.0001f);
            CHECK(d.box.y + d.box.h <= static_cast<float>(lb.srcHeight) * 1.0001f);
        }

        const auto kept = crystal::nms(dets, 0.45f);
        CHECK(kept.size() <= dets.size());
        for (std::size_t i = 0; i < kept.size(); ++i) {
            if (i > 0) CHECK(kept[i - 1].score >= kept[i].score);
            for (std::size_t j = 0; j < i; ++j)
                CHECK(crystal::iou(kept[i].box, kept[j].box) <= 0.45f);
        }
    }
    // Too-short buffers and no classes at all decode to nothing (without reading past them).
    std::vector<float> small(4 * 10, 1.0f);
    CHECK(crystal::decode(small, 18, 10, {}, 0.25f).empty());
    CHECK(crystal::decode(small, 0, 10, {}, 0.25f).empty());
}

void rejectsBrokenPpmFiles() {
    const auto path = std::filesystem::temp_directory_path() / "crystalvision_robustness.ppm";
    auto tryRead = [&](const std::string& bytes) {
        std::ofstream{path, std::ios::binary}.write(bytes.data(),
                                                    static_cast<std::streamsize>(bytes.size()));
        Frame f;
        std::string error;
        const bool ok = crystal::readPpm(path, f, error);
        if (ok)
            CHECK(f.width > 0 && f.height > 0 &&
                  f.rgb.size() == static_cast<std::size_t>(f.width) * f.height * 3);
        else
            CHECK(!error.empty());
        return ok;
    };

    const std::string pixels(2 * 3 * 3, '\x7f');
    const std::string good = "P6\n# made by a test\n2 3\n255\n" + pixels;
    CHECK(tryRead(good));
    CHECK(!tryRead(""));
    CHECK(!tryRead("P6"));
    CHECK(!tryRead("P3\n2 3\n255\n" + pixels));
    CHECK(!tryRead("P6\n2 3\n65535\n" + pixels));
    CHECK(!tryRead("P6\n-2 3\n255\n" + pixels));
    CHECK(!tryRead("P6\n0 3\n255\n" + pixels));
    CHECK(!tryRead("P6\n99999999999 3\n255\n" + pixels));
    CHECK(!tryRead("P6\n2 3\n255\n" + pixels.substr(1))); // one byte short

    // A header promising a huge image with no pixels behind it is rejected before the frame
    // is allocated (30000 x 30000 would be 2.7 GB).
    const long before = peakRssKb();
    CHECK(!tryRead("P6\n30000 30000\n255\n" + pixels));
    CHECK(peakRssKb() - before < 100 * 1024);

    // Every truncation and many random corruptions of a valid file: never a crash.
    for (std::size_t len = 0; len < good.size(); ++len) tryRead(good.substr(0, len));
    for (int iter = 0; iter < 500; ++iter) {
        std::string bad = good;
        for (int k = randInt(1, 4); k > 0; --k)
            bad[static_cast<std::size_t>(randInt(0, static_cast<int>(bad.size()) - 1))] =
                static_cast<char>(randByte());
        tryRead(bad);
    }
    std::filesystem::remove(path);
}

void summarisesAnySize() {
    for (int iter = 0; iter < 400; ++iter) {
        const int w = randInt(0, 300), h = randInt(0, 300);
        const int cell = randInt(1, 100), step = randInt(1, 10);
        const std::uint8_t r = randByte(), g = randByte(), b = randByte();
        const bool solid = iter % 2 == 0;
        const Frame f = solid ? solidFrame(w, h, r, g, b) : randomFrame(w, h);

        const auto grid = crystal::summarize(f, cell, step);
        CHECK(grid.cols == (w + cell - 1) / cell && grid.rows == (h + cell - 1) / cell);
        CHECK(grid.cells.size() == static_cast<std::size_t>(grid.cols) * grid.rows);
        for (const auto& c : grid.cells) {
            CHECK(c.luma >= 0 && c.luma <= 255);
            CHECK(c.cb >= -128 && c.cb <= 128 && c.cr >= -128 && c.cr <= 128);
        }
        if (solid && !grid.cells.empty()) {
            // Every cell with a sample sees the same colour (a cell may get no sample when
            // `step` jumps over a narrow last row or column; it reads as zero).
            const auto& first = grid.cells.front();
            for (const auto& c : grid.cells)
                if (c.luma != 0 || c.cb != 0 || c.cr != 0)
                    CHECK(c.luma == first.luma && c.cb == first.cb && c.cr == first.cr);
        }
        const auto same = crystal::compareGrids(grid, grid);
        CHECK(same.luma == 0 && same.chroma == 0);
    }
}

} // namespace

int main() {
    pastesLikeAPixelByPixelReference();
    drawsRectanglesLikeAReference();
    scalesAnySizeIntoAPaddedTensor();
    letterboxesAnySize();
    decodesHostileOutputIntoSaneDetections();
    rejectsBrokenPpmFiles();
    summarisesAnySize();
    return check::finish("robustness_test");
}
