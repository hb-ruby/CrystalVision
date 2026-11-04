// Integration test for the Detector with the real model: every input path (letterboxed, and
// scaled at each monitor scale) on awkward image sizes, then a long run that must not keep
// growing in memory. Skipped when the model isn't there (run scripts/fetch-models.sh).
//
// Run it in the asan preset to check that the tensors we hand ONNX Runtime match their
// buffers and nothing leaks; it takes a minute or two there.

#include "check.hpp"
#include "detector.hpp"
#include "frame.hpp"
#include "paths.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>

#include <unistd.h>

namespace {

using crystal::Detection;
using crystal::Detector;
using crystal::Frame;

Frame noise(int w, int h) {
    std::mt19937 rng{static_cast<unsigned>(w * 7919 + h)};
    Frame f;
    f.reset(w, h);
    for (auto& v : f.rgb) v = static_cast<std::uint8_t>(rng());
    return f;
}

long rssKb() {
    std::ifstream in{"/proc/self/statm"};
    long pages = 0, resident = 0;
    in >> pages >> resident;
    return resident * (sysconf(_SC_PAGESIZE) / 1024);
}

void checkDetections(const std::vector<Detection>& dets, const Frame& f, float minScore) {
    for (std::size_t i = 0; i < dets.size(); ++i) {
        const auto& d = dets[i];
        CHECK(d.classId >= 0 && d.classId < static_cast<int>(crystal::kLabels.size()));
        CHECK(d.score >= minScore && d.score <= 1);
        CHECK(d.box.x >= 0 && d.box.y >= 0 && d.box.w > 0 && d.box.h > 0);
        CHECK(d.box.x + d.box.w <= static_cast<float>(f.width) + 0.01f);
        CHECK(d.box.y + d.box.h <= static_cast<float>(f.height) + 0.01f);
        if (i > 0) CHECK(dets[i - 1].score >= d.score);
    }
}

void runsEveryPathOnAwkwardSizes(Detector& detector) {
    const Detector::Options opts;
    for (auto [w, h] : {std::pair{1, 1}, {7, 5}, {33, 31}, {640, 640}, {1281, 799}, {300, 1200}}) {
        const Frame f = noise(w, h);
        Detector::Timing tm;
        checkDetections(detector.detect(f, &tm), f, opts.minScore);
        CHECK(tm.inputWidth == detector.inputSize() && tm.inputHeight == detector.inputSize());
        CHECK(detector.anchors() > 0);

        for (float scale : {0.333f, 0.5f, 1.0f}) {
            checkDetections(detector.detect(f, scale, &tm), f, opts.minScore);
            CHECK(tm.inputWidth % 32 == 0 && tm.inputHeight % 32 == 0);
            CHECK(tm.inputWidth >= static_cast<int>(static_cast<float>(w) * scale));
            CHECK(tm.inputHeight >= static_cast<int>(static_cast<float>(h) * scale));
        }
    }
    CHECK(detector.detect(Frame{}, 0.5f).empty());
}

// The monitor's pattern of scales, many times over. ONNX Runtime's memory arena grows to the
// largest input it has seen and then stays put, so after one round of every scale the
// resident size must stay flat.
void longRunStaysFlat(Detector& detector) {
    const Frame f = noise(960, 540);
    const float scales[] = {0.333f, 1.0f, 0.5f};
    long warm = 0;
    for (int i = 0; i < 30; ++i) {
        detector.detect(f, scales[i % 3]);
        if (i == 5) warm = rssKb();
    }
    const long grown = rssKb() - warm;
    std::printf("detector_test: RSS %ld MiB, grew %ld KiB over 24 runs\n", rssKb() / 1024, grown);
    if (check::kRssIsMeaningful) CHECK(grown < 8 * 1024);
}

} // namespace

int main() {
    const auto model = crystal::findModel();
    if (!model) return check::skip("detector_test", "model not found");

    Detector::Options opts;
    opts.threads = 4;
    Detector detector{*model, opts};
    CHECK(detector.dynamicInput());
    runsEveryPathOnAwkwardSizes(detector);
    longRunStaysFlat(detector);
    return check::finish("detector_test");
}
