#include "scene/camera.h"

#include <cmath>

namespace lc {

math::Vec3 Camera::RayDirection(float aspect, float u, float v) const {
    const float tanHalfFovY = std::tan(VerticalFov(aspect) * 0.5f);
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = 1.0f - v * 2.0f;
    const math::Vec3 dirView{ndcX * tanHalfFovY * aspect, ndcY * tanHalfFovY, -1.0f};
    return math::Normalize(ViewToWorld().TransformDirection(dirView));
}

std::optional<math::Vec2> Camera::ProjectToImage(float aspect, math::Vec3 worldPoint) const {
    const math::Vec3 p = WorldToView().TransformPoint(worldPoint);
    if (p.z >= -1e-6f) {
        return std::nullopt;
    }
    const float tanHalfFovY = std::tan(VerticalFov(aspect) * 0.5f);
    const float ndcX = (p.x / -p.z) / (tanHalfFovY * aspect);
    const float ndcY = (p.y / -p.z) / tanHalfFovY;
    return math::Vec2{(ndcX + 1.0f) * 0.5f, (1.0f - ndcY) * 0.5f};
}

void Camera::LookAt(math::Vec3 target) {
    const math::Vec3 d = target - position;
    const float horizontal = std::sqrt(d.x * d.x + d.z * d.z);
    // Forward is -Z at yaw 0; positive yaw turns toward -X: forward = (-sin(yaw), 0, -cos(yaw)).
    yawRadians = std::atan2(-d.x, -d.z);
    pitchRadians = std::atan2(d.y, horizontal);
}

}  // namespace lc
