#include "overlay.hpp"

#include <xcb/shape.h>

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace crystal {

namespace {

using x11::Reply;

void setShape(xcb_connection_t* c, xcb_window_t w, xcb_shape_kind_t kind,
              std::span<const xcb_rectangle_t> rects) {
    xcb_shape_rectangles(c, XCB_SHAPE_SO_SET, kind, XCB_CLIP_ORDERING_UNSORTED, w, 0, 0,
                         static_cast<std::uint32_t>(rects.size()), rects.data());
}

// Clamps `v` to [0, hi] before converting it, so no value (huge, infinite, NaN) overflows int.
int toPixel(float v, int hi) {
    return v > 0 ? (v < static_cast<float>(hi) ? static_cast<int>(v) : hi) : 0;
}

void setString(xcb_connection_t* c, xcb_window_t w, xcb_atom_t prop, const char* value,
               std::size_t len) {
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, prop, XCB_ATOM_STRING, 8,
                        static_cast<std::uint32_t>(len), value);
}

} // namespace

Overlay::Overlay(x11::Connection& x) : x_{x} {
    xcb_connection_t* c = x_.get();
    if (!x_.hasExtension(&xcb_shape_id))
        throw std::runtime_error{"X server lacks the SHAPE extension"};
    // Input shapes (click-through) need SHAPE 1.1.
    Reply<xcb_shape_query_version_reply_t> ver{
        xcb_shape_query_version_reply(c, xcb_shape_query_version(c), nullptr)};
    if (!ver || (ver->major_version == 1 && ver->minor_version < 1) || ver->major_version < 1)
        throw std::runtime_error{"X server's SHAPE extension is too old (need 1.1+)"};

    const xcb_screen_t& s = x_.screen();
    width_ = s.width_in_pixels;
    height_ = s.height_in_pixels;
    window_ = xcb_generate_id(c);
    const std::uint32_t values[] = {s.black_pixel, /*override_redirect=*/1};
    Reply<xcb_generic_error_t> err{xcb_request_check(
        c, xcb_create_window_checked(c, XCB_COPY_FROM_PARENT, window_, x_.root(), 0, 0,
                                     static_cast<std::uint16_t>(width_),
                                     static_cast<std::uint16_t>(height_), 0,
                                     XCB_WINDOW_CLASS_INPUT_OUTPUT, s.root_visual,
                                     XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT, values))};
    if (err) throw std::runtime_error{"could not create the overlay window"};

    // Nothing visible and nothing clickable until show() says otherwise.
    setShape(c, window_, XCB_SHAPE_SK_BOUNDING, {});
    setShape(c, window_, XCB_SHAPE_SK_INPUT, {});

    // Names for xprop/xwininfo; WM_CLASS is "instance\0class\0".
    static constexpr char name[] = "CrystalVision overlay";
    static constexpr char cls[] = "crystalvision\0CrystalVision";
    setString(c, window_, XCB_ATOM_WM_NAME, name, std::strlen(name));
    setString(c, window_, XCB_ATOM_WM_CLASS, cls, sizeof cls);
    xcb_flush(c);
}

Overlay::~Overlay() {
    xcb_destroy_window(x_.get(), window_);
    xcb_flush(x_.get());
}

void Overlay::show(std::span<const Box> boxes) {
    xcb_connection_t* c = x_.get();

    if (boxes.empty()) {
        if (mapped_) {
            xcb_unmap_window(c, window_);
            mapped_ = false;
        }
        xcb_flush(c);
        return;
    }

    // Follow screen size changes (monitor plugged in, resolution changed).
    Reply<xcb_get_geometry_reply_t> root{
        xcb_get_geometry_reply(c, xcb_get_geometry(c, x_.root()), nullptr)};
    if (!root) throw std::runtime_error{"lost connection to X server"};
    if (root->width != width_ || root->height != height_) {
        width_ = root->width;
        height_ = root->height;
        const std::uint32_t size[] = {root->width, root->height};
        xcb_configure_window(c, window_, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
    }

    // Round outwards so a box is never smaller than the detection.
    rects_.clear();
    for (const Box& b : boxes) {
        const int x0 = toPixel(std::floor(b.x), width_);
        const int y0 = toPixel(std::floor(b.y), height_);
        const int x1 = toPixel(std::ceil(b.x + b.w), width_);
        const int y1 = toPixel(std::ceil(b.y + b.h), height_);
        if (x1 > x0 && y1 > y0)
            rects_.push_back({static_cast<std::int16_t>(x0), static_cast<std::int16_t>(y0),
                              static_cast<std::uint16_t>(x1 - x0),
                              static_cast<std::uint16_t>(y1 - y0)});
    }

    // Shape first, then map: mapping before shaping would flash the whole screen black.
    setShape(c, window_, XCB_SHAPE_SK_BOUNDING, rects_);
    if (!mapped_) {
        xcb_map_window(c, window_);
        mapped_ = true;
    }
    const std::uint32_t above[] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(c, window_, XCB_CONFIG_WINDOW_STACK_MODE, above);
    xcb_flush(c);
}

} // namespace crystal
