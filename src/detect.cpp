#include "detect.hpp"

#include <algorithm>
#include <cmath>

namespace crystal {

namespace {

// One output pixel's two source neighbours along an axis, with their weights.
struct Tap {
    int i0, i1;
    float w0, w1;
};

// OpenCV INTER_LINEAR sampling from an axis of `paddedLen` pixels down to `dstLen`, where
// only the first `srcLen` pixels are image and the rest is black padding (weight 0).
// Returns the taps for the output pixels that touch the image; the rest are pure padding.
std::vector<Tap> taps(int srcLen, int paddedLen, int dstLen) {
    std::vector<Tap> t;
    const float ratio = static_cast<float>(paddedLen) / static_cast<float>(dstLen);
    for (int d = 0; d < dstLen; ++d) {
        const float s = std::max((static_cast<float>(d) + 0.5f) * ratio - 0.5f, 0.0f);
        const int i0 = std::min(static_cast<int>(s), paddedLen - 1);
        if (i0 >= srcLen) break; // from here on, only padding
        const int i1 = std::min(i0 + 1, paddedLen - 1);
        const float f = s - static_cast<float>(i0);
        if (i1 < srcLen)
            t.push_back({i0, i1, 1 - f, f});
        else
            t.push_back({i0, i0, 1 - f, 0}); // the second neighbour is black padding
    }
    return t;
}

} // namespace

Letterbox letterbox(const Frame& src, int size, std::vector<float>& out) {
    Letterbox lb;
    lb.srcWidth = src.width;
    lb.srcHeight = src.height;

    const std::size_t plane = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    out.assign(3 * plane, 0.0f); // black padding
    if (src.width <= 0 || src.height <= 0 || size <= 0) return lb;

    const int side = std::max(src.width, src.height); // the padded square
    lb.scale = static_cast<float>(size) / static_cast<float>(side);

    const std::vector<Tap> xs = taps(src.width, side, size);
    const std::vector<Tap> ys = taps(src.height, side, size);
    const std::size_t srcStride = static_cast<std::size_t>(src.width) * 3;
    float* const planes[3] = {out.data(), out.data() + plane, out.data() + 2 * plane};

    for (std::size_t dy = 0; dy < ys.size(); ++dy) {
        const Tap& ty = ys[dy];
        const std::uint8_t* row0 = src.rgb.data() + static_cast<std::size_t>(ty.i0) * srcStride;
        const std::uint8_t* row1 = src.rgb.data() + static_cast<std::size_t>(ty.i1) * srcStride;
        const std::size_t outRow = dy * static_cast<std::size_t>(size);

        for (std::size_t dx = 0; dx < xs.size(); ++dx) {
            const Tap& tx = xs[dx];
            const std::size_t a = static_cast<std::size_t>(tx.i0) * 3;
            const std::size_t b = static_cast<std::size_t>(tx.i1) * 3;
            for (std::size_t c = 0; c < 3; ++c) {
                const float top = row0[a + c] * tx.w0 + row0[b + c] * tx.w1;
                const float bot = row1[a + c] * tx.w0 + row1[b + c] * tx.w1;
                planes[c][outRow + dx] = (top * ty.w0 + bot * ty.w1) * (1.0f / 255.0f);
            }
        }
    }
    return lb;
}

Box unletterbox(const Box& in, const Letterbox& lb) {
    const auto w = static_cast<float>(lb.srcWidth);
    const auto h = static_cast<float>(lb.srcHeight);
    const float x0 = std::clamp(in.x / lb.scale, 0.0f, w);
    const float y0 = std::clamp(in.y / lb.scale, 0.0f, h);
    const float x1 = std::clamp((in.x + in.w) / lb.scale, 0.0f, w);
    const float y1 = std::clamp((in.y + in.h) / lb.scale, 0.0f, h);
    return {x0, y0, x1 - x0, y1 - y0};
}

std::vector<Detection> decode(std::span<const float> output, std::size_t classes,
                              std::size_t anchors, const Letterbox& lb, float minScore) {
    std::vector<Detection> dets;
    if (output.size() < (4 + classes) * anchors) return dets;

    // Row r holds attribute r for every anchor, so anchor i's value is row(r)[i].
    auto row = [&](std::size_t r) { return output.data() + r * anchors; };

    for (std::size_t i = 0; i < anchors; ++i) {
        std::size_t best = 0;
        float bestScore = row(4)[i];
        for (std::size_t c = 1; c < classes; ++c) {
            if (const float s = row(4 + c)[i]; s > bestScore) {
                bestScore = s;
                best = c;
            }
        }
        if (bestScore < minScore) continue;

        const float cx = row(0)[i], cy = row(1)[i], w = row(2)[i], h = row(3)[i];
        const Box box = unletterbox({cx - w / 2, cy - h / 2, w, h}, lb);
        if (box.w <= 0 || box.h <= 0) continue; // entirely in the padding
        dets.push_back({static_cast<int>(best), bestScore, box});
    }
    return dets;
}

float iou(const Box& a, const Box& b) {
    const float ix = std::max(0.0f, std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x));
    const float iy = std::max(0.0f, std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y));
    const float inter = ix * iy;
    const float uni = a.w * a.h + b.w * b.h - inter;
    return uni > 0 ? inter / uni : 0.0f;
}

std::vector<Detection> nms(std::vector<Detection> dets, float iouThreshold) {
    std::stable_sort(dets.begin(), dets.end(),
                     [](const Detection& a, const Detection& b) { return a.score > b.score; });
    std::vector<Detection> kept;
    for (const Detection& d : dets) {
        const bool overlaps = std::any_of(kept.begin(), kept.end(), [&](const Detection& k) {
            return iou(d.box, k.box) > iouThreshold;
        });
        if (!overlaps) kept.push_back(d);
    }
    return kept;
}

} // namespace crystal
