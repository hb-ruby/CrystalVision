// Unit tests for detect.cpp (letterbox, decode, NMS) and the PPM/drawing helpers in
// frame.cpp. No model or X server needed.

#include "detect.hpp"
#include "frame.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

bool near(float a, float b, float tol = 1e-3f) {
    return std::fabs(a - b) <= tol;
}

// NCHW value at channel c, row y, column x of a size x size tensor.
float at(const std::vector<float>& t, int size, int c, int y, int x) {
    return t[static_cast<std::size_t>((c * size + y) * size + x)];
}

crystal::Frame solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    crystal::Frame f;
    f.reset(w, h);
    for (std::size_t i = 0; i < f.rgb.size(); i += 3) {
        f.rgb[i] = r;
        f.rgb[i + 1] = g;
        f.rgb[i + 2] = b;
    }
    return f;
}

void letterboxesWideImage() {
    // 64x32 into 32x32: scaled by 0.5 to 32x16 at the top, black padding below.
    const auto f = solid(64, 32, 255, 0, 51);
    std::vector<float> t;
    const auto lb = crystal::letterbox(f, 32, t);
    CHECK(t.size() == 3u * 32 * 32);
    CHECK(near(lb.scale, 0.5f));

    CHECK(near(at(t, 32, 0, 0, 0), 1.0f));   // R plane, scaled to [0, 1]
    CHECK(near(at(t, 32, 1, 0, 0), 0.0f));   // G plane
    CHECK(near(at(t, 32, 2, 15, 31), 0.2f)); // B plane, last image row
    CHECK(near(at(t, 32, 0, 16, 0), 0));     // padding is black
    CHECK(near(at(t, 32, 2, 31, 31), 0));
}

void letterboxesTallImage() {
    const auto f = solid(10, 40, 10, 20, 30); // 40 tall -> scale 0.5 -> 5x20, padding right
    std::vector<float> t;
    const auto lb = crystal::letterbox(f, 20, t);
    CHECK(near(lb.scale, 0.5f));
    CHECK(near(at(t, 20, 0, 0, 0), 10 / 255.0f));
    CHECK(near(at(t, 20, 1, 19, 4), 20 / 255.0f));
    CHECK(near(at(t, 20, 0, 0, 5), 0));
    CHECK(near(at(t, 20, 2, 19, 19), 0));
}

// Fills each row of `f` with the same values in the red channel.
void redColumns(crystal::Frame& f, const std::vector<std::uint8_t>& v) {
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x)
            f.rgb[static_cast<std::size_t>((y * f.width + x) * 3)] = v[static_cast<std::size_t>(x)];
}

void interpolatesLikeOpenCv() {
    // Columns 0, 100, 200, 255 resized 4 -> 2 wide: OpenCV samples source x = 0.5 and 2.5,
    // giving 50 and 227.5.
    crystal::Frame f;
    f.reset(4, 4);
    redColumns(f, {0, 100, 200, 255});
    std::vector<float> t;
    crystal::letterbox(f, 2, t);
    CHECK(near(at(t, 2, 0, 0, 0), 50 / 255.0f));
    CHECK(near(at(t, 2, 0, 0, 1), 227.5f / 255.0f));
}

void blendsLastColumnWithPadding() {
    // 3x4 is padded to 4x4 with a black column; sampling x = 2.5 mixes column 2 with that
    // padding, as OpenCV does when resizing the padded square: 200 * 0.5 = 100.
    crystal::Frame f;
    f.reset(3, 4);
    redColumns(f, {0, 100, 200});
    std::vector<float> t;
    crystal::letterbox(f, 2, t);
    CHECK(near(at(t, 2, 0, 0, 0), 50 / 255.0f));
    CHECK(near(at(t, 2, 0, 1, 1), 100 / 255.0f));
}

void mapsBoxesBackToSource() {
    // 1920x1080 into 640: scale 1/3, image in the top 360 rows.
    crystal::Letterbox lb{1.0f / 3, 1920, 1080};
    const auto b = crystal::unletterbox({100, 100, 30, 60}, lb);
    CHECK(near(b.x, 300, 0.01f) && near(b.y, 300, 0.01f));
    CHECK(near(b.w, 90, 0.01f) && near(b.h, 180, 0.01f));

    const auto clipped = crystal::unletterbox({630, -10, 20, 60}, lb); // overhangs right/top
    CHECK(near(clipped.x, 1890, 0.01f) && near(clipped.w, 30, 0.01f));
    CHECK(near(clipped.y, 0) && near(clipped.h, 150, 0.01f));
}

void decodesOutputTensor() {
    // 3 classes, 3 anchors, identity letterbox. Layout: rows of `anchors` values.
    crystal::Letterbox lb{1, 100, 100};
    const std::vector<float> out = {
        50,   20,   10,   // cx
        50,   20,   10,   // cy
        20,   10,   4,    // w
        40,   10,   4,    // h
        0.1f, 0.9f, 0.1f, // class 0
        0.8f, 0.0f, 0.2f, // class 1
        0.3f, 0.0f, 0.1f, // class 2
    };
    const auto dets = crystal::decode(out, 3, 3, lb, 0.25f);
    CHECK(dets.size() == 2); // anchor 2's best score 0.2 is below the threshold
    if (dets.size() == 2) {
        CHECK(dets[0].classId == 1 && near(dets[0].score, 0.8f));
        CHECK(near(dets[0].box.x, 40) && near(dets[0].box.y, 30));
        CHECK(near(dets[0].box.w, 20) && near(dets[0].box.h, 40));
        CHECK(dets[1].classId == 0 && near(dets[1].score, 0.9f));
    }
    CHECK(crystal::decode(std::span<const float>{out}.first(10), 3, 3, lb, 0.25f).empty());
}

void suppressesOverlaps() {
    CHECK(near(crystal::iou({0, 0, 10, 10}, {0, 0, 10, 10}), 1));
    CHECK(near(crystal::iou({0, 0, 10, 10}, {5, 0, 10, 10}), 50.0f / 150));
    CHECK(near(crystal::iou({0, 0, 10, 10}, {20, 20, 5, 5}), 0));

    std::vector<crystal::Detection> dets = {
        {0, 0.5f, {1, 0, 10, 10}}, // overlaps the 0.9 box heavily (IoU ~0.82): dropped
        {2, 0.9f, {0, 0, 10, 10}},
        {1, 0.7f, {5, 0, 10, 10}}, // IoU 0.33 with the 0.9 box: kept, even across classes
        {3, 0.3f, {50, 50, 5, 5}},
    };
    const auto kept = crystal::nms(dets, 0.45f);
    CHECK(kept.size() == 3);
    if (kept.size() == 3) {
        CHECK(near(kept[0].score, 0.9f) && near(kept[1].score, 0.7f) && near(kept[2].score, 0.3f));
    }
}

void roundTripsPpm() {
    auto f = solid(5, 3, 1, 2, 3);
    f.rgb[7] = 200;
    const auto path = std::filesystem::temp_directory_path() / "crystalvision_detect_test.ppm";
    CHECK(crystal::writePpm(f, path));
    crystal::Frame g;
    std::string error;
    CHECK(crystal::readPpm(path, g, error));
    CHECK(g.width == 5 && g.height == 3 && g.rgb == f.rgb);
    std::filesystem::remove(path);

    CHECK(!crystal::readPpm(path, g, error)); // gone now
}

void drawsClippedRectangles() {
    auto f = solid(6, 6, 0, 0, 0);
    crystal::drawRect(f, 1, 1, 4, 4, 255, 0, 0, 1);
    auto red = [&](int x, int y) {
        return f.rgb[static_cast<std::size_t>((y * 6 + x) * 3)] == 255;
    };
    CHECK(red(1, 1) && red(4, 1) && red(1, 4) && red(4, 4) && red(2, 1));
    CHECK(!red(2, 2) && !red(0, 0) && !red(5, 5));
    crystal::drawRect(f, -3, -3, 20, 20, 255, 0, 0, 2); // mostly off-frame: no crash
}

} // namespace

int main() {
    letterboxesWideImage();
    letterboxesTallImage();
    interpolatesLikeOpenCv();
    blendsLastColumnWithPadding();
    mapsBoxesBackToSource();
    decodesOutputTensor();
    suppressesOverlaps();
    roundTripsPpm();
    drawsClippedRectangles();
    if (failures == 0) std::printf("detect_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
