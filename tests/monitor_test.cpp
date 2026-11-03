// Unit tests for the monitoring loop's scale schedule and block holding. No model or X server
// needed.

#include "monitor.hpp"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

void followsTickSchedule() {
    using crystal::tickScale;
    for (int n : {1, 2, 3, 4, 6, 7, 8, 9, 11, 14, 16, 19}) CHECK(tickScale(n) == 0.333f);
    for (int n : {5, 15, 25, 35}) CHECK(tickScale(n) == 0.5f);
    for (int n : {10, 20, 30, 100}) CHECK(tickScale(n) == 1.0f);
}

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

} // namespace

int main() {
    followsTickSchedule();
    blocksOnlyExposedLabels();
    holdsSharperFindsThroughCoarserTicks();
    if (failures == 0) std::printf("monitor_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
