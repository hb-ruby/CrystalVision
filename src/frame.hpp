#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace crystal {

// A captured screen image: 8-bit RGB, rows top to bottom, no row padding.
struct Frame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb; // width * height * 3 bytes

    // Sets the size and fills with black.
    void reset(int w, int h);
};

// Pixels as the X server returns 24/32-bit images on little-endian machines:
// 4 bytes per pixel in the order B, G, R, then padding (depth 24) or alpha (depth 32).
struct BgrxImage {
    const std::uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;        // bytes per row
    bool hasAlpha = false; // depth-32 window; colours are premultiplied by alpha
};

// Draws `src` onto `dst` with its top-left corner at (x, y), clipped to the frame.
// Opaque images overwrite; images with alpha are blended over what's already there.
void paste(Frame& dst, const BgrxImage& src, int x, int y);

// Saves the frame as a binary PPM (opens in any image viewer). Returns false on I/O error.
bool writePpm(const Frame& frame, const std::filesystem::path& path);

// Loads a binary (P6, 8-bit) PPM. On failure returns false and describes why in `error`.
bool readPpm(const std::filesystem::path& path, Frame& out, std::string& error);

// Draws a rectangle outline `thickness` pixels wide, inside the box (x, y, w, h),
// clipped to the frame.
void drawRect(Frame& frame, int x, int y, int w, int h, std::uint8_t r, std::uint8_t g,
              std::uint8_t b, int thickness = 2);

} // namespace crystal
