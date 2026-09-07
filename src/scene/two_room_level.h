// The spec §4 first-proof level: Room A (equipment) with a fixture, crate, portable lamp, and a
// door; a hall with one right-angle turn; Room B (inspection) with a sink block, an angled flat
// mirror, and a lamp shelf. Built from the generated kit; entity handles let the game layer drive
// the door, lamp, and threat. No game types here.
#pragma once

#include "scene/builtin_scenes.h"
#include "scene/rooms.h"

#include <string>
#include <vector>

namespace lc {

struct PoseSpec {
    math::Vec3 position;      // Feet (player) or base point (objects).
    float yaw = 0.0f;         // Positive turns toward -X at yaw 0 (camera convention).
    float pitch = 0.0f;
};

struct SocketSpec {
    std::string name;
    math::Vec3 position;      // Lamp base position when placed.
    float yaw = 0.0f;         // Lamp facing.
};

struct TwoRoomLevel {
    SceneDescription description;   // Static scene, camera at the mirror check position, static expectations.

    DoorHandle door;                // Room A door into the hall.
    std::uint32_t lampMaterial = 0; // Emitter material of the lamp face.
    InstanceId lampHousing;
    InstanceId lampFace;
    SocketSpec floorSocket;         // Where the lamp starts (Room A floor).
    SocketSpec shelfSocket;         // Marked shelf in Room B.
    InstanceId threatBody;
    InstanceId threatHead;
    std::vector<math::Vec3> threatPath;  // Waypoints on the hall floor.
    float threatSpeed = 1.2f;
    math::Vec3 threatCheckPosition;      // Position on the path where the mirror shows the threat.
    InstanceId mirror;
    PoseSpec playerStart;           // Room A, facing the door.
    PoseSpec mirrorCheckCamera;     // Room B, facing the mirror.
    math::Vec3 mirrorCheckPoint;    // Point on the mirror face whose reflection reaches the threat.
    math::Vec3 hallCheckPoint;      // Hall floor point seen through the mirror when the threat is absent.
    InstanceId hallFloorId;         // What the mirror shows there.

    // Lamp fixture geometry (local frame: forward -Z, origin at the base resting on the socket).
    math::Vec3 lampHousingHalf{0.05f, 0.05f, 0.10f};
    float lampFaceOffset = 0.101f;  // Distance of the emitting face from the fixture origin along -Z.
};

// The fixture origin is its base; the housing box is raised by its half height so it rests on the socket.
inline constexpr float kLampBaseOffset = 0.05f;

// Builds the level with the threat parked at threatCheckPosition (so the static scene proves the
// mirror identity) and the lamp on its floor socket, switched on.
TwoRoomLevel BuildTwoRoomLevel();

// Fixture transforms shared by the scene builder and the game: housing and face from one pose.
math::Mat4 LampHousingTransform(const PoseSpec& pose);
math::Mat4 LampFaceTransform(const PoseSpec& pose, float faceOffset);
math::Mat4 ThreatBodyTransform(const PoseSpec& pose);
math::Mat4 ThreatHeadTransform(const PoseSpec& pose);

}  // namespace lc
