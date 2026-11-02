// CrystalVision — smoke test: proves the build can load the model with ONNX Runtime
// and reach the X server through XCB.

#include "paths.hpp"

#include <onnxruntime_cxx_api.h>
#include <xcb/composite.h>
#include <xcb/shm.h>
#include <xcb/xcb.h>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

struct XcbDisconnect {
    void operator()(xcb_connection_t* c) const { xcb_disconnect(c); }
};
using XcbConnection = std::unique_ptr<xcb_connection_t, XcbDisconnect>;

bool hasExtension(xcb_connection_t* conn, xcb_extension_t* ext) {
    const xcb_query_extension_reply_t* r = xcb_get_extension_data(conn, ext);
    return r != nullptr && r->present;
}

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
    int screenNum = 0;
    XcbConnection conn{xcb_connect(nullptr, &screenNum)};
    if (xcb_connection_has_error(conn.get())) {
        std::fprintf(stderr, "error: cannot connect to X server (is DISPLAY set?)\n");
        return false;
    }

    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(conn.get()));
    for (int i = 0; i < screenNum; ++i) xcb_screen_next(&it);
    const xcb_screen_t* screen = it.data;

    std::printf("X11: screen %d, %ux%u, depth %u\n", screenNum, screen->width_in_pixels,
                screen->height_in_pixels, screen->root_depth);
    std::printf("X11: MIT-SHM %s, Composite %s\n",
                hasExtension(conn.get(), &xcb_shm_id) ? "yes" : "NO",
                hasExtension(conn.get(), &xcb_composite_id) ? "yes" : "NO");
    return true;
}

} // namespace

int main() {
    bool ok = checkOnnx();
    ok = checkX11() && ok;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
