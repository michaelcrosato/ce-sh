#include "core/math/radiometry.h"

#include <cmath>

namespace lc::math {

float RectangleIrradianceAtCorner(float a, float b, float h, float radiance) {
    const double A = a, B = b, H = h;
    const double sa = std::sqrt(A * A + H * H);
    const double sb = std::sqrt(B * B + H * H);
    const double e = 0.5 * radiance * ((A / sa) * std::atan(B / sa) + (B / sb) * std::atan(A / sb));
    return static_cast<float>(e);
}

float RectangleIrradianceBelowCentre(float a, float b, float h, float radiance) {
    return 4.0f * RectangleIrradianceAtCorner(a * 0.5f, b * 0.5f, h, radiance);
}

float RectangleIrradianceNumerical(float a, float b, float h, float radiance, int n) {
    // Point at the origin, rectangle centred above it at height h, normals facing each other.
    const double dx = static_cast<double>(a) / n;
    const double dz = static_cast<double>(b) / n;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = -0.5 * a + (i + 0.5) * dx;
        for (int j = 0; j < n; ++j) {
            const double z = -0.5 * b + (j + 0.5) * dz;
            const double r2 = x * x + z * z + static_cast<double>(h) * h;
            const double cosP = h / std::sqrt(r2);  // Point normal +Y, direction to the emitter.
            const double cosL = cosP;               // Emitter faces -Y, same angle.
            sum += cosP * cosL / r2 * dx * dz;
        }
    }
    return static_cast<float>(radiance * sum);
}

}  // namespace lc::math
