#include "scene/two_room_level.h"

#include "core/error.h"
#include "scene/scene_file.h"

namespace lc {

using math::Mat4;
using math::Vec3;

namespace {

Mat4 PoseTransform(const PoseSpec& pose) {
    return Mat4::Translation(pose.position) * Mat4::RotationY(pose.yaw) * Mat4::RotationX(pose.pitch);
}

}  // namespace

Mat4 LampHousingTransform(const PoseSpec& pose) {
    return PoseTransform(pose) * Mat4::Translation({0.0f, kLampBaseOffset, 0.0f});
}

Mat4 LampFaceTransform(const PoseSpec& pose, float faceOffset) {
    return PoseTransform(pose) * Mat4::Translation({0.0f, kLampBaseOffset, -faceOffset}) * QuadFacing({0.0f, 0.0f, -1.0f});
}

Mat4 ThreatBodyTransform(const PoseSpec& pose) {
    return Mat4::Translation(pose.position + Vec3{0.0f, 0.6f, 0.0f}) * Mat4::RotationY(pose.yaw);
}

Mat4 ThreatHeadTransform(const PoseSpec& pose) {
    return Mat4::Translation(pose.position) * Mat4::RotationY(pose.yaw) * Mat4::Translation({0.0f, 1.35f, -0.1f});
}

Mat4 PlayerTorsoTransform(const PoseSpec& feet) {
    // Placeholder torso (spec §14: clear of the camera through placement, not ray exclusion). Centre
    // 1.0 m up and 6 cm behind the eye axis (local +Z): the front face sits under the eye and the top
    // (1.28 m) is 0.32 m below it, so the floor stays visible down to 0.3 m from the feet and the
    // chest only enters the view when the player looks almost straight down.
    return Mat4::Translation(feet.position) * Mat4::RotationY(feet.yaw) * Mat4::Translation({0.0f, 1.0f, 0.06f});
}

Mat4 PlayerHandTransform(const PoseSpec& feet, bool right) {
    // Hands at rest in front of the waist: 1.05 m up, 20 cm to the side, 16 cm forward (local -Z).
    // Their nearest corner is 81 degrees below the eye line, so they appear only at steep pitches.
    return Mat4::Translation(feet.position) * Mat4::RotationY(feet.yaw) * Mat4::Translation({right ? 0.2f : -0.2f, 1.05f, -0.16f});
}

TwoRoomLevel BuildTwoRoomLevel() {
    SceneFileResult result = LoadSceneFile(kTwoRoomSceneFile, AssetRoot());
    if (!result.Ok()) {
        throw Error("scene file " + result.sourceName + " is invalid:\n" + result.ErrorText());
    }
    return std::move(*result.level);
}

}  // namespace lc
