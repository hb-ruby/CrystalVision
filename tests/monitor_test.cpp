// Unit tests for the monitoring loop's Calm/Alert schedule and block holding. No model or X server
// needed.

#include "check.hpp"
#include "monitor.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr int kBreast = 3; // FEMALE_BREAST_EXPOSED
constexpr int kFace = 1;   // FACE_FEMALE

crystal::Detection det(int classId, float x) {
    return {classId, 0.9f, {x, 0, 10, 10}};
}

void blocksOnlyExposedLabels() {
    std::vector<crystal::Block> blocks;
    crystal::updateBlocks(blocks, {det(kBreast, 0), det(kFace, 50)}, 0.333f);
    CHECK(blocks.size() == 1);
    if (!blocks.empty()) CHECK(blocks[0].box.x == 0 && blocks[0].scale == 0.333f);
    for (int id : {2, 3, 4, 6, 14}) CHECK(crystal::shouldBlock(id));
    for (int id : {0, 1, 5, 7, 11, 13}) CHECK(!crystal::shouldBlock(id));
}

void blocksOnlyConfidentDetections() {
    std::vector<crystal::Block> blocks;
    crystal::updateBlocks(blocks,
                          {{kBreast, crystal::kBlockScore, {0, 0, 10, 10}},
                           {kBreast, crystal::kBlockScore - 0.01f, {50, 0, 10, 10}}},
                          0.333f);
    CHECK(blocks.size() == 1);
    if (!blocks.empty()) CHECK(blocks[0].box.x == 0);
}

void holdsSharperFindsThroughCoarserTicks() {
    std::vector<crystal::Block> blocks;
    crystal::updateBlocks(blocks, {det(kBreast, 100)}, 1.0f); // found by a full scan

    crystal::updateBlocks(blocks, {}, 0.333f); // cheap ticks miss it: still covered
    crystal::updateBlocks(blocks, {det(kBreast, 0)}, 0.333f);
    CHECK(blocks.size() == 2);
    crystal::updateBlocks(blocks, {}, 0.5f); // half scale is still coarser than full
    CHECK(blocks.size() == 1 && blocks[0].box.x == 100);

    crystal::updateBlocks(blocks, {}, 1.0f); // the next full scan doesn't see it: cleared
    CHECK(blocks.empty());

    crystal::updateBlocks(blocks, {det(kBreast, 0)}, 0.333f); // cheap finds are per tick
    crystal::updateBlocks(blocks, {}, 0.333f);
    CHECK(blocks.empty());
}

void mergesRepeatFindsIntoHeldBlocks() {
    std::vector<crystal::Block> blocks;
    crystal::updateBlocks(blocks, {det(kBreast, 100)}, 1.0f);

    // Coarser ticks find the same area slightly shifted: one block, grown to cover both.
    for (int i = 0; i < 3; ++i) crystal::updateBlocks(blocks, {det(kBreast, 102)}, 0.333f);
    CHECK(blocks.size() == 1);
    if (!blocks.empty()) {
        CHECK(blocks[0].scale == 1.0f);
        CHECK(blocks[0].box.x == 100 && blocks[0].box.w == 12);
    }
    crystal::updateBlocks(blocks, {det(kBreast, 101)}, 0.5f);
    CHECK(blocks.size() == 1);

    // A separate area is its own block, and goes with the next coarse tick.
    crystal::updateBlocks(blocks, {det(kBreast, 102), det(kBreast, 0)}, 0.333f);
    CHECK(blocks.size() == 2);
    crystal::updateBlocks(blocks, {}, 0.333f);
    CHECK(blocks.size() == 1);

    // The full scan that no longer sees it clears the grown block too.
    crystal::updateBlocks(blocks, {}, 1.0f);
    CHECK(blocks.empty());
}

using namespace std::chrono_literals;
using crystal::ScanState;
using Clock = crystal::Scheduler::Clock;

// Runs one scan on `s` and returns its scale.
float scan(crystal::Scheduler& s, Clock::time_point& now, bool triggered = false,
           std::size_t blocks = 0) {
    const float scale = s.nextScale();
    now += s.gap();
    s.record(triggered, blocks, now);
    return scale;
}

void calmScansCheaplyWithAFullScanEvery6th() {
    crystal::Scheduler s;
    Clock::time_point now{};
    CHECK(s.state() == ScanState::Calm && s.gap() == 5000ms);
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 5; ++i) CHECK(scan(s, now) == 0.333f);
        CHECK(scan(s, now) == 1.0f);
    }
    CHECK(s.state() == ScanState::Calm);
}

void triggersCoverBlockedAndWarningClassesFromLowerScore() {
    using crystal::triggersAlert;
    CHECK(triggersAlert({kBreast, crystal::kAlertScore, {}}));
    CHECK(!triggersAlert({kBreast, crystal::kAlertScore - 0.01f, {}}));
    for (int id : {0, 15}) CHECK(triggersAlert({id, 0.3f, {}})); // covered warnings
    for (int id : {1, 5, 7, 8, 12, 16, 17}) CHECK(!triggersAlert({id, 0.99f, {}}));
    // Anything that gets covered also triggers Alert.
    for (int id = 0; id < 18; ++id)
        if (crystal::shouldBlock(crystal::Detection{id, crystal::kBlockScore, {}}))
            CHECK(triggersAlert({id, crystal::kBlockScore, {}}));
}

void alertAlternatesSharpScansThenCalmsDown() {
    crystal::Scheduler s;
    Clock::time_point now{};
    scan(s, now);
    scan(s, now, true); // a 0.333 Calm scan sees something
    CHECK(s.state() == ScanState::Alert && s.gap() == 500ms);

    const auto trigger = now;
    CHECK(scan(s, now) == 1.0f);
    CHECK(scan(s, now) == 0.5f);
    CHECK(scan(s, now) == 1.0f);
    while (now - trigger < 30s - 500ms) scan(s, now);
    CHECK(s.state() == ScanState::Alert);
    scan(s, now); // the 30 s are up, nothing held
    CHECK(s.state() == ScanState::Calm && s.gap() == 5000ms);

    // A fresh Calm cycle: five cheap scans before the next full one.
    for (int i = 0; i < 5; ++i) CHECK(scan(s, now) == 0.333f);
    CHECK(scan(s, now) == 1.0f);
}

void eachTriggerRestartsTheAlertHold() {
    crystal::Scheduler s;
    Clock::time_point now{};
    scan(s, now, true);
    while (now < Clock::time_point{} + 20s) scan(s, now);
    scan(s, now, true); // seen again 20 s in
    const auto trigger = now;
    while (now - trigger < 29s) scan(s, now);
    CHECK(s.state() == ScanState::Alert);
    while (now - trigger < 30s) scan(s, now);
    CHECK(s.state() == ScanState::Calm);
}

void staysOnAlertWhileBlocksAreHeld() {
    crystal::Scheduler s;
    Clock::time_point now{};
    scan(s, now, true, 1);
    // No new trigger (say the image now scores 0.25), but a block found at 1.0 is still held.
    while (now < Clock::time_point{} + 60s) scan(s, now, false, 1);
    CHECK(s.state() == ScanState::Alert);
    scan(s, now, false, 0); // a 1.0 scan cleared it
    CHECK(s.state() == ScanState::Calm);
}

} // namespace

int main() {
    calmScansCheaplyWithAFullScanEvery6th();
    triggersCoverBlockedAndWarningClassesFromLowerScore();
    alertAlternatesSharpScansThenCalmsDown();
    eachTriggerRestartsTheAlertHold();
    staysOnAlertWhileBlocksAreHeld();
    blocksOnlyExposedLabels();
    blocksOnlyConfidentDetections();
    holdsSharperFindsThroughCoarserTicks();
    mergesRepeatFindsIntoHeldBlocks();
    return check::finish("monitor_test");
}
