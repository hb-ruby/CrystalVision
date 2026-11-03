// CrystalVision entry point.
//
//   crystalvision                     smoke test: load the model, check the X server
//   crystalvision --dump-frame FILE   capture the screen once and save it as a PPM image
//   crystalvision --detect IN.ppm [OUT.ppm]
//                                     run the detector on an image; optionally save a copy
//                                     with the detections drawn on it
//   crystalvision --detect-screen [OUT.ppm]
//                                     the same, on one capture of the screen
//   crystalvision --monitor           capture and detect once a second until stopped

#include "capture.hpp"
#include "detector.hpp"
#include "frame.hpp"
#include "monitor.hpp"
#include "overlay.hpp"
#include "paths.hpp"
#include "x11.hpp"

#include <onnxruntime_cxx_api.h>
#include <xcb/composite.h>
#include <xcb/shm.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <pthread.h>
#include <time.h>

namespace {

bool checkOnnx() {
    try {
        std::printf("ONNX Runtime: %s\n", Ort::GetVersionString().c_str());

        auto model = crystal::findModel();
        if (!model) {
            std::fprintf(stderr, "error: model not found (run scripts/fetch-models.sh)\n");
            return false;
        }
        crystal::Detector detector{*model};
        std::printf("model: %s (input %dx%d)\n", model->c_str(), detector.inputSize(),
                    detector.inputSize());
        return true;
    } catch (const Ort::Exception& e) {
        std::fprintf(stderr, "ONNX Runtime error: %s\n", e.what());
        return false;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return false;
    }
}

bool checkX11() {
    try {
        crystal::x11::Connection x;
        const xcb_screen_t& s = x.screen();
        std::printf("X11: screen %d, %ux%u, depth %u\n", x.screenNumber(), s.width_in_pixels,
                    s.height_in_pixels, s.root_depth);
        std::printf("X11: MIT-SHM %s, Composite %s\n", x.hasExtension(&xcb_shm_id) ? "yes" : "NO",
                    x.hasExtension(&xcb_composite_id) ? "yes" : "NO");
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return false;
    }
}

int smokeTest() {
    bool ok = checkOnnx();
    ok = checkX11() && ok;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int dumpFrame(const char* path) {
    try {
        crystal::x11::Connection x;
        crystal::Capturer capturer{x};
        crystal::Frame frame;

        const auto start = std::chrono::steady_clock::now();
        const auto stats = capturer.capture(frame);
        const std::chrono::duration<double, std::milli> took =
            std::chrono::steady_clock::now() - start;

        if (!crystal::writePpm(frame, path)) {
            std::fprintf(stderr, "error: cannot write %s\n", path);
            return EXIT_FAILURE;
        }
        std::printf("captured %dx%d: wallpaper %s, %d windows (%d skipped) in %.1f ms -> %s\n",
                    frame.width, frame.height, stats.wallpaperDrawn ? "yes" : "no",
                    stats.windowsDrawn, stats.windowsSkipped, took.count(), path);
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return EXIT_FAILURE;
    }
}

// Runs the detector on `image`, prints the detections and timing, and optionally saves
// a copy with the boxes drawn (red: *_EXPOSED classes, green: everything else).
int detect(crystal::Frame& image, const char* outPath) {
    try {
        auto model = crystal::findModel();
        if (!model) {
            std::fprintf(stderr, "error: model not found (run scripts/fetch-models.sh)\n");
            return EXIT_FAILURE;
        }
        const auto loadStart = std::chrono::steady_clock::now();
        crystal::Detector detector{*model};
        const std::chrono::duration<double, std::milli> loadMs =
            std::chrono::steady_clock::now() - loadStart;

        // The first run is slower (memory allocation, lazy initialisation), so time a second
        // one as well; that's the steady-state cost the live loop will pay.
        crystal::Detector::Timing first, second;
        detector.detect(image, &first);
        const auto dets = detector.detect(image, &second);

        std::printf("image %dx%d, model input %dx%d, %zu anchors\n", image.width, image.height,
                    detector.inputSize(), detector.inputSize(), detector.anchors());
        std::printf("%zu detection(s):\n", dets.size());
        for (const auto& d : dets) {
            std::printf("  %-26s %.3f  x=%d y=%d w=%d h=%d\n",
                        std::string{crystal::kLabels[static_cast<std::size_t>(d.classId)]}.c_str(),
                        static_cast<double>(d.score), static_cast<int>(d.box.x),
                        static_cast<int>(d.box.y), static_cast<int>(d.box.w),
                        static_cast<int>(d.box.h));
        }
        std::printf("timing: model load %.0f ms; first run %.0f ms; steady run: pre %.1f ms, "
                    "inference %.0f ms, post %.1f ms\n",
                    loadMs.count(), first.preMs + first.inferMs + first.postMs, second.preMs,
                    second.inferMs, second.postMs);

        if (outPath) {
            for (const auto& d : dets) {
                const bool exposed =
                    crystal::kLabels[static_cast<std::size_t>(d.classId)].ends_with("_EXPOSED");
                crystal::drawRect(image, static_cast<int>(d.box.x), static_cast<int>(d.box.y),
                                  static_cast<int>(d.box.w), static_cast<int>(d.box.h),
                                  exposed ? 255 : 0, exposed ? 0 : 200, 0, 3);
            }
            if (!crystal::writePpm(image, outPath)) {
                std::fprintf(stderr, "error: cannot write %s\n", outPath);
                return EXIT_FAILURE;
            }
            std::printf("annotated image -> %s\n", outPath);
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& e) { // Ort::Exception derives from std::exception
        std::fprintf(stderr, "error: %s\n", e.what());
        return EXIT_FAILURE;
    }
}

int detectFile(const char* inPath, const char* outPath) {
    crystal::Frame image;
    std::string error;
    if (!crystal::readPpm(inPath, image, error)) {
        std::fprintf(stderr, "error: %s: %s\n", inPath, error.c_str());
        return EXIT_FAILURE;
    }
    return detect(image, outPath);
}

int detectScreen(const char* outPath) {
    crystal::Frame frame;
    try {
        crystal::x11::Connection x;
        crystal::Capturer capturer{x};
        capturer.capture(frame);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return detect(frame, outPath);
}

// The monitoring loop: one tick (capture the screen, detect), then wait a second, then the
// next tick. Exposed detections are covered with black boxes (see Monitor and Overlay).
// The wait starts only after a tick finishes, so a slow tick (a full-resolution
// one, say) pushes the next one back instead of piling up. Runs until SIGINT (Ctrl+C),
// SIGTERM or SIGHUP; a signal that arrives mid-tick stops the loop once the tick is done.
int monitor() {
    // Block the stop signals before ONNX Runtime starts its threads (they inherit the mask),
    // so they stay pending until sigtimedwait picks them up during the wait.
    sigset_t stop;
    sigemptyset(&stop);
    sigaddset(&stop, SIGINT);
    sigaddset(&stop, SIGTERM);
    sigaddset(&stop, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &stop, nullptr);

    try {
        auto model = crystal::findModel();
        if (!model) {
            std::fprintf(stderr, "error: model not found (run scripts/fetch-models.sh)\n");
            return EXIT_FAILURE;
        }
        // Four inference threads at most, so a scan never takes over every core.
        crystal::Detector::Options opts;
        opts.threads = 4;
        crystal::Detector detector{*model, opts};
        if (!detector.dynamicInput()) {
            std::fprintf(stderr,
                         "error: %s has a fixed input size; the monitor needs a model "
                         "with a dynamic one\n",
                         model->c_str());
            return EXIT_FAILURE;
        }
        crystal::x11::Connection x;
        crystal::Capturer capturer{x};
        crystal::Overlay overlay{x};
        capturer.exclude(overlay.window()); // analyse what's under the boxes, not the boxes
        crystal::Monitor mon{capturer, detector};
        std::vector<crystal::Box> covered;
        std::printf("monitoring; Ctrl+C to stop\n");

        for (;;) {
            const auto t = mon.tick();
            covered.clear();
            for (const auto& b : t.blocks) covered.push_back(b.box);
            overlay.show(covered);
            const auto& tm = t.timing;
            std::printf("tick %llu  scale %.3f  input %dx%d  capture %.0f ms  detect %.0f ms "
                        "(pre %.0f, inference %.0f, post %.1f)  %zu detection(s), %zu covered\n",
                        static_cast<unsigned long long>(t.number), static_cast<double>(t.scale),
                        tm.inputWidth, tm.inputHeight, t.captureMs,
                        tm.preMs + tm.inferMs + tm.postMs, tm.preMs, tm.inferMs, tm.postMs,
                        t.detections.size(), t.blocks.size());
            for (const auto& d : t.detections) {
                std::printf(
                    "  %-26s %.3f  x=%d y=%d w=%d h=%d\n",
                    std::string{crystal::kLabels[static_cast<std::size_t>(d.classId)]}.c_str(),
                    static_cast<double>(d.score), static_cast<int>(d.box.x),
                    static_cast<int>(d.box.y), static_cast<int>(d.box.w),
                    static_cast<int>(d.box.h));
            }
            std::fflush(stdout);

            const timespec interval{1, 0};
            const int sig = sigtimedwait(&stop, nullptr, &interval);
            if (sig > 0) {
                std::printf("stopping (signal %d) after %llu tick(s)\n", sig,
                            static_cast<unsigned long long>(t.number));
                return EXIT_SUCCESS;
            }
            // sig == -1: EAGAIN means the second is up; EINTR (some other signal) just
            // starts the next tick a little early.
        }
    } catch (const std::exception& e) { // Ort::Exception derives from std::exception
        std::fprintf(stderr, "error: %s\n", e.what());
        return EXIT_FAILURE;
    }
}

void usage(std::FILE* to) {
    std::fprintf(to, "usage: crystalvision [--dump-frame FILE.ppm]\n"
                     "       crystalvision --detect IN.ppm [OUT.ppm]\n"
                     "       crystalvision --detect-screen [OUT.ppm]\n"
                     "       crystalvision --monitor\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) return smokeTest();

    const std::string_view arg = argv[1];
    if (arg == "--dump-frame" && argc == 3) return dumpFrame(argv[2]);
    if (arg == "--detect" && (argc == 3 || argc == 4))
        return detectFile(argv[2], argc == 4 ? argv[3] : nullptr);
    if (arg == "--detect-screen" && argc <= 3) return detectScreen(argc == 3 ? argv[2] : nullptr);
    if (arg == "--monitor" && argc == 2) return monitor();
    if (arg == "--help" || arg == "-h") {
        usage(stdout);
        return EXIT_SUCCESS;
    }
    usage(stderr);
    return EXIT_FAILURE;
}
