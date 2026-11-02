#include "x11.hpp"

#include <stdexcept>

namespace crystal::x11 {

Connection::Connection() : conn_{xcb_connect(nullptr, &screenNum_)} {
    // xcb_connect never returns null; failure is reported through the connection object.
    if (xcb_connection_has_error(conn_.get()))
        throw std::runtime_error{"cannot connect to X server (is DISPLAY set?)"};

    xcb_screen_iterator_t it = xcb_setup_roots_iterator(xcb_get_setup(conn_.get()));
    for (int i = 0; i < screenNum_ && it.rem > 0; ++i) xcb_screen_next(&it);
    if (it.rem == 0) throw std::runtime_error{"X server reported no screen for DISPLAY"};
    screen_ = it.data;
}

bool Connection::hasExtension(xcb_extension_t* ext) const {
    const xcb_query_extension_reply_t* r = xcb_get_extension_data(conn_.get(), ext);
    return r != nullptr && r->present;
}

} // namespace crystal::x11
