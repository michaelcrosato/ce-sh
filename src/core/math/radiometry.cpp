#include "core/math/radiometry.h"

#include <cmath>

namespace lc::math {

namespace {

// Signed corner contribution: F(a, b) = sign(a) * sign(b) * corner(|a|, |b|).
double SignedCorner(double a, double b, double h, double radiance) {
    const double sa = std::sqrt(a * a + h * h);
    const double sb = std::sqrt(b * b + h * h);
    const double magnitude = 0.5 * radiance * ((std::fabs(a) / sa) * std::atan(std::fabs(b) / sa) + (std::fabs(b) / sb) * std::atan(std::fabs(a) / sb));
    const double sign = ((a < 0.0) ? -1.0 : 1.0) * ((b < 0.0) ? -1.0 : 1.0);
    return sign * magnitude;
}

}  // namespace

float RectangleIrradianceAtCorner(float a, float b, float h, float radiance) {
    return static_cast<float>(SignedCorner(a, b, h, radiance));
}

float RectangleIrradianceBelowCentre(float a, float b, float h, float radiance) {
    return 4.0f * RectangleIrradianceAtCorner(a * 0.5f, b * 0.5f, h, radiance);
}

float RectangleIrradianceAtPoint(float x0, float x1, float z0, float z1, float h, float radiance, float px, float pz) {
    const double e = SignedCorner(x1 - px, z1 - pz, h, radiance) - SignedCorner(x0 - px, z1 - pz, h, radiance) -
                     SignedCorner(x1 - px, z0 - pz, h, radiance) + SignedCorner(x0 - px, z0 - pz, h, radiance);
    return static_cast<float>(e);
}

float RectangleIrradianceNumericalAtPoint(float x0, float x1, float z0, float z1, float h, float radiance, float px, float pz, int n) {
    const double dx = static_cast<double>(x1 - x0) / n;
    const double dz = static_cast<double>(z1 - z0) / n;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = x0 + (i + 0.5) * dx - px;
        for (int j = 0; j < n; ++j) {
            const double z = z0 + (j + 0.5) * dz - pz;
            const double r2 = x * x + z * z + static_cast<double>(h) * h;
            const double cosP = h / std::sqrt(r2);  // Receiver normal +Y.
            const double cosL = cosP;               // Emitter faces -Y: same angle.
            sum += cosP * cosL / r2 * dx * dz;
        }
    }
    return static_cast<float>(radiance * sum);
}

float RectangleIrradianceNumerical(float a, float b, float h, float radiance, int n) {
    return RectangleIrradianceNumericalAtPoint(-0.5f * a, 0.5f * a, -0.5f * b, 0.5f * b, h, radiance, 0.0f, 0.0f, n);
}

}  // namespace lc::math
