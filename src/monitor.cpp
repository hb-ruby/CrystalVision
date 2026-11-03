#include "monitor.hpp"

#include <chrono>

namespace crystal {

float tickScale(std::uint64_t number) {
    if (number % 10 == 0) return 1.0f;
    if (number % 5 == 0) return 0.5f;
    return 0.333f;
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
    return t;
}

} // namespace crystal
