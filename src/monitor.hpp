#pragma once

#include "capture.hpp"
#include "change.hpp"
#include "detect.hpp"
#include "detector.hpp"
#include "frame.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
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
// confirms it's gone. Detections that shouldBlock() rejects (wrong class, or a score below
// kBlockScore) are ignored.
//
// A detection that overlaps a block held from a sharper tick (IoU above kMergeIou) is the
// same area seen again, so instead of adding a second block it grows the held one to cover
// both boxes. The block keeps the sharper scale, so it is held just as long as before.
inline constexpr float kMergeIou = 0.45f; // the same overlap NMS treats as one object

void updateBlocks(std::vector<Block>& blocks, const std::vector<Detection>& detections,
                  float scale);

// Which scan schedule the monitor is on.
enum class ScanState {
    Calm,  // the default: a cheap scan every few seconds
    Alert, // something suggestive was seen: sharper scans, close together
};

const char* toString(ScanState s);

// Timing of the Calm and Alert states.
struct ScheduleOptions {
    std::chrono::milliseconds calmGap{5000};    // wait after each Calm scan finishes
    std::chrono::milliseconds alertGap{500};    // wait after each Alert scan finishes
    std::chrono::milliseconds alertHold{30000}; // Alert lasts this long after the last trigger
    int calmFullEvery = 6;                      // every Nth Calm scan runs at full resolution
};

// Decides the scale of each scan and the wait after it, switching between Calm and Alert.
// Pure: the caller passes in when each scan finished, so it can be tested without a clock.
//
//   Calm:  0.333 scans, every calmFullEvery-th at 1.0 so small images (thumbnail grids)
//          still get caught; calmGap after each.
//   Alert: entered when a scan finds anything triggersAlert() accepts. Scans alternate 1.0
//          and 0.5, starting with 1.0; alertGap after each.
//   Back to Calm once alertHold has passed since the last trigger and no blocks are held.
//          Calm's 0.333 scans can't clear blocks found by sharper scans (see updateBlocks), so
//          leaving with blocks still held would leave stale boxes on screen. Every block is
//          also a trigger, so this means a 1.0 scan has cleared every block found at 1.0.
class Scheduler {
  public:
    using Clock = std::chrono::steady_clock;

    explicit Scheduler(ScheduleOptions opts = {}) : opts_{opts} {}

    ScanState state() const { return state_; }

    // The scale for the next scan.
    float nextScale() const;

    // Records a scan at nextScale() that finished at `finished`, found a trigger or not, and
    // left `blocksHeld` blocks. Updates the state for the next scan.
    void record(bool triggered, std::size_t blocksHeld, Clock::time_point finished);

    // How long to wait before the next scan, given the current state.
    std::chrono::milliseconds gap() const {
        return state_ == ScanState::Calm ? opts_.calmGap : opts_.alertGap;
    }

  private:
    ScheduleOptions opts_;
    ScanState state_ = ScanState::Calm;
    std::uint64_t calmScans_ = 0;  // scans run since entering Calm
    std::uint64_t alertScans_ = 0; // scans run since entering Alert
    Clock::time_point lastTrigger_{};
};

// The continuous monitoring loop's work, one tick at a time. Each tick captures the whole
// screen and runs the detector on it, at the scale the Scheduler picks, except that a Calm
// tick skips the detector if the screen hasn't changed (see ChangeGate). Waiting between ticks
// is the caller's job (for Tick::gap), so the wait can start only once a tick has finished
// and can be cut short by a signal.
class Monitor {
  public:
    struct Tick {
        std::uint64_t number = 0; // 1 for the first tick
        float scale = 0;
        ScanState state{};                // the state this tick ran in
        ScanState next{};                 // the state for the next tick
        std::chrono::milliseconds gap{};  // how long to wait before the next tick
        bool triggered = false;           // found something that triggers Alert
        bool skipped = false;             // unchanged Calm screen: no detection ran
        std::optional<GridChange> change; // against the gate's reference, if it has one
        Capturer::Stats capture;
        double captureMs = 0;
        double changeMs = 0;
        Detector::Timing timing;           // includes the model input size used
        std::vector<Detection> detections; // in screen pixels, highest score first
        std::vector<Block> blocks;         // what should be covered now (see updateBlocks)
    };

    // Both must outlive the monitor.
    Monitor(Capturer& capturer, Detector& detector, ScheduleOptions schedule = {},
            ChangeGate::Options change = {})
        : capturer_{capturer}, detector_{detector}, scheduler_{schedule}, gate_{change} {}

    // Runs the next tick. Throws whatever capture or detection throws.
    Tick tick();

  private:
    Capturer& capturer_;
    Detector& detector_;
    Scheduler scheduler_;
    ChangeGate gate_;
    Frame frame_; // reused between ticks
    std::uint64_t ticks_ = 0;
    std::vector<Block> blocks_;
};

} // namespace crystal
