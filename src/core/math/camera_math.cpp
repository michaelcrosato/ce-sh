#include "core/math/camera_math.h"

#include <cmath>

namespace lc::math {

Mat4 LookAtRh(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = Normalize(target - eye);
    const Vec3 s = Normalize(Cross(f, up));
    const Vec3 u = Cross(s, f);
    Mat4 r = Mat4::Identity();
    r.m[0][0] = s.x; r.m[0][1] = s.y; r.m[0][2] = s.z; r.m[0][3] = -Dot(s, eye);
    r.m[1][0] = u.x; r.m[1][1] = u.y; r.m[1][2] = u.z; r.m[1][3] = -Dot(u, eye);
    r.m[2][0] = -f.x; r.m[2][1] = -f.y; r.m[2][2] = -f.z; r.m[2][3] = Dot(f, eye);
    return r;
}

Mat4 PerspectiveRhZeroToOne(float fovYRadians, float aspect, float nearZ, float farZ) {
    const float yScale = 1.0f / std::tan(fovYRadians * 0.5f);
    const float xScale = yScale / aspect;
    Mat4 r;
    r.m[0][0] = xScale;
    r.m[1][1] = yScale;
    r.m[2][2] = farZ / (nearZ - farZ);
    r.m[2][3] = nearZ * farZ / (nearZ - farZ);
    r.m[3][2] = -1.0f;
    r.m[3][3] = 0.0f;
    return r;
}

float HorizontalToVerticalFov(float horizontalFovRadians, float aspect) {
    return 2.0f * std::atan(std::tan(horizontalFovRadians * 0.5f) / aspect);
}

Mat4 ViewToWorldFromYawPitch(Vec3 position, float yawRadians, float pitchRadians) {
    return Mat4::Translation(position) * Mat4::RotationY(yawRadians) * Mat4::RotationX(pitchRadians);
}

}  // namespace lc::math
