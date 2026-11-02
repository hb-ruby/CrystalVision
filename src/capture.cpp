#include "capture.hpp"

#include <xcb/composite.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace crystal {

namespace {

using x11::Reply;

void requireExtension(const x11::Connection& x, xcb_extension_t* ext, const char* name) {
    if (!x.hasExtension(ext))
        throw std::runtime_error{std::string{"X server lacks the "} + name + " extension"};
}

// paste() assumes 32 bits per pixel, little-endian, for both 24- and 32-bit depths.
void requireBgrxFormat(xcb_connection_t* c) {
    const xcb_setup_t* setup = xcb_get_setup(c);
    if (setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST)
        throw std::runtime_error{"unsupported X image byte order (expected little-endian)"};
    xcb_format_iterator_t it = xcb_setup_pixmap_formats_iterator(setup);
    for (; it.rem > 0; xcb_format_next(&it)) {
        if ((it.data->depth == 24 || it.data->depth == 32) && it.data->bits_per_pixel != 32)
            throw std::runtime_error{"unsupported X pixel format (expected 32 bits per pixel)"};
    }
}

} // namespace

Capturer::Capturer(x11::Connection& x) : x_{x} {
    xcb_connection_t* c = x_.get();
    requireExtension(x_, &xcb_shm_id, "MIT-SHM");
    requireExtension(x_, &xcb_composite_id, "Composite");
    requireBgrxFormat(c);

    // NameWindowPixmap needs Composite 0.2+.
    Reply<xcb_composite_query_version_reply_t> ver{
        xcb_composite_query_version_reply(c, xcb_composite_query_version(c, 0, 4), nullptr)};
    if (!ver || (ver->major_version == 0 && ver->minor_version < 2))
        throw std::runtime_error{"X server's Composite extension is too old (need 0.2+)"};

    Reply<xcb_generic_error_t> err{xcb_request_check(
        c,
        xcb_composite_redirect_subwindows_checked(c, x_.root(), XCB_COMPOSITE_REDIRECT_AUTOMATIC))};
    if (err) throw std::runtime_error{"could not enable Composite redirection"};

    pixmap_ = xcb_generate_id(c);

    const char* names[] = {"_XROOTPMAP_ID", "ESETROOT_PMAP_ID"};
    xcb_intern_atom_cookie_t cookies[2];
    for (int i = 0; i < 2; ++i)
        cookies[i] = xcb_intern_atom(c, /*only_if_exists=*/1,
                                     static_cast<std::uint16_t>(std::strlen(names[i])), names[i]);
    for (int i = 0; i < 2; ++i) {
        Reply<xcb_intern_atom_reply_t> r{xcb_intern_atom_reply(c, cookies[i], nullptr)};
        if (r) wallpaperAtoms_[i] = r->atom;
    }
}

Capturer::~Capturer() {
    xcb_composite_unredirect_subwindows(x_.get(), x_.root(), XCB_COMPOSITE_REDIRECT_AUTOMATIC);
    releaseShm();
    xcb_flush(x_.get());
}

void Capturer::exclude(xcb_window_t window) {
    if (std::find(excluded_.begin(), excluded_.end(), window) == excluded_.end())
        excluded_.push_back(window);
}

void Capturer::releaseShm() {
    if (!shmAddr_) return;
    xcb_shm_detach(x_.get(), seg_);
    // Wait until the server has detached before unmapping our side.
    Reply<xcb_get_input_focus_reply_t> sync{
        xcb_get_input_focus_reply(x_.get(), xcb_get_input_focus(x_.get()), nullptr)};
    shmdt(shmAddr_);
    shmAddr_ = nullptr;
    shmSize_ = 0;
}

void Capturer::ensureShm(std::size_t bytes) {
    if (bytes <= shmSize_) return;
    releaseShm();

    xcb_connection_t* c = x_.get();
    shmId_ = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
    if (shmId_ < 0) throw std::runtime_error{"shmget failed"};
    void* addr = shmat(shmId_, nullptr, 0);
    if (addr == reinterpret_cast<void*>(-1)) {
        shmctl(shmId_, IPC_RMID, nullptr);
        throw std::runtime_error{"shmat failed"};
    }

    seg_ = xcb_generate_id(c);
    Reply<xcb_generic_error_t> err{
        xcb_request_check(c, xcb_shm_attach_checked(c, seg_, static_cast<std::uint32_t>(shmId_),
                                                    /*read_only=*/0))};
    // Mark for deletion now; the kernel frees it once both sides have detached,
    // so it can't leak even if we crash.
    shmctl(shmId_, IPC_RMID, nullptr);
    if (err) {
        shmdt(addr);
        throw std::runtime_error{"X server could not attach shared memory (remote display?)"};
    }
    shmAddr_ = static_cast<std::uint8_t*>(addr);
    shmSize_ = bytes;
}

Capturer::Stats Capturer::capture(Frame& out) {
    xcb_connection_t* c = x_.get();
    Stats stats;

    // Re-read the root size every time so resolution / monitor changes are picked up.
    Reply<xcb_get_geometry_reply_t> rootGeo{
        xcb_get_geometry_reply(c, xcb_get_geometry(c, x_.root()), nullptr)};
    if (!rootGeo) throw std::runtime_error{"lost connection to X server"};
    out.reset(rootGeo->width, rootGeo->height);

    // Wallpaper first; windows are painted over it.
    stats.wallpaperDrawn = grabWallpaper(out);

    // Top-level windows, bottom of the stack first.
    Reply<xcb_query_tree_reply_t> tree{
        xcb_query_tree_reply(c, xcb_query_tree(c, x_.root()), nullptr)};
    if (!tree) throw std::runtime_error{"lost connection to X server"};
    const xcb_window_t* children = xcb_query_tree_children(tree.get());
    const auto count = static_cast<std::size_t>(xcb_query_tree_children_length(tree.get()));

    // Send all attribute/geometry requests before reading any reply: one round trip total.
    std::vector<xcb_get_window_attributes_cookie_t> attrCookies(count);
    std::vector<xcb_get_geometry_cookie_t> geoCookies(count);
    for (std::size_t i = 0; i < count; ++i) {
        attrCookies[i] = xcb_get_window_attributes(c, children[i]);
        geoCookies[i] = xcb_get_geometry(c, children[i]);
    }

    for (std::size_t i = 0; i < count; ++i) {
        // Always collect both replies (a vanished window yields an error, freed here).
        xcb_generic_error_t* e1 = nullptr;
        xcb_generic_error_t* e2 = nullptr;
        Reply<xcb_get_window_attributes_reply_t> attr{
            xcb_get_window_attributes_reply(c, attrCookies[i], &e1)};
        Reply<xcb_get_geometry_reply_t> geo{xcb_get_geometry_reply(c, geoCookies[i], &e2)};
        Reply<xcb_generic_error_t> err1{e1}, err2{e2};

        const xcb_window_t w = children[i];
        const bool excluded = std::find(excluded_.begin(), excluded_.end(), w) != excluded_.end();
        if (excluded) continue;
        if (!attr || !geo || attr->map_state != XCB_MAP_STATE_VIEWABLE ||
            attr->_class != XCB_WINDOW_CLASS_INPUT_OUTPUT ||
            (geo->depth != 24 && geo->depth != 32) || !grabWindow(out, w, *geo)) {
            ++stats.windowsSkipped;
            continue;
        }
        ++stats.windowsDrawn;
    }
    return stats;
}

bool Capturer::grabWindow(Frame& out, xcb_window_t window, const xcb_get_geometry_reply_t& geo) {
    xcb_connection_t* c = x_.get();

    // The window's pixmap includes its border; geo.x/y is the border's outer corner.
    const int fullW = geo.width + 2 * geo.border_width;
    const int fullH = geo.height + 2 * geo.border_width;

    // Only fetch the part that's on screen.
    const int x0 = std::max<int>(geo.x, 0);
    const int y0 = std::max<int>(geo.y, 0);
    const int x1 = std::min(geo.x + fullW, out.width);
    const int y1 = std::min(geo.y + fullH, out.height);
    if (x0 >= x1 || y0 >= y1) return false;
    const int w = x1 - x0;
    const int h = y1 - y0;

    ensureShm(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);

    // Pipelined: send "name the pixmap" and "read it" together, then wait once.
    // Naming fails (BadMatch) for windows that aren't redirected, such as the
    // compositor's own overlay window, and for windows unmapped since we listed them.
    // Every request here is checked, so no error ever lands in the unread event queue.
    xcb_void_cookie_t nameCookie = xcb_composite_name_window_pixmap_checked(c, window, pixmap_);
    xcb_shm_get_image_cookie_t imgCookie =
        xcb_shm_get_image(c, pixmap_, static_cast<int16_t>(x0 - geo.x),
                          static_cast<int16_t>(y0 - geo.y), static_cast<uint16_t>(w),
                          static_cast<uint16_t>(h), ~0u, XCB_IMAGE_FORMAT_Z_PIXMAP, seg_, 0);

    Reply<xcb_generic_error_t> nameErr{xcb_request_check(c, nameCookie)};
    xcb_generic_error_t* e = nullptr;
    Reply<xcb_shm_get_image_reply_t> img{xcb_shm_get_image_reply(c, imgCookie, &e)};
    Reply<xcb_generic_error_t> imgErr{e};
    if (nameErr) return false;
    xcb_free_pixmap(c, pixmap_); // only exists if naming succeeded
    if (!img) return false;

    paste(out, BgrxImage{shmAddr_, w, h, w * 4, geo.depth == 32}, x0, y0);
    return true;
}

bool Capturer::grabWallpaper(Frame& out) {
    xcb_connection_t* c = x_.get();

    for (xcb_atom_t atom : wallpaperAtoms_) {
        if (atom == XCB_ATOM_NONE) continue;
        Reply<xcb_get_property_reply_t> prop{xcb_get_property_reply(
            c, xcb_get_property(c, 0, x_.root(), atom, XCB_ATOM_PIXMAP, 0, 1), nullptr)};
        if (!prop || prop->type != XCB_ATOM_PIXMAP || prop->format != 32 ||
            xcb_get_property_value_length(prop.get()) < 4)
            continue;
        const xcb_pixmap_t pixmap =
            *static_cast<const xcb_pixmap_t*>(xcb_get_property_value(prop.get()));
        if (pixmap == XCB_NONE) continue;

        // The property can name a pixmap that has since been freed: check, don't assume.
        xcb_generic_error_t* e = nullptr;
        Reply<xcb_get_geometry_reply_t> geo{
            xcb_get_geometry_reply(c, xcb_get_geometry(c, pixmap), &e)};
        Reply<xcb_generic_error_t> geoErr{e};
        if (!geo || (geo->depth != 24 && geo->depth != 32)) continue;

        const int w = std::min<int>(geo->width, out.width);
        const int h = std::min<int>(geo->height, out.height);
        if (w <= 0 || h <= 0) continue;
        ensureShm(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);

        e = nullptr;
        Reply<xcb_shm_get_image_reply_t> img{xcb_shm_get_image_reply(
            c,
            xcb_shm_get_image(c, pixmap, 0, 0, static_cast<uint16_t>(w), static_cast<uint16_t>(h),
                              ~0u, XCB_IMAGE_FORMAT_Z_PIXMAP, seg_, 0),
            &e)};
        Reply<xcb_generic_error_t> imgErr{e};
        if (!img) continue;

        // The wallpaper is the bottom layer, so treat it as opaque even if 32-bit.
        paste(out, BgrxImage{shmAddr_, w, h, w * 4, false}, 0, 0);
        return true;
    }
    return false;
}

} // namespace crystal
