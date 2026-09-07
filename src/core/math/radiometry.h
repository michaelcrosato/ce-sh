// Closed-form radiometry used as the reference for the integrator tests (spec §19 T08/T09).
// Radiance is scene-linear and unitless: an emitter of radiance L covering the whole hemisphere
// produces irradiance pi * L.
#pragma once

namespace lc::math {

// Irradiance at a point on a plane parallel to a Lambertian rectangle of sides a and b at height h,
// with the point directly below one corner of the rectangle. Standard configuration-factor result.
float RectangleIrradianceAtCorner(float a, float b, float h, float radiance);

// Same, with the point directly below the rectangle's centre (sum of four corner rectangles).
float RectangleIrradianceBelowCentre(float a, float b, float h, float radiance);

// Midpoint-rule numerical integration of L * cos_p * cos_l / r^2 over an n x n grid; test use only.
float RectangleIrradianceNumerical(float a, float b, float h, float radiance, int n);

}  // namespace lc::math
