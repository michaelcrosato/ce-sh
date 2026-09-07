#include "scene/two_room_level.h"

#include "scene/primitives.h"

#include <cmath>

namespace lc {

using math::Mat4;
using math::Vec3;

namespace {

std::uint32_t AddDiffuse(Scene& s, const char* name, Vec3 reflectance) {
    Material m;
    m.name = name;
    m.type = MaterialType::Diffuse;
    m.reflectance = reflectance;
    return s.AddMaterial(m);
}

std::uint32_t AddEmitter(Scene& s, const char* name, Vec3 radiance) {
    Material m;
    m.name = name;
    m.type = MaterialType::Emitter;
    m.radiance = radiance;
    m.reflectance = {0.02f, 0.02f, 0.02f};
    return s.AddMaterial(m);
}

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
    TwoRoomLevel level;
    SceneDescription& d = level.description;
    Scene& s = d.scene;
    d.name = "two_room";
    d.needsLighting = true;

    const std::uint32_t concrete = AddDiffuse(s, "painted_concrete", {0.55f, 0.55f, 0.52f});
    const std::uint32_t floorMat = AddDiffuse(s, "rubber_floor", {0.35f, 0.35f, 0.33f});
    const std::uint32_t ceilingMat = AddDiffuse(s, "ceiling", {0.7f, 0.7f, 0.7f});
    const std::uint32_t crateMat = AddDiffuse(s, "crate", {0.6f, 0.35f, 0.15f});
    const std::uint32_t doorMat = AddDiffuse(s, "painted_metal_door", {0.45f, 0.5f, 0.55f});
    const std::uint32_t lampHousingMat = AddDiffuse(s, "lamp_housing", {0.12f, 0.12f, 0.12f});
    const std::uint32_t threatMat = AddDiffuse(s, "threat_metal", {0.15f, 0.15f, 0.18f});
    const std::uint32_t sinkMat = AddDiffuse(s, "sink_block", {0.75f, 0.75f, 0.78f});
    const std::uint32_t shelfMat = AddDiffuse(s, "lamp_shelf", {0.8f, 0.6f, 0.2f});
    Material mirrorMat;
    mirrorMat.name = "mirror";
    mirrorMat.type = MaterialType::Mirror;
    mirrorMat.reflectance = {0.9f, 0.9f, 0.9f};
    const std::uint32_t mirrorMaterial = s.AddMaterial(mirrorMat);

    // Layout (metres). Room A x 0..4, z 0..4. Hall 1 x 4.15..8, z 0..2.4. Hall 2 x 5.6..8, z 2.4..8.15.
    // Room B x 4..8, z 8.15..12.15. Walls 0.15 m, ceiling 2.8 m.
    const float t = 0.15f;
    const float h = 2.8f;
    AddSlab(s, "floor", {-t, -t, -t}, {8.0f + t, 0.0f, 12.15f + t}, floorMat);
    AddSlab(s, "ceiling", {-t, h, -t}, {8.0f + t, h + t, 12.15f + t}, ceilingMat);
    // Room A.
    AddSlab(s, "a_wall_neg_x", {-t, 0.0f, -t}, {0.0f, h, 4.0f + t}, concrete);
    AddSlab(s, "a_h1_wall_neg_z", {0.0f, 0.0f, -t}, {8.0f + t, h, 0.0f}, concrete);
    AddSlab(s, "a_wall_pos_z", {0.0f, 0.0f, 4.0f}, {4.0f + t, h, 4.0f + t}, concrete);
    AddWallWithOpening(s, "a_wall_pos_x", {4.0f, 0.0f, 0.0f}, {4.0f + t, h, 4.0f}, false, WallOpening{1.2f, 0.9f, 2.1f}, concrete);
    // Hall 1 and hall 2.
    AddSlab(s, "east_wall", {8.0f, 0.0f, -t}, {8.0f + t, h, 12.15f + t}, concrete);
    AddSlab(s, "h1_wall_pos_z", {4.0f + t, 0.0f, 2.4f}, {5.6f, h, 2.4f + t}, concrete);
    AddSlab(s, "h2_wall_neg_x", {5.6f - t, 0.0f, 2.4f}, {5.6f, h, 8.0f}, concrete);
    // Room B.
    AddWallWithOpening(s, "b_wall_neg_z", {4.0f, 0.0f, 8.0f}, {8.0f, h, 8.0f + t}, true, WallOpening{6.8f, 0.9f, 2.1f}, concrete);
    AddSlab(s, "b_wall_neg_x", {4.0f - t, 0.0f, 8.0f}, {4.0f, h, 12.15f + t}, concrete);
    AddSlab(s, "b_wall_pos_z", {4.0f - t, 0.0f, 12.15f}, {8.0f + t, h, 12.15f + t}, concrete);

    // Door in Room A's +X wall, hinged at the z = 0.75 edge, swinging into the hall (+X).
    DoorLeafSpec leaf;
    leaf.hingeBase = {4.0f, 0.0f, 0.75f};
    leaf.widthDir = {0.0f, 0.0f, 1.0f};
    leaf.thicknessDir = {1.0f, 0.0f, 0.0f};
    level.door = AddDoorLeaf(s, "door", leaf, doorMat);

    // Room A contents.
    AddBox(s, "crate", {0.4f, 0.4f, 0.4f}, {0.8f, 0.4f, 3.0f}, crateMat);
    const std::uint32_t fixtureA = AddEmitter(s, "fixture_a", {6.0f, 5.8f, 5.4f});
    AddRectangleEmitter(s, "fixture_a", 0.8f, 0.8f, {2.0f, h - 0.01f, 2.0f}, {0.0f, -1.0f, 0.0f}, fixtureA);

    // Hall emergency fixture (independent supply): warm and about a third of fixture A's power, so
    // the hall and the mirror image of the threat stay readable while the hall remains dim.
    const std::uint32_t emergency = AddEmitter(s, "hall_emergency", {5.0f, 1.2f, 0.8f});
    AddRectangleEmitter(s, "hall_emergency", 0.4f, 0.4f, {6.8f, h - 0.01f, 5.0f}, {0.0f, -1.0f, 0.0f}, emergency);

    // Room B contents. Its fixture is on circuit "b", which is off in the proof: the inspection room
    // is dark apart from the hall spill, so the portable lamp's effect is the dominant change.
    const std::uint32_t fixtureB = AddEmitter(s, "fixture_b", {5.0f, 4.9f, 4.7f});
    s.SetEmitterOn(fixtureB, false);
    AddRectangleEmitter(s, "fixture_b", 0.8f, 0.8f, {6.0f, h - 0.01f, 10.0f}, {0.0f, -1.0f, 0.0f}, fixtureB);
    AddBox(s, "sink_block", {0.4f, 0.45f, 0.3f}, {7.4f, 0.45f, 10.0f}, sinkMat);
    AddSlab(s, "lamp_shelf", {4.0f, 0.9f, 10.0f}, {4.3f, 0.95f, 10.5f}, shelfMat);
    // Socket near the shelf's front edge so the emitting face overhangs it and lights the floor.
    level.shelfSocket = {"shelf", {4.25f, 0.95f, 10.25f}, -math::kPi * 0.5f};  // Faces +X into the room.

    // Mirror: a slab standing just off Room B's +Z wall, turned so that the camera at the check
    // position sees the threat's crossing point through the doorway.
    // Player feet pose in Room B facing +Z (yaw pi) toward the mirror; the eye is 1.6 m up.
    level.mirrorCheckCamera = {{5.2f, 0.0f, 10.3f}, math::kPi, 0.0f};
    const Vec3 checkEye = level.mirrorCheckCamera.position + Vec3{0.0f, 1.6f, 0.0f};
    // The mirror is turned so that this camera sees, through the doorway and down hall 2, the point in
    // hall 1 where the threat crosses the line of sight (it walks across that line along X).
    const Vec3 mirrorCentre{6.4f, 1.5f, 12.0f};
    level.threatCheckPosition = {7.0f, 0.0f, 1.9f};
    const Vec3 threatAim = level.threatCheckPosition + Vec3{0.0f, 0.9f, 0.0f};
    const Vec3 toCamera = math::Normalize(checkEye - mirrorCentre);
    const Vec3 toThreat = math::Normalize(threatAim - mirrorCentre);
    const Vec3 normal = math::Normalize(toCamera + toThreat);
    const float mirrorYaw = std::atan2(-normal.x, -normal.z);  // RotationY(yaw) maps -Z to (-sin, 0, -cos).
    {
        const MeshId mesh = s.AddMesh(MakeBox("mirror", {0.6f, 0.8f, 0.01f}));
        level.mirror = s.AddInstance("mirror", mesh, Mat4::Translation(mirrorCentre) * Mat4::RotationY(mirrorYaw), mirrorMaterial);
    }
    level.mirrorCheckPoint = mirrorCentre - normal * 0.01f;  // On the front face.
    level.hallCheckPoint = level.threatCheckPosition;

    // Lamp fixture: housing box and emitting face under one pose; starts on the Room A floor, in
    // front of and below the player's start so it is picked by looking down at it.
    level.floorSocket = {"floor_a", {3.0f, 0.0f, 0.9f}, -math::kPi * 0.5f};  // Base on the floor, faces +X toward the door.
    // A small, bright face (about a tenth of fixture A's power): enough to light the way in the dark rooms.
    level.lampMaterial = AddEmitter(s, "lamp_face", {120.0f, 112.0f, 100.0f});
    const PoseSpec lampPose{level.floorSocket.position, level.floorSocket.yaw, 0.0f};
    {
        const MeshId housing = s.AddMesh(MakeBox("lamp_housing", level.lampHousingHalf));
        level.lampHousing = s.AddInstance("lamp_housing", housing, LampHousingTransform(lampPose), lampHousingMat);
        const MeshId face = s.AddMesh(MakeQuadXZ("lamp_face", 0.03f, 0.03f));
        level.lampFace = s.AddInstance("lamp_face", face, LampFaceTransform(lampPose, level.lampFaceOffset), level.lampMaterial);
    }

    // Threat: rigid two-part machine crossing hall 1 along X (z = 1.9, beside the player's route at
    // z = 1.2), parked at the check position for the static scene.
    level.threatPath = {{4.6f, 0.0f, 1.9f}, {7.7f, 0.0f, 1.9f}};
    level.threatSpeed = 1.2f;
    const PoseSpec threatPose{level.threatCheckPosition, 0.0f, 0.0f};
    {
        const MeshId body = s.AddMesh(MakeBox("threat_body", {0.25f, 0.6f, 0.2f}));
        level.threatBody = s.AddInstance("threat_body", body, ThreatBodyTransform(threatPose), threatMat);
        const MeshId head = s.AddMesh(MakeBox("threat_head", {0.15f, 0.15f, 0.15f}));
        level.threatHead = s.AddInstance("threat_head", head, ThreatHeadTransform(threatPose), threatMat);
    }

    // Player: 1.5 m from the door on the doorway's centre line, facing +X toward it.
    level.playerStart = {{2.5f, 0.0f, 1.2f}, -math::kPi * 0.5f, 0.0f};
    level.hallFloorId = s.Instances()[0].id;  // The floor slab is the first instance.

    // Static description: camera at the mirror check position; expectations for the static proof.
    d.camera.position = checkEye;
    d.camera.yawRadians = level.mirrorCheckCamera.yaw;
    d.camera.pitchRadians = level.mirrorCheckCamera.pitch;
    HitExpectation seesThreat;
    seesThreat.worldPoint = level.mirrorCheckPoint;
    seesThreat.expectedStableId = level.threatBody.value;
    seesThreat.description = "mirror shows the threat in the hall (outside the direct view)";
    d.expectations = {seesThreat};
    RadianceExpectation lit;
    lit.kind = RadianceExpectation::Kind::PositivePatch;
    lit.description = "mirror image of the threat is lit by the hall fixture";
    lit.point = level.mirrorCheckPoint;
    // Derived, not tuned: the emergency fixture (luminance ~0.8, 0.09 m^2, ~2.8 m above) gives the threat
    // an irradiance of roughly 6e-3; with albedo 0.15 and the 0.9 mirror that is ~2.6e-4 radiance.
    lit.minimum = 1e-4f;
    d.radianceExpectations = {lit};
    d.statsPatches = {{"mirror_threat", level.mirrorCheckPoint, 2}};
    return level;
}

}  // namespace lc
