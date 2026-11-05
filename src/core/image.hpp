#pragma once
// Small images and golden-image comparison.
//
// CI compares a downsampled frame rather than a full one: a driver that rounds a
// pixel differently must not fail the build, and a reference small enough to read
// in a diff is a reference people will actually look at. Everything here is pure
// integer maths so the result is identical on every platform.
#include "core/types.hpp"

#include <span>
#include <string>
#include <vector>

namespace sc {

// A tightly packed RGB image, three bytes per pixel.
struct Thumbnail {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgb;

    bool empty() const { return width == 0 || height == 0 || rgb.empty(); }
};

// Box-filters a frame read back from the GPU (RGBA8) into a smaller image.
// `swizzle_bgra` handles B8G8R8A8, which is what the swapchain usually offers.
// Returns an empty thumbnail when the input is too small for its declared size.
Thumbnail make_thumbnail(std::span<const u8> rgba, u32 width, u32 height, u32 out_width,
                         u32 out_height, bool swizzle_bgra);

struct ImageDifference {
    u32 max_channel_delta = 0;
    double mean_channel_delta = 0.0;
    u32 changed_pixels = 0;

    // Mean rather than max: averaging over a box-filtered block is what makes the
    // comparison stable across drivers while still catching a real regression.
    bool within(u32 tolerance) const { return mean_channel_delta <= static_cast<double>(tolerance); }
};

// Images of different sizes never match; they report the worst possible score.
ImageDifference compare_thumbnails(const Thumbnail& actual, const Thumbnail& expected);

// Binary PPM (P6). Deliberately uncompressed: no third-party code, and the file
// opens in any image viewer.
bool write_ppm(const std::string& path, const Thumbnail& image);
bool read_ppm(const std::string& path, Thumbnail& image);

}  // namespace sc
