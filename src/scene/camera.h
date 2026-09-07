// Player camera state and its ray generation contract (mirrored by camera_view.hlsl).
#pragma once

#include "core/math/camera_math.h"

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
    // both in [0, 1]); identical to CameraRayDirection in camera_view.hlsl.
    math::Vec3 RayDirection(float aspect, float u, float v) const;
};

}  // namespace lc
