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
