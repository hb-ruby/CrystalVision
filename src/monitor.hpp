#pragma once

#include "capture.hpp"
#include "detect.hpp"
#include "detector.hpp"
#include "frame.hpp"

#include <cstdint>
#include <vector>

namespace crystal {

// A screen area being covered, and the scale of the scan that found it.
struct Block {
    Box box;
    float scale = 0;
};

// Updates the covered areas after a tick at `scale` found `detections`. A tick only clears
// blocks found at its own scale or a coarser one: a cheap 0.333 tick that misses something
// the last full-resolution tick found leaves it covered until the next tick at least as sharp
// confirms it's gone. Detections of classes that shouldBlock() rejects are ignored.
void updateBlocks(std::vector<Block>& blocks, const std::vector<Detection>& detections,
                  float scale);

// The continuous monitoring loop's work, one tick at a time. Each tick captures the whole
// screen and runs the detector on it, at a scale that depends on the tick number (see
// tickScale). Waiting between ticks is the caller's job, so the wait can start only once a
// tick has finished and can be cut short by a signal.
class Monitor {
  public:
    struct Tick {
        std::uint64_t number = 0; // 1 for the first tick
        float scale = 0;
        Capturer::Stats capture;
        double captureMs = 0;
        Detector::Timing timing;           // includes the model input size used
        std::vector<Detection> detections; // in screen pixels, highest score first
        std::vector<Block> blocks;         // what should be covered now (see updateBlocks)
    };

    // Both must outlive the monitor.
    Monitor(Capturer& capturer, Detector& detector) : capturer_{capturer}, detector_{detector} {}

    // Runs the next tick. Throws whatever capture or detection throws.
    Tick tick();

  private:
    Capturer& capturer_;
    Detector& detector_;
    Frame frame_; // reused between ticks
    std::uint64_t ticks_ = 0;
    std::vector<Block> blocks_;
};

// The detector scale for tick `number` (1-based): every 10th tick runs at full resolution,
// every other 5th tick at half, and the rest at a third. Cheap scans most of the time, with
// a sharper look every few seconds to catch what the small ones miss.
float tickScale(std::uint64_t number);

} // namespace crystal
