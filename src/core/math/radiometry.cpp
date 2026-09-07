#include "core/math/radiometry.h"

#include <algorithm>
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

namespace {

double GgxLambda(double alpha, double cosTheta) {
    const double c2 = std::max(cosTheta * cosTheta, 1e-12);
    const double tan2 = (1.0 - c2) / c2;
    return (-1.0 + std::sqrt(1.0 + alpha * alpha * tan2)) * 0.5;
}

}  // namespace

float GgxDirectionalAlbedo(float cosThetaO, float alpha, int uSteps, int phiSteps) {
    // E(v) = integral over l of D(h) G2(v, l) / (4 n.v) dl. With dl = 4 (v.h) dh this becomes
    // integral over h of [G2 (v.h) / ((n.v)(n.h))] * D(h)(n.h) dh, and D(h)(n.h) dh is exactly the
    // uniform measure in (u, phi) under the GGX parameterization tan^2(theta_h) = a^2 u / (1 - u).
    // A midpoint grid in (u, phi) therefore resolves the lobe at any roughness.
    const double cosO = cosThetaO;
    const double sinO = std::sqrt(std::max(0.0, 1.0 - cosO * cosO));
    const double a = alpha;
    const double a2 = a * a;
    const double pi = 3.14159265358979323846;
    const double lambdaO = GgxLambda(a, cosO);
    double sum = 0.0;
    for (int i = 0; i < uSteps; ++i) {
        const double u = (i + 0.5) / uSteps;
        const double cos2 = (1.0 - u) / (1.0 - u + a2 * u);
        const double cosH = std::sqrt(cos2);
        const double sinH = std::sqrt(std::max(0.0, 1.0 - cos2));
        for (int j = 0; j < phiSteps; ++j) {
            const double phi = 2.0 * pi * (j + 0.5) / phiSteps;
            const double hx = sinH * std::cos(phi);
            const double hy = sinH * std::sin(phi);
            const double hz = cosH;
            const double vDotH = hx * sinO + hz * cosO;
            if (vDotH <= 0.0) continue;
            // l = 2 (v.h) h - v
            const double lx = 2.0 * vDotH * hx - sinO;
            const double ly = 2.0 * vDotH * hy;
            const double lz = 2.0 * vDotH * hz - cosO;
            (void)ly;
            if (lz <= 0.0) continue;  // Reflected below the horizon: single-scattering loss.
            const double g2 = 1.0 / (1.0 + lambdaO + GgxLambda(a, lz));
            sum += g2 * vDotH / (cosO * hz);
            (void)lx;
        }
    }
    return static_cast<float>(sum / (static_cast<double>(uSteps) * phiSteps));
}

}  // namespace lc::math
