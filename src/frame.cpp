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

namespace {

// Reads the next whitespace-separated PPM header number, skipping '#' comments.
bool readHeaderInt(std::istream& in, int& value) {
    for (;;) {
        const int c = in.peek();
        if (c == '#') {
            std::string ignored;
            std::getline(in, ignored);
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            in.get();
        } else {
            break;
        }
    }
    return static_cast<bool>(in >> value);
}

} // namespace

bool readPpm(const std::filesystem::path& path, Frame& out, std::string& error) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        error = "cannot open file";
        return false;
    }
    char magic[2] = {};
    in.read(magic, 2);
    if (!in || magic[0] != 'P' || magic[1] != '6') {
        error = "not a binary PPM (P6) file";
        return false;
    }
    int w = 0, h = 0, maxval = 0;
    if (!readHeaderInt(in, w) || !readHeaderInt(in, h) || !readHeaderInt(in, maxval)) {
        error = "malformed PPM header";
        return false;
    }
    if (w <= 0 || h <= 0 || w > 32768 || h > 32768) {
        error = "unsupported PPM size";
        return false;
    }
    if (maxval != 255) {
        error = "only 8-bit PPM (maxval 255) is supported";
        return false;
    }
    in.get(); // the single whitespace byte that ends the header

    out.reset(w, h);
    in.read(reinterpret_cast<char*>(out.rgb.data()), static_cast<std::streamsize>(out.rgb.size()));
    if (!in) {
        error = "PPM pixel data is truncated";
        return false;
    }
    return true;
}

void drawRect(Frame& frame, int x, int y, int w, int h, std::uint8_t r, std::uint8_t g,
              std::uint8_t b, int thickness) {
    auto fill = [&](int x0, int y0, int x1, int y1) { // half-open, clipped
        x0 = std::max(x0, 0);
        y0 = std::max(y0, 0);
        x1 = std::min(x1, frame.width);
        y1 = std::min(y1, frame.height);
        for (int row = y0; row < y1; ++row) {
            for (int col = x0; col < x1; ++col) {
                std::uint8_t* d = frame.rgb.data() + (static_cast<std::size_t>(row) *
                                                          static_cast<std::size_t>(frame.width) +
                                                      static_cast<std::size_t>(col)) *
                                                         3;
                d[0] = r;
                d[1] = g;
                d[2] = b;
            }
        }
    };
    const int t = std::max(1, std::min({thickness, w, h}));
    fill(x, y, x + w, y + t);         // top
    fill(x, y + h - t, x + w, y + h); // bottom
    fill(x, y, x + t, y + h);         // left
    fill(x + w - t, y, x + w, y + h); // right
}

} // namespace crystal
