#pragma once

// Pre- and post-processing around the NudeNet v3 detector. Pure functions, no ONNX
// Runtime, so they can be unit-tested.
//
// It follows NudeNet's own Python pipeline (nudenet.py, v3 branch) so results can be compared:
//   letterbox: pad the image with black on the right or bottom to make it square (the
//              image stays at the top-left corner), then resize that square to the input
//              size with bilinear interpolation
//   input:     float32 RGB in [0, 1], layout NCHW = [1, 3, size, size]. One deliberate
//              difference: nudenet.py feeds BGR by accident (a double channel swap); the
//              model was trained on RGB. See scripts/compare-nudenet.py.
//   output:    [1, 4 + classes, anchors]; per anchor: centre x, centre y, width, height
//              (in input pixels), then one score per class
//   NMS:       class-agnostic, keep score > 0.25, IoU threshold 0.45

#include "frame.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace crystal {

// NudeNet v3 class labels, in the model's output order.
inline constexpr std::array<std::string_view, 18> kLabels = {
    "FEMALE_GENITALIA_COVERED",
    "FACE_FEMALE",
    "BUTTOCKS_EXPOSED",
    "FEMALE_BREAST_EXPOSED",
    "FEMALE_GENITALIA_EXPOSED",
    "MALE_BREAST_EXPOSED",
    "ANUS_EXPOSED",
    "FEET_EXPOSED",
    "BELLY_COVERED",
    "FEET_COVERED",
    "ARMPITS_COVERED",
    "ARMPITS_EXPOSED",
    "FACE_MALE",
    "BELLY_EXPOSED",
    "MALE_GENITALIA_EXPOSED",
    "ANUS_COVERED",
    "FEMALE_BREAST_COVERED",
    "BUTTOCKS_COVERED",
};

// Whether a detection of this class gets covered: the explicit *_EXPOSED labels. The other
// exposed ones (feet, armpits, belly, male breast) are ordinary clothing, and the covered and
// face labels are only context. The same set as SCORED in scripts/scale-accuracy.py.
constexpr bool shouldBlock(int classId) {
    switch (classId) {
    case 2:  // BUTTOCKS_EXPOSED
    case 3:  // FEMALE_BREAST_EXPOSED
    case 4:  // FEMALE_GENITALIA_EXPOSED
    case 6:  // ANUS_EXPOSED
    case 14: // MALE_GENITALIA_EXPOSED
        return true;
    default:
        return false;
    }
}

// A box in source-image pixels: top-left corner plus size.
struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};

struct Detection {
    int classId = 0; // index into kLabels
    float score = 0; // 0..1
    Box box;
};

// The lowest score that gets a detection covered. Higher than the detector's own 0.25 cutoff
// (Detector::Options::minScore, kept at NudeNet's value so its output still matches Python
// NudeNet): on the local test set, 0.5 drops weak false positives while missing few nude images.
inline constexpr float kBlockScore = 0.5f;

// Whether this detection gets covered: a blocked class, scored at least kBlockScore.
constexpr bool shouldBlock(const Detection& d) {
    return d.score >= kBlockScore && shouldBlock(d.classId);
}

// The lowest score at which a detection puts the monitor into Alert (faster, sharper scans).
// Lower than kBlockScore: the trigger only speeds up scanning, so it errs sensitive.
inline constexpr float kAlertScore = 0.3f;

// The covered labels that hint at nudity nearby without being covered themselves.
constexpr bool isWarning(int classId) {
    switch (classId) {
    case 0:  // FEMALE_GENITALIA_COVERED
    case 15: // ANUS_COVERED
    case 16: // FEMALE_BREAST_COVERED
    case 17: // BUTTOCKS_COVERED
        return true;
    default:
        return false;
    }
}

// Whether this detection puts the monitor into Alert: a blocked or warning class, scored at
// least kAlertScore. Every detection shouldBlock() accepts also triggers Alert.
constexpr bool triggersAlert(const Detection& d) {
    return d.score >= kAlertScore && (shouldBlock(d.classId) || isWarning(d.classId));
}

// How the source image was placed into the model input: scaled by `scaleX` and `scaleY`
// (equal, or nearly so after rounding to whole pixels), with its top-left corner at the
// input's top-left corner and black padding to the right and below.
struct Letterbox {
    float scaleX = 1; // input pixels per source pixel, horizontally
    float scaleY = 1; // and vertically
    int srcWidth = 0;
    int srcHeight = 0;
};

// A rectangular model input made by scaleToInput.
struct ScaledInput {
    Letterbox lb;
    int width = 0;  // tensor width, a multiple of the requested stride
    int height = 0; // tensor height
};

// Scales `src` into a `size` x `size` NCHW float tensor, as if it were first padded with
// black to a square and that square resized with OpenCV's INTER_LINEAR. `out` is resized
// to 3 * size * size.
Letterbox letterbox(const Frame& src, int size, std::vector<float>& out);

// Shrinks `src` by `scale` (0 < scale <= 1) to round(width * scale) x round(height * scale)
// by area averaging (OpenCV INTER_AREA), pads it with black on the right and bottom up to a
// multiple of `stride` in each direction, and writes it into `out` as an NCHW float tensor.
// No square letterbox: a 16:9 screen stays 16:9, so no input pixels are wasted on padding.
// This is what scripts/scale-accuracy.py measures. Throws std::invalid_argument on a bad
// scale or stride.
ScaledInput scaleToInput(const Frame& src, float scale, int stride, std::vector<float>& out);

// Maps a box from model-input pixels back to source pixels, clipped to the source.
Box unletterbox(const Box& inputBox, const Letterbox& lb);

// Turns the raw output tensor into candidate detections in source pixels.
// `output` holds (4 + classes) rows of `anchors` floats each. Anchors whose best class
// score is below `minScore` are dropped.
std::vector<Detection> decode(std::span<const float> output, std::size_t classes,
                              std::size_t anchors, const Letterbox& lb, float minScore);

float iou(const Box& a, const Box& b);

// Greedy non-maximum suppression across all classes: keeps the highest-scoring box and
// drops any box overlapping a kept one by more than `iouThreshold`. Sorted by score.
std::vector<Detection> nms(std::vector<Detection> dets, float iouThreshold);

} // namespace crystal
