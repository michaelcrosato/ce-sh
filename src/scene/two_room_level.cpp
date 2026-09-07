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

TwoRoomLevel BuildTwoRoomLevel() {
    SceneFileResult result = LoadSceneFile(kTwoRoomSceneFile, AssetRoot());
    if (!result.Ok()) {
        throw Error("scene file " + result.sourceName + " is invalid:\n" + result.ErrorText());
    }
    return std::move(*result.level);
}

}  // namespace lc
