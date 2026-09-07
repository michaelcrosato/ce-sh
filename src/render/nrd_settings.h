// Portable helpers for the NRD integration (no graphics types): matrix conversion to NRD's
// column-major layout, the per-frame camera jitter sequence, and the fixed contract values shared
// by the guided trace and the denoiser settings. Covered by tests/cpu/test_reconstruction.cpp.
#pragma once

#include "core/math/mat.h"

#include <cstdint>

namespace lc::nrd_settings {

// NRD wants column-vector math with column-major storage: element (row r, column c) at c * 4 + r.
// lc::math::Mat4 stores m[row][col], so this is a transpose of the memory order.
inline void ToColumnMajor(const math::Mat4& m, float out[16]) {
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            out[c * 4 + r] = m.m[r][c];
        }
    }
}

// Van der Corput radical inverse in the given base for index >= 1 (index 0 maps to 0).
inline float RadicalInverse(std::uint32_t index, std::uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

// Sub-pixel camera offset for a frame, in pixels within [-0.5, 0.5): Halton(2, 3) over a
// sequence of kJitterSequenceLength frames, the same offset for every pixel of the frame (NRD is
// told the identical value through CommonSettings::cameraJitter).
inline constexpr std::uint32_t kJitterSequenceLength = 32;

struct Jitter {
    float x = 0.0f;
    float y = 0.0f;
};

inline Jitter FrameJitter(std::uint32_t frameIndex) {
    const std::uint32_t i = (frameIndex % kJitterSequenceLength) + 1;
    return {RadicalInverse(i, 2) - 0.5f, RadicalInverse(i, 3) - 0.5f};
}

// Contract values (docs/RENDERING.md, "Denoiser buffer contract").
inline constexpr float kDenoisingRangeMetres = 500.0f;         // Beyond every scene; misses are written past it.
inline constexpr float kHitDistanceA = 3.0f;                    // nrd::ReblurHitDistanceParameters defaults, metres.
inline constexpr float kHitDistanceB = 0.1f;
inline constexpr float kHitDistanceC = 20.0f;

}  // namespace lc::nrd_settings
