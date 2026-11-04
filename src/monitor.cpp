#include "monitor.hpp"

#include <algorithm>
#include <chrono>

namespace crystal {

const char* toString(ScanState s) {
    return s == ScanState::Calm ? "calm" : "alert";
}

float Scheduler::nextScale() const {
    if (state_ == ScanState::Alert) return alertScans_ % 2 == 0 ? 1.0f : 0.5f;
    const auto every = static_cast<std::uint64_t>(opts_.calmFullEvery);
    return (calmScans_ + 1) % every == 0 ? 1.0f : 0.333f;
}

void Scheduler::record(bool triggered, std::size_t blocksHeld, Clock::time_point finished) {
    if (triggered) lastTrigger_ = finished;

    if (state_ == ScanState::Calm) {
        ++calmScans_;
        if (triggered) {
            state_ = ScanState::Alert;
            alertScans_ = 0;
        }
        return;
    }
    ++alertScans_;
    if (!triggered && blocksHeld == 0 && finished - lastTrigger_ >= opts_.alertHold) {
        state_ = ScanState::Calm;
        calmScans_ = 0;
    }
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
        if (!shouldBlock(d)) continue;
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
    t.state = scheduler_.state();
    t.scale = scheduler_.nextScale();

    const auto start = std::chrono::steady_clock::now();
    t.capture = capturer_.capture(frame_);
    t.captureMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    // Summarised on every tick, Alert ones too, so the gate always compares with the screen
    // the last scan actually saw.
    const auto summarized = std::chrono::steady_clock::now();
    const ScreenGrid grid = summarize(frame_);
    t.changeMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - summarized)
            .count();
    t.change = gate_.change(grid, t.scale);

    if (t.state == ScanState::Calm && gate_.canSkip(grid, t.scale, start)) {
        t.skipped = true; // blocks stay as they were
    } else {
        t.detections = detector_.detect(frame_, t.scale, &t.timing);
        updateBlocks(blocks_, t.detections, t.scale);
        gate_.ran(grid, t.scale, start);
    }
    t.blocks = blocks_;

    t.triggered =
        std::ranges::any_of(t.detections, [](const Detection& d) { return triggersAlert(d); });
    // A skipped scan still counts toward Calm's every-6th full scan.
    scheduler_.record(t.triggered, blocks_.size(), std::chrono::steady_clock::now());
    t.next = scheduler_.state();
    t.gap = scheduler_.gap();
    return t;
}

} // namespace crystal
