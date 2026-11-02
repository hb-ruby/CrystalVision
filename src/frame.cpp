#include "frame.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <string>

namespace crystal {

namespace {

// dst' = src + dst * (1 - alpha), for premultiplied colour.
std::uint8_t blend(std::uint8_t src, std::uint8_t dst, unsigned inverseAlpha) {
    const unsigned v = src + (dst * inverseAlpha + 127u) / 255u;
    return static_cast<std::uint8_t>(std::min(v, 255u));
}

} // namespace

void Frame::reset(int w, int h) {
    width = w;
    height = h;
    rgb.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3, 0);
}

void paste(Frame& dst, const BgrxImage& src, int x, int y) {
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + src.width, dst.width);
    const int y1 = std::min(y + src.height, dst.height);
    if (x0 >= x1 || y0 >= y1) return;

    for (int row = y0; row < y1; ++row) {
        const std::uint8_t* s = src.data + static_cast<std::ptrdiff_t>(row - y) * src.stride +
                                static_cast<std::ptrdiff_t>(x0 - x) * 4;
        std::uint8_t* d =
            dst.rgb.data() + (static_cast<std::size_t>(row) * static_cast<std::size_t>(dst.width) +
                              static_cast<std::size_t>(x0)) *
                                 3;
        if (!src.hasAlpha) {
            for (int col = x0; col < x1; ++col, s += 4, d += 3) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
            }
        } else {
            for (int col = x0; col < x1; ++col, s += 4, d += 3) {
                const unsigned inv = 255u - s[3];
                d[0] = blend(s[2], d[0], inv);
                d[1] = blend(s[1], d[1], inv);
                d[2] = blend(s[0], d[2], inv);
            }
        }
    }
}

bool writePpm(const Frame& frame, const std::filesystem::path& path) {
    std::ofstream out{path, std::ios::binary};
    if (!out) return false;
    out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    out.write(reinterpret_cast<const char*>(frame.rgb.data()),
              static_cast<std::streamsize>(frame.rgb.size()));
    return static_cast<bool>(out);
}

} // namespace crystal
