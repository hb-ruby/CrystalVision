// CrystalVision entry point.
//
//   crystalvision                     smoke test: load the model, check the X server
//   crystalvision --dump-frame FILE   capture the screen once and save it as a PPM image

#include "capture.hpp"
#include "frame.hpp"
#include "paths.hpp"
#include "x11.hpp"

#include <onnxruntime_cxx_api.h>
#include <xcb/composite.h>
#include <xcb/shm.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string_view>

namespace {

bool checkOnnx() {
    try {
        Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "crystalvision"};
        std::printf("ONNX Runtime: %s\n", Ort::GetVersionString().c_str());

        auto model = crystal::findModel();
        if (!model) {
            std::fprintf(stderr, "error: model not found (run scripts/fetch-models.sh)\n");
            return false;
        }
        Ort::SessionOptions opts;
        Ort::Session session{env, model->c_str(), opts};
        auto shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        std::printf("model: %s\n  input shape [", model->c_str());
        for (std::size_t i = 0; i < shape.size(); ++i)
            std::printf("%s%lld", i ? ", " : "", static_cast<long long>(shape[i]));
        std::printf("]\n");
        return true;
    } catch (const Ort::Exception& e) {
        std::fprintf(stderr, "ONNX Runtime error: %s\n", e.what());
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

void usage(std::FILE* to) {
    std::fprintf(to, "usage: crystalvision [--dump-frame FILE.ppm]\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 1) return smokeTest();

    const std::string_view arg = argv[1];
    if (arg == "--dump-frame" && argc == 3) return dumpFrame(argv[2]);
    if (arg == "--help" || arg == "-h") {
        usage(stdout);
        return EXIT_SUCCESS;
    }
    usage(stderr);
    return EXIT_FAILURE;
}
