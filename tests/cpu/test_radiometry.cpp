#include "lc_test.h"

#include "core/math/radiometry.h"
#include "core/math/vec.h"

LC_TEST(radiometry_rectangle_irradiance_matches_numerical_integration) {
    const float cases[][3] = {{1.0f, 0.5f, 1.5f}, {2.0f, 2.0f, 0.8f}, {0.3f, 4.0f, 2.0f}};
    for (const auto& c : cases) {
        const float analytic = lc::math::RectangleIrradianceBelowCentre(c[0], c[1], c[2], 4.0f);
        const float numerical = lc::math::RectangleIrradianceNumerical(c[0], c[1], c[2], 4.0f, 400);
        LC_CHECK(analytic > 0.0f);
        LC_CHECK_NEAR(analytic / numerical, 1.0f, 1e-3);
    }
}

LC_TEST(radiometry_huge_rectangle_approaches_pi_times_radiance) {
    const float e = lc::math::RectangleIrradianceBelowCentre(10000.0f, 10000.0f, 1.0f, 1.0f);
    LC_CHECK_NEAR(e, lc::math::kPi, 1e-3);
    const float corner = lc::math::RectangleIrradianceAtCorner(10000.0f, 10000.0f, 1.0f, 1.0f);
    LC_CHECK_NEAR(corner, lc::math::kPi / 4.0f, 1e-3);
}

LC_TEST(radiometry_ggx_directional_albedo_is_bounded_and_monotonic) {
    // Smooth conductor: nearly all energy is reflected. Rougher: single scattering loses energy.
    const float cos45 = 0.70710678f;
    const float smooth = lc::math::GgxDirectionalAlbedo(cos45, 0.02f * 0.02f, 512, 512);
    const float medium = lc::math::GgxDirectionalAlbedo(cos45, 0.35f * 0.35f, 512, 512);
    const float rough = lc::math::GgxDirectionalAlbedo(cos45, 1.0f, 512, 512);
    LC_CHECK(smooth > 0.99f && smooth <= 1.0f + 1e-3f);
    LC_CHECK(medium < smooth);
    LC_CHECK(rough < medium);
    LC_CHECK(rough > 0.25f && rough < 0.5f);
    // Closed form at normal incidence with alpha = 1: D = 1/pi, Lambda = (sec - 1)/2, so
    // E = integral of sin cos / (1 + cos) = 1 - ln 2.
    const float normal = lc::math::GgxDirectionalAlbedo(1.0f, 1.0f, 1024, 1024);
    LC_CHECK_NEAR(normal, 1.0f - 0.69314718f, 2e-3f);
    // Grid refinement changes the medium value by less than 0.1 %.
    const float mediumFine = lc::math::GgxDirectionalAlbedo(cos45, 0.35f * 0.35f, 2048, 2048);
    LC_CHECK_NEAR(medium / mediumFine, 1.0f, 1e-3f);
}

LC_TEST(radiometry_arbitrary_point_matches_numerical_integration) {
    // Points under the footprint, on its edge, and well outside it.
    const float points[][2] = {{0.0f, 0.0f}, {0.3f, -0.1f}, {0.5f, 0.0f}, {0.75f, 0.2f}, {-1.4f, 0.9f}};
    for (const auto& p : points) {
        const float analytic = lc::math::RectangleIrradianceAtPoint(-0.5f, 0.5f, -0.25f, 0.25f, 1.5f, 4.0f, p[0], p[1]);
        const float numerical = lc::math::RectangleIrradianceNumericalAtPoint(-0.5f, 0.5f, -0.25f, 0.25f, 1.5f, 4.0f, p[0], p[1], 400);
        LC_CHECK(analytic > 0.0f);
        LC_CHECK_NEAR(analytic / numerical, 1.0f, 2e-3);
    }
    // Consistency with the centred formula.
    LC_CHECK_NEAR(lc::math::RectangleIrradianceAtPoint(-0.5f, 0.5f, -0.25f, 0.25f, 1.5f, 4.0f, 0.0f, 0.0f),
                  lc::math::RectangleIrradianceBelowCentre(1.0f, 0.5f, 1.5f, 4.0f), 1e-6);
}
