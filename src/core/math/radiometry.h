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

// Irradiance at (px, pz) on the plane y = 0 from the rectangle [x0, x1] x [z0, z1] at height h
// facing down (signed corner decomposition; the point may lie outside the footprint).
float RectangleIrradianceAtPoint(float x0, float x1, float z0, float z1, float h, float radiance, float px, float pz);

// Midpoint-rule numerical integration of L * cos_p * cos_l / r^2 over an n x n grid for the point
// below the centre; test use only.
float RectangleIrradianceNumerical(float a, float b, float h, float radiance, int n);

// Same for an arbitrary point (px, pz) under/beside the rectangle [x0, x1] x [z0, z1].
float RectangleIrradianceNumericalAtPoint(float x0, float x1, float z0, float z1, float h, float radiance, float px, float pz, int n);

// Directional albedo of the single-scattering GGX conductor BRDF used by the renderer (height-
// correlated Smith masking, Fresnel = 1): E(theta_o) = integral over the hemisphere of
// D * G2 / (4 cos_o) d(omega_i), integrated numerically. Under uniform illumination of radiance L
// a conductor with F0 = 1 reflects exactly L * E(theta_o) (spec §19 T10 reference).
float GgxDirectionalAlbedo(float cosThetaO, float alpha, int thetaSteps, int phiSteps);

}  // namespace lc::math
