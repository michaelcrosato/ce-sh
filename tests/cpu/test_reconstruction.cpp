#include "lc_test.h"

#include "render/nrd_settings.h"

#include <cmath>
#include <set>

LC_TEST(nrd_matrices_are_transposed_to_column_major) {
    lc::math::Mat4 m = lc::math::Mat4::Identity();
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            m.m[r][c] = static_cast<float>(r * 10 + c);
        }
    }
    float out[16];
    lc::nrd_settings::ToColumnMajor(m, out);
    // Column-major: element (row r, column c) at index c * 4 + r. The translation column (c = 3)
    // must land in the last four floats, as NRD reads it.
    LC_CHECK_NEAR(out[0], 0.0f, 0.0f);   // (0,0)
    LC_CHECK_NEAR(out[1], 10.0f, 0.0f);  // (1,0)
    LC_CHECK_NEAR(out[4], 1.0f, 0.0f);   // (0,1)
    LC_CHECK_NEAR(out[12], 3.0f, 0.0f);  // (0,3): x translation
    LC_CHECK_NEAR(out[13], 13.0f, 0.0f); // (1,3)
    LC_CHECK_NEAR(out[15], 33.0f, 0.0f); // (3,3)
}

LC_TEST(frame_jitter_stays_in_half_pixel_range_and_does_not_repeat_within_the_sequence) {
    std::set<std::pair<int, int>> seen;
    for (std::uint32_t i = 0; i < lc::nrd_settings::kJitterSequenceLength; ++i) {
        const lc::nrd_settings::Jitter j = lc::nrd_settings::FrameJitter(i);
        LC_CHECK(j.x >= -0.5f && j.x < 0.5f);
        LC_CHECK(j.y >= -0.5f && j.y < 0.5f);
        seen.insert({static_cast<int>(std::lround(j.x * 4096.0f)), static_cast<int>(std::lround(j.y * 4096.0f))});
    }
    LC_CHECK_EQ(seen.size(), static_cast<std::size_t>(lc::nrd_settings::kJitterSequenceLength));
    // The sequence wraps: NRD sees the same offsets again after kJitterSequenceLength frames.
    const auto a = lc::nrd_settings::FrameJitter(3);
    const auto b = lc::nrd_settings::FrameJitter(3 + lc::nrd_settings::kJitterSequenceLength);
    LC_CHECK_NEAR(a.x, b.x, 0.0f);
    LC_CHECK_NEAR(a.y, b.y, 0.0f);
    // Halton(2) for index 1 is 0.5 -> offset 0; index 2 -> 0.25 -> -0.25.
    LC_CHECK_NEAR(lc::nrd_settings::RadicalInverse(1, 2), 0.5f, 1e-7f);
    LC_CHECK_NEAR(lc::nrd_settings::RadicalInverse(2, 2), 0.25f, 1e-7f);
    LC_CHECK_NEAR(lc::nrd_settings::RadicalInverse(1, 3), 1.0f / 3.0f, 1e-6f);
}
