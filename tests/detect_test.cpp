// Unit tests for detect.cpp (letterbox, decode, NMS) and the PPM/drawing helpers in
// frame.cpp. No model or X server needed.

#include "check.hpp"
#include "detect.hpp"
#include "frame.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

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
    CHECK(near(lb.scaleX, 0.5f) && near(lb.scaleY, 0.5f));

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
    CHECK(near(lb.scaleX, 0.5f) && near(lb.scaleY, 0.5f));
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

// NCHW value of a w x h tensor.
float at(const std::vector<float>& t, int w, int h, int c, int y, int x) {
    return t[static_cast<std::size_t>((c * h + y) * w + x)];
}

void scalesScreenToRectangularInput() {
    // 1920x1080 at 0.333: 639x360 of image, padded up to 640x384 (multiples of 32).
    const auto f = solid(1920, 1080, 255, 0, 51);
    std::vector<float> t;
    const auto in = crystal::scaleToInput(f, 0.333f, 32, t);
    CHECK(in.width == 640 && in.height == 384);
    CHECK(t.size() == 3u * 640 * 384);
    CHECK(near(in.lb.scaleX, 639.0f / 1920) && near(in.lb.scaleY, 360.0f / 1080));
    CHECK(near(at(t, 640, 384, 0, 0, 0), 1.0f));
    CHECK(near(at(t, 640, 384, 2, 359, 638), 0.2f)); // last image pixel
    CHECK(near(at(t, 640, 384, 0, 359, 639), 0));    // padding column
    CHECK(near(at(t, 640, 384, 0, 360, 0), 0));      // padding row

    const auto full = crystal::scaleToInput(f, 1.0f, 32, t);
    CHECK(full.width == 1920 && full.height == 1088);
    CHECK(near(full.lb.scaleX, 1) && near(full.lb.scaleY, 1));
}

void averagesAreasLikeOpenCv() {
    // Shrinking by 3: each output pixel is the mean of a 3x3 block.
    crystal::Frame f;
    f.reset(6, 3);
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 3; ++y)
            f.rgb[static_cast<std::size_t>((y * 6 + x) * 3)] =
                static_cast<std::uint8_t>(x * 30 + y);
    std::vector<float> t;
    const auto in = crystal::scaleToInput(f, 1.0f / 3, 4, t);
    CHECK(in.width == 4 && in.height == 4);
    CHECK(near(at(t, 4, 4, 0, 0, 0), 31 / 255.0f));  // mean of 0,30,60 plus rows 0,1,2
    CHECK(near(at(t, 4, 4, 0, 0, 1), 121 / 255.0f)); // mean of 90,120,150 plus rows
    CHECK(near(at(t, 4, 4, 0, 0, 2), 0));            // padding

    // Shrinking 3 -> 2 (ratio 1.5): the middle pixel is split between both outputs,
    // as cv2.resize(..., INTER_AREA) does: (0 + 90 / 2) / 1.5 and (90 / 2 + 180) / 1.5.
    f.reset(3, 1);
    f.rgb[0] = 0;
    f.rgb[3] = 90;
    f.rgb[6] = 180;
    crystal::scaleToInput(f, 2.0f / 3, 1, t);
    CHECK(near(at(t, 2, 1, 0, 0, 0), 30 / 255.0f));
    CHECK(near(at(t, 2, 1, 0, 0, 1), 150 / 255.0f));

    bool threw = false;
    try {
        crystal::scaleToInput(f, 1.5f, 32, t);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void mapsBoxesBackToSource() {
    // 1920x1080 into 640: scale 1/3, image in the top 360 rows.
    crystal::Letterbox lb{1.0f / 3, 1.0f / 3, 1920, 1080};
    const auto b = crystal::unletterbox({100, 100, 30, 60}, lb);
    CHECK(near(b.x, 300, 0.01f) && near(b.y, 300, 0.01f));
    CHECK(near(b.w, 90, 0.01f) && near(b.h, 180, 0.01f));

    const auto clipped = crystal::unletterbox({630, -10, 20, 60}, lb); // overhangs right/top
    CHECK(near(clipped.x, 1890, 0.01f) && near(clipped.w, 30, 0.01f));
    CHECK(near(clipped.y, 0) && near(clipped.h, 150, 0.01f));
}

void decodesOutputTensor() {
    // 3 classes, 3 anchors, identity letterbox. Layout: rows of `anchors` values.
    crystal::Letterbox lb{1, 1, 100, 100};
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
    scalesScreenToRectangularInput();
    averagesAreasLikeOpenCv();
    mapsBoxesBackToSource();
    decodesOutputTensor();
    suppressesOverlaps();
    roundTripsPpm();
    drawsClippedRectangles();
    return check::finish("detect_test");
}
