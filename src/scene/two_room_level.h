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
    math::Vec3 position;      // Item base position when placed.
    float yaw = 0.0f;         // Item facing.
    std::vector<std::string> accepts;  // Item ids this socket takes (schema 2).
    std::string text;         // Player-facing name ("shelf", "fuse box").
};

struct CircuitState {
    std::string id;
    bool on = true;
    // Schema 2: a circuit powered by an item sitting in a socket (the fuse) is on exactly when it is there.
    std::string poweredByItem;
    std::string poweredBySocket;
};

// Schema 2 entities (spec §5, §15): several doors, carried items, fans, and the objective's steps.
struct LevelDoor {
    std::string id;
    DoorHandle handle;
    bool locked = false;              // Ignores the interaction key.
    std::string opensWithCircuit;     // Opens when this circuit turns on (the exit door).
};

struct LevelItem {
    std::string id;
    std::string text;                 // Display name ("lamp", "fuse").
    InstanceId body;                  // The carried box (lamp housing, fuse body).
    InstanceId face;                  // Lit items: the emitting quad.
    bool hasLight = false;
    std::uint32_t lightMaterial = 0;  // Lit items: the emitter material index.
    float faceOffset = 0.101f;        // Lit items: face distance from the fixture origin along -Z.
    math::Vec3 half;                  // Body box half extents.
    std::string startSocket;
    bool hidesWhenCarried = false;    // Parked inside the player's torso while carried (a pocketed fuse).
};

struct LevelFan {
    std::string id;
    InstanceId hub;
    std::vector<InstanceId> blades;
    std::vector<math::Mat4> bladeLocal;  // Blade transforms relative to the hub at angle 0.
    math::Mat4 hubLocal;
    math::Vec3 centre;
    math::Vec3 axis;                  // Unit rotation axis.
    float rpm = 0.0f;
    std::string circuit;              // Turns while this circuit is on.
};

struct ObjectiveStep {
    std::string id;                   // The phase name once completed.
    std::string kind;                 // take, place, reach.
    std::string item;                 // take, place.
    std::string socket;               // place.
    std::string marker;               // reach.
    math::Vec3 markerPosition;        // reach: resolved marker.
    float radius = 0.8f;              // reach.
    std::string requiresItem;         // reach: the item must be held.
    std::string text;                 // The objective line while this step is pending.
};

struct TwoRoomLevel {
    SceneDescription description;   // Static scene, camera at the mirror check position, static expectations.
    std::string sceneFile;          // Source scene file (assets/scenes/two_room.json) and its content hash.
    std::uint64_t contentHash = 0;
    std::vector<CircuitState> circuits;   // Initial circuit states from the file.
    std::vector<SocketSpec> sockets;      // Every interaction socket (floorSocket and shelfSocket are two of them).
    std::vector<InstanceId> colliders;    // Instances that block movement (M5 collision), from the file's collider flags.
    // Schema 2 lists (the fields below them mirror the first door and the lit item for the proof's code paths).
    std::vector<LevelDoor> doors;
    std::vector<LevelItem> items;
    std::vector<LevelFan> fans;
    std::vector<ObjectiveStep> steps;
    std::string objectiveCompleteText;
    std::vector<std::pair<std::uint32_t, std::string>> emitterCircuits;  // Emitter material index -> circuit id.

    DoorHandle door;                // The first door (Room A into the hall in the proof).
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
    math::Vec3 mirrorNormal;        // Unit normal of the mirror's reflecting face (toward the room).
    PoseSpec playerStart;           // Room A, facing the door.
    // Placeholder body (spec §14): rigid boxes that follow the player pose below and beside the
    // camera so reflections and shadows show the player; kept clear of the camera by placement.
    InstanceId playerTorso;
    InstanceId playerHandLeft;
    InstanceId playerHandRight;
    // Objective exit (spec §15): reached with the lamp held to finish the proof's route.
    bool hasExit = false;
    math::Vec3 exitPosition;
    float exitRadius = 0.8f;
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

// Loads assets/scenes/two_room.json from the asset root (scene_file.h) with the threat parked at
// threatCheckPosition (so the static scene proves the mirror identity) and the lamp on its floor
// socket, switched on. Throws lc::Error listing every problem when the file is invalid.
TwoRoomLevel BuildTwoRoomLevel();
inline constexpr const char* kTwoRoomSceneFile = "scenes/two_room.json";

using Level = TwoRoomLevel;  // The struct outgrew its name in M6; both names are the same type.

// Fixture transforms shared by the scene builder and the game: housing and face from one pose.
math::Mat4 LampHousingTransform(const PoseSpec& pose);
math::Mat4 LampFaceTransform(const PoseSpec& pose, float faceOffset);
// A plain item's body resting on its socket (the box's bottom on the socket point).
math::Mat4 ItemBodyTransform(const PoseSpec& pose, math::Vec3 half);
math::Mat4 ThreatBodyTransform(const PoseSpec& pose);
math::Mat4 ThreatHeadTransform(const PoseSpec& pose);
// Player body parts from the feet pose (yaw only: the body does not pitch with the view).
math::Mat4 PlayerTorsoTransform(const PoseSpec& feet);
math::Mat4 PlayerHandTransform(const PoseSpec& feet, bool right);

}  // namespace lc
