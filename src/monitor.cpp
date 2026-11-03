#include "monitor.hpp"

#include <algorithm>
#include <chrono>

namespace crystal {

float tickScale(std::uint64_t number) {
    if (number % 10 == 0) return 1.0f;
    if (number % 5 == 0) return 0.5f;
    return 0.333f;
}

namespace {

// The smallest box that contains both.
Box unite(const Box& a, const Box& b) {
    const float x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const float x1 = std::max(a.x + a.w, b.x + b.w), y1 = std::max(a.y + a.h, b.y + b.h);
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace

void updateBlocks(std::vector<Block>& blocks, const std::vector<Detection>& detections,
                  float scale) {
    std::erase_if(blocks, [&](const Block& b) { return b.scale <= scale; });
    for (const Detection& d : detections) {
        if (!shouldBlock(d.classId)) continue;
        // Only blocks held from sharper ticks are left to match: NMS already keeps this
        // tick's own detections apart.
        auto held = std::ranges::find_if(blocks, [&](const Block& b) {
            return b.scale > scale && iou(b.box, d.box) > kMergeIou;
        });
        if (held != blocks.end())
            held->box = unite(held->box, d.box); // same area: grow it, keep the sharper hold
        else
            blocks.push_back({d.box, scale});
    }
}

Monitor::Tick Monitor::tick() {
    Tick t;
    t.number = ++ticks_;
    t.scale = tickScale(t.number);

    const auto start = std::chrono::steady_clock::now();
    t.capture = capturer_.capture(frame_);
    t.captureMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    t.detections = detector_.detect(frame_, t.scale, &t.timing);
    updateBlocks(blocks_, t.detections, t.scale);
    t.blocks = blocks_;
    return t;
}

} // namespace crystal
