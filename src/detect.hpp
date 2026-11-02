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

// A box in source-image pixels: top-left corner plus size.
struct Box {
    float x = 0, y = 0, w = 0, h = 0;
};

struct Detection {
    int classId = 0; // index into kLabels
    float score = 0; // 0..1
    Box box;
};

// How the source image was placed into the square model input: scaled by `scale`, with
// its top-left corner at the input's top-left corner.
struct Letterbox {
    float scale = 1; // input pixels per source pixel
    int srcWidth = 0;
    int srcHeight = 0;
};

// Scales `src` into a `size` x `size` NCHW float tensor, as if it were first padded with
// black to a square and that square resized with OpenCV's INTER_LINEAR. `out` is resized
// to 3 * size * size.
Letterbox letterbox(const Frame& src, int size, std::vector<float>& out);

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
