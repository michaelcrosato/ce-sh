// View and projection matrices for the project camera contract:
//   * view space is right-handed; the camera looks along -Z with +X right and +Y up;
//   * clip space uses the Direct3D depth range [0, 1] with the near plane at 0;
//   * the field of view stored in settings is horizontal; the vertical one derives from the aspect.
#pragma once

#include "core/math/mat.h"

namespace lc::math {

// World -> view matrix for a camera at eye looking at target.
Mat4 LookAtRh(Vec3 eye, Vec3 target, Vec3 up);

// View -> clip matrix. fovYRadians is the full vertical field of view. Depth: near -> 0, far -> 1.
Mat4 PerspectiveRhZeroToOne(float fovYRadians, float aspect, float nearZ, float farZ);

// Vertical field of view for a horizontal one at the given width/height aspect ratio.
float HorizontalToVerticalFov(float horizontalFovRadians, float aspect);

// Camera -> world for a yaw/pitch camera: Translation(position) * RotationY(yaw) * RotationX(pitch).
// Positive yaw turns the view toward -X (left when facing -Z); positive pitch looks up.
Mat4 ViewToWorldFromYawPitch(Vec3 position, float yawRadians, float pitchRadians);

}  // namespace lc::math
