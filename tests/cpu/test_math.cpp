#include "lc_test.h"

#include "core/math/camera_math.h"
#include "core/math/mat.h"
#include "core/math/vec.h"

using lc::math::Mat3x4;
using lc::math::Mat4;
using lc::math::Vec3;
using lc::math::Vec4;

LC_TEST(math_cross_is_right_handed) {
    const Vec3 z = lc::math::Cross({1, 0, 0}, {0, 1, 0});
    LC_CHECK(lc::math::NearlyEqual(z, {0, 0, 1}, 1e-7f));
}

LC_TEST(math_rotation_y_maps_plus_x_to_minus_z) {
    const Vec3 p = Mat4::RotationY(lc::math::kPi / 2).TransformPoint({1, 0, 0});
    LC_CHECK_NEAR(p.x, 0, 1e-6);
    LC_CHECK_NEAR(p.y, 0, 1e-6);
    LC_CHECK_NEAR(p.z, -1, 1e-6);
}

LC_TEST(math_rotation_x_maps_plus_y_to_plus_z) {
    const Vec3 p = Mat4::RotationX(lc::math::kPi / 2).TransformPoint({0, 1, 0});
    LC_CHECK_NEAR(p.x, 0, 1e-6);
    LC_CHECK_NEAR(p.y, 0, 1e-6);
    LC_CHECK_NEAR(p.z, 1, 1e-6);
}

LC_TEST(math_product_applies_right_factor_first) {
    // M = T * R applies R first, then T (column vectors).
    const Mat4 m = Mat4::Translation({10, 0, 0}) * Mat4::RotationZ(lc::math::kPi / 2);
    const Vec3 p = m.TransformPoint({1, 0, 0});  // R: (1,0,0) -> (0,1,0); T: -> (10,1,0).
    LC_CHECK_NEAR(p.x, 10, 1e-6);
    LC_CHECK_NEAR(p.y, 1, 1e-6);
    LC_CHECK_NEAR(p.z, 0, 1e-6);
}

LC_TEST(math_translation_lives_in_column_three) {
    const Mat4 t = Mat4::Translation({1, 2, 3});
    LC_CHECK_NEAR(t.m[0][3], 1, 0);
    LC_CHECK_NEAR(t.m[1][3], 2, 0);
    LC_CHECK_NEAR(t.m[2][3], 3, 0);
    LC_CHECK_NEAR(t.m[3][0], 0, 0);
    const Vec4 v = t * Vec4{0, 0, 0, 1};
    LC_CHECK_NEAR(v.z, 3, 0);
}

LC_TEST(math_direction_ignores_translation) {
    const Vec3 d = Mat4::Translation({5, 6, 7}).TransformDirection({0, 0, 1});
    LC_CHECK(lc::math::NearlyEqual(d, {0, 0, 1}, 1e-7f));
}

LC_TEST(math_inverse_round_trip_with_nonuniform_scale) {
    const Mat4 m = Mat4::Translation({1, -2, 3}) * Mat4::RotationAxis(lc::math::Normalize({1, 2, 3}), 0.7f) *
                   Mat4::Scale({2, 3, 4});
    Mat4 inv;
    LC_REQUIRE(m.TryInverse(inv));
    LC_CHECK(lc::math::NearlyEqual(inv * m, Mat4::Identity(), 1e-5f));
    LC_CHECK(lc::math::NearlyEqual(m * inv, Mat4::Identity(), 1e-5f));
    LC_CHECK_NEAR(m.Determinant(), 24.0f, 1e-3);
}

LC_TEST(math_singular_matrix_has_no_inverse) {
    Mat4 out;
    LC_CHECK(!Mat4::Scale({1, 0, 1}).TryInverse(out));
}

LC_TEST(math_transpose_swaps_indices) {
    Mat4 m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m.m[r][c] = static_cast<float>(r * 10 + c);
    const Mat4 t = m.Transposed();
    LC_CHECK_NEAR(t.m[3][0], 3, 0);
    LC_CHECK_NEAR(t.m[0][3], 30, 0);
}

LC_TEST(math_lookat_maps_target_to_negative_z) {
    const Mat4 view = lc::math::LookAtRh({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
    const Vec3 origin = view.TransformPoint({0, 0, 0});
    LC_CHECK_NEAR(origin.x, 0, 1e-6);
    LC_CHECK_NEAR(origin.y, 0, 1e-6);
    LC_CHECK_NEAR(origin.z, -5, 1e-6);
    const Vec3 right = view.TransformPoint({1, 0, 5});
    LC_CHECK_NEAR(right.x, 1, 1e-6);
    const Vec3 up = view.TransformPoint({0, 1, 5});
    LC_CHECK_NEAR(up.y, 1, 1e-6);
}

LC_TEST(math_perspective_depth_range_zero_to_one) {
    const float nearZ = 0.1f;
    const float farZ = 100.0f;
    const Mat4 proj = lc::math::PerspectiveRhZeroToOne(lc::math::DegreesToRadians(60.0f), 16.0f / 9.0f, nearZ, farZ);
    const Vec4 n = proj * Vec4{0, 0, -nearZ, 1};
    LC_CHECK_NEAR(n.z / n.w, 0, 1e-6);
    const Vec4 f = proj * Vec4{0, 0, -farZ, 1};
    LC_CHECK_NEAR(f.z / f.w, 1, 1e-5);
    const Vec4 r = proj * Vec4{1, 0, -2, 1};
    LC_CHECK(r.x / r.w > 0.0f);
    const Vec4 u = proj * Vec4{0, 1, -2, 1};
    LC_CHECK(u.y / u.w > 0.0f);
    LC_CHECK(r.w > 0.0f);  // w is the positive view distance.
}

LC_TEST(math_horizontal_fov_conversion) {
    const float vfov = lc::math::HorizontalToVerticalFov(lc::math::DegreesToRadians(90.0f), 16.0f / 9.0f);
    LC_CHECK_NEAR(lc::math::RadiansToDegrees(vfov), 58.7155f, 1e-2);
}

LC_TEST(math_yaw_pitch_camera_basis) {
    // Yaw +90 degrees turns the -Z forward toward -X.
    const Mat4 v2w = lc::math::ViewToWorldFromYawPitch({1, 2, 3}, lc::math::kPi / 2, 0.0f);
    const Vec3 forward = v2w.TransformDirection({0, 0, -1});
    LC_CHECK(lc::math::NearlyEqual(forward, {-1, 0, 0}, 1e-6f));
    LC_CHECK(lc::math::NearlyEqual(v2w.TranslationPart(), {1, 2, 3}, 0.0f));
    // Pitch +45 degrees looks up.
    const Vec3 up = lc::math::ViewToWorldFromYawPitch({0, 0, 0}, 0.0f, lc::math::kPi / 4).TransformDirection({0, 0, -1});
    LC_CHECK(up.y > 0.7f);
    LC_CHECK(up.z < -0.7f);
}

LC_TEST(math_mat3x4_copies_the_first_three_rows) {
    Mat4 m;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m.m[r][c] = static_cast<float>(r * 10 + c);
    const Mat3x4 a = Mat3x4::FromMat4(m);
    LC_CHECK_NEAR(a.m[0][3], 3, 0);
    LC_CHECK_NEAR(a.m[2][1], 21, 0);
    LC_CHECK_NEAR(a.m[1][0], 10, 0);
}

LC_TEST(math_normalize_zero_is_zero) {
    const Vec3 z = lc::math::Normalize({0, 0, 0});
    LC_CHECK(z == Vec3{});
    LC_CHECK_NEAR(lc::math::Length(lc::math::Normalize({3, 4, 0})), 1, 1e-6);
}
