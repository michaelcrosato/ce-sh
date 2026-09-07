// Player camera state and its ray generation contract (mirrored by camera_view.hlsl / path_trace.hlsl).
#pragma once

#include "core/math/camera_math.h"

#include <optional>

namespace lc {

struct Camera {
    math::Vec3 position{0.0f, 1.6f, 0.0f};
    float yawRadians = 0.0f;    // Positive turns toward -X (left when facing -Z).
    float pitchRadians = 0.0f;  // Positive looks up.
    float horizontalFovRadians = math::DegreesToRadians(90.0f);
    float nearZ = 0.05f;
    float farZ = 200.0f;

    math::Mat4 ViewToWorld() const { return math::ViewToWorldFromYawPitch(position, yawRadians, pitchRadians); }
    math::Mat4 WorldToView() const { return ViewToWorld().Inverse(); }
    float VerticalFov(float aspect) const { return math::HorizontalToVerticalFov(horizontalFovRadians, aspect); }
    math::Vec3 Forward() const { return ViewToWorld().TransformDirection({0.0f, 0.0f, -1.0f}); }

    // World-space direction of the camera ray through normalized image coordinates (u right, v down,
    // both in [0, 1]); identical to CameraRayDirection in the shaders.
    math::Vec3 RayDirection(float aspect, float u, float v) const;

    // Normalized image coordinates of a world point, or empty when it is behind the camera.
    std::optional<math::Vec2> ProjectToImage(float aspect, math::Vec3 worldPoint) const;

    // Aims the camera at a target from its current position (yaw and pitch only).
    void LookAt(math::Vec3 target);
};

}  // namespace lc
