// Lossless image encoders with no third-party code: PNG (stored, uncompressed deflate blocks) for
// display captures and PFM (portable float map) for linear high-dynamic-range captures.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace lc {

// 8-bit RGBA, top-down rows, 4 bytes per pixel, no padding.
struct ImageRgba8 {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

// 32-bit float RGB, top-down rows, 3 floats per pixel, scene-linear values.
struct ImageRgbF32 {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> pixels;
};

std::uint32_t Crc32(std::span<const std::uint8_t> data);
std::uint32_t Adler32(std::span<const std::uint8_t> data);

// Returns the complete PNG file bytes (8-bit RGBA, colour type 6, no interlace).
std::vector<std::uint8_t> EncodePng(const ImageRgba8& image);

// Returns the complete PFM file bytes ("PF", little-endian, rows stored bottom-up as the format requires).
std::vector<std::uint8_t> EncodePfm(const ImageRgbF32& image);

}  // namespace lc
