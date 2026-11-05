#include "core/image.hpp"

#include <cstdio>
#include <fstream>
#include <utility>

namespace sc {
namespace {

u8 average(u64 sum, u64 count) {
    if (count == 0) {
        return 0;
    }
    const u64 value = (sum + count / 2) / count;
    return static_cast<u8>(value > 255 ? 255 : value);
}

}  // namespace

Thumbnail make_thumbnail(std::span<const u8> rgba, u32 width, u32 height, u32 out_width,
                         u32 out_height, bool swizzle_bgra) {
    Thumbnail out;
    if (width == 0 || height == 0 || out_width == 0 || out_height == 0) {
        return out;
    }
    if (rgba.size() < static_cast<std::size_t>(width) * height * 4) {
        return out;
    }

    out.width = out_width;
    out.height = out_height;
    out.rgb.assign(static_cast<std::size_t>(out_width) * out_height * 3, 0);

    for (u32 ty = 0; ty < out_height; ++ty) {
        const u32 y0 = static_cast<u32>(static_cast<u64>(ty) * height / out_height);
        u32 y1 = static_cast<u32>(static_cast<u64>(ty + 1) * height / out_height);
        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        for (u32 tx = 0; tx < out_width; ++tx) {
            const u32 x0 = static_cast<u32>(static_cast<u64>(tx) * width / out_width);
            u32 x1 = static_cast<u32>(static_cast<u64>(tx + 1) * width / out_width);
            if (x1 <= x0) {
                x1 = x0 + 1;
            }

            u64 red = 0;
            u64 green = 0;
            u64 blue = 0;
            u64 count = 0;
            for (u32 y = y0; y < y1 && y < height; ++y) {
                for (u32 x = x0; x < x1 && x < width; ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
                    const u8 c0 = rgba[i + 0];
                    const u8 c1 = rgba[i + 1];
                    const u8 c2 = rgba[i + 2];
                    red += swizzle_bgra ? c2 : c0;
                    green += c1;
                    blue += swizzle_bgra ? c0 : c2;
                    ++count;
                }
            }

            const std::size_t o = (static_cast<std::size_t>(ty) * out_width + tx) * 3;
            out.rgb[o + 0] = average(red, count);
            out.rgb[o + 1] = average(green, count);
            out.rgb[o + 2] = average(blue, count);
        }
    }
    return out;
}

ImageDifference compare_thumbnails(const Thumbnail& actual, const Thumbnail& expected) {
    ImageDifference diff;
    if (actual.width != expected.width || actual.height != expected.height ||
        actual.rgb.size() != expected.rgb.size() || actual.rgb.empty()) {
        // Nothing meaningful can be compared; report a score no tolerance accepts.
        diff.max_channel_delta = 255;
        diff.mean_channel_delta = 256.0;
        diff.changed_pixels = actual.width * actual.height;
        return diff;
    }

    u64 total = 0;
    for (std::size_t i = 0; i + 2 < actual.rgb.size(); i += 3) {
        u32 worst = 0;
        for (std::size_t c = 0; c < 3; ++c) {
            const u32 a = actual.rgb[i + c];
            const u32 b = expected.rgb[i + c];
            const u32 delta = a > b ? a - b : b - a;
            total += delta;
            if (delta > worst) {
                worst = delta;
            }
        }
        if (worst != 0) {
            ++diff.changed_pixels;
        }
        if (worst > diff.max_channel_delta) {
            diff.max_channel_delta = worst;
        }
    }
    diff.mean_channel_delta =
        static_cast<double>(total) / static_cast<double>(actual.rgb.size());
    return diff;
}

bool write_ppm(const std::string& path, const Thumbnail& image) {
    if (image.empty() || image.rgb.size() != static_cast<std::size_t>(image.width) * image.height * 3) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    out.write(reinterpret_cast<const char*>(image.rgb.data()),
              static_cast<std::streamsize>(image.rgb.size()));
    return out.good();
}

bool read_ppm(const std::string& path, Thumbnail& image) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::string magic;
    in >> magic;
    if (magic != "P6") {
        return false;
    }

    u32 width = 0;
    u32 height = 0;
    u32 max_value = 0;
    in >> width >> height >> max_value;
    if (width == 0 || height == 0 || max_value != 255) {
        return false;
    }

    in.get();  // the single whitespace byte after the header

    Thumbnail loaded;
    loaded.width = width;
    loaded.height = height;
    loaded.rgb.resize(static_cast<std::size_t>(width) * height * 3);
    in.read(reinterpret_cast<char*>(loaded.rgb.data()),
            static_cast<std::streamsize>(loaded.rgb.size()));
    if (!in) {
        return false;
    }

    image = std::move(loaded);
    return true;
}

}  // namespace sc
