#pragma once

#include "capture.hpp"
#include "detect.hpp"
#include "detector.hpp"
#include "frame.hpp"

#include <cstdint>
#include <vector>

namespace crystal {

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
};

// The detector scale for tick `number` (1-based): every 10th tick runs at full resolution,
// every other 5th tick at half, and the rest at a third. Cheap scans most of the time, with
// a sharper look every few seconds to catch what the small ones miss.
float tickScale(std::uint64_t number);

} // namespace crystal
