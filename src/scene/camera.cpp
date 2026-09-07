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

}  // namespace lc
