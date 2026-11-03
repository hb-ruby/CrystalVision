#include "monitor.hpp"

#include <algorithm>
#include <chrono>

namespace crystal {

float tickScale(std::uint64_t number) {
    if (number % 10 == 0) return 1.0f;
    if (number % 5 == 0) return 0.5f;
    return 0.333f;
}

void updateBlocks(std::vector<Block>& blocks, const std::vector<Detection>& detections,
                  float scale) {
    std::erase_if(blocks, [&](const Block& b) { return b.scale <= scale; });
    for (const Detection& d : detections)
        if (shouldBlock(d.classId)) blocks.push_back({d.box, scale});
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
