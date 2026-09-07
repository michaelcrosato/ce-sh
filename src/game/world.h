// The game world for the two-room proof: player, door, lamp fixture, and threat, advanced by the
// fixed-step simulation and written into the Scene as interpolated render transforms. No D3D12
// types; the renderer only sees the Scene and a Camera.
#pragma once

#include "game/input.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/two_room_level.h"

#include <optional>
#include <string>
#include <vector>

namespace lc::game {

struct PlayerPose {
    math::Vec3 position;  // Feet.
    float yaw = 0.0f;
    float pitch = 0.0f;
};

class Player {
public:
    static constexpr float kWalkSpeed = 1.5f;
    static constexpr float kSprintSpeed = 2.6f;
    static constexpr float kEyeHeight = 1.6f;
    static constexpr float kMaxPitch = 1.4835f;  // 85 degrees.

    void Reset(const PlayerPose& pose);
    void Tick(const InputFrame& input, float dt);
    const PlayerPose& Current() const { return current_; }
    const PlayerPose& Previous() const { return previous_; }
    PlayerPose At(float alpha) const;
    math::Vec3 EyePosition(const PlayerPose& pose) const { return pose.position + math::Vec3{0.0f, kEyeHeight, 0.0f}; }
    math::Vec3 Forward(const PlayerPose& pose) const;  // Horizontal facing.
    Camera CameraAt(float alpha) const;

private:
    PlayerPose current_;
    PlayerPose previous_;
};

enum class DoorState { Closed, Opening, Open, Closing };
const char* DoorStateName(DoorState state);

class Door {
public:
    static constexpr float kOpenAngle = 1.5708f;  // 90 degrees.
    static constexpr float kDuration = 1.0f;      // Seconds for a full swing.

    explicit Door(const DoorHandle& handle) : handle_(handle) {}
    void Interact();
    void Tick(float dt);
    DoorState State() const { return state_; }
    bool IsMoving() const { return state_ == DoorState::Opening || state_ == DoorState::Closing; }
    float Angle() const { return angle_; }
    float AngleAt(float alpha) const { return previousAngle_ + (angle_ - previousAngle_) * alpha; }
    math::Mat4 TransformAt(float alpha) const { return DoorTransform(handle_, AngleAt(alpha)); }
    const DoorHandle& Handle() const { return handle_; }

private:
    DoorHandle handle_;
    DoorState state_ = DoorState::Closed;
    float angle_ = 0.0f;
    float previousAngle_ = 0.0f;
};

enum class LampState { Held, Placed };

class Lamp {
public:
    static constexpr float kReach = 2.0f;

    Lamp(const TwoRoomLevel& level, const SocketSpec& startSocket);
    LampState State() const { return state_; }
    bool IsOn() const { return on_; }
    const std::string& SocketName() const { return socketName_; }
    void PickUp();
    void Place(const SocketSpec& socket);
    void Toggle(Scene& scene);
    // Held lamps follow the player's eye pose; placed lamps keep the socket pose.
    void Tick(const Player& player);
    PoseSpec PoseAt(float alpha) const;
    const PoseSpec& Current() const { return current_; }

private:
    static PoseSpec HeldPose(const Player& player, const PlayerPose& pose);

    std::uint32_t material_ = 0;
    math::Vec3 housingHalf_;
    float faceOffset_ = 0.1f;
    LampState state_ = LampState::Placed;
    bool on_ = true;
    std::string socketName_;
    PoseSpec current_;
    PoseSpec previous_;
};

class Threat {
public:
    Threat(std::vector<math::Vec3> waypoints, float speed);
    void Tick(float dt);
    PoseSpec Current() const { return current_; }
    PoseSpec At(float alpha) const;
    // Places the threat at a distance along the path (tests and staging); resets history.
    void SetDistance(float distance);
    float Distance() const { return distance_; }
    float PathLength() const { return pathLength_; }

private:
    PoseSpec PoseAtDistance(float distance) const;

    std::vector<math::Vec3> waypoints_;
    std::vector<float> cumulative_;
    float pathLength_ = 0.0f;
    float speed_ = 1.2f;
    float distance_ = 0.0f;
    bool forward_ = true;
    PoseSpec current_;
    PoseSpec previous_;
};

struct InteractionTarget {
    std::string name;  // "door", "lamp", or a socket name.
    float distance = 0.0f;
    float alignment = 0.0f;  // Cosine between the view direction and the direction to the target.
};

class World {
public:
    explicit World(TwoRoomLevel level);

    // Back to the initial state (player start, door closed, lamp on its floor socket and on, threat
    // at the path start) and writes every transform; the caller treats it as a camera cut.
    void Reset();

    void Tick(const InputFrame& input, float dt);

    // Writes interpolated transforms into the scene for everything that moved and returns whether
    // any transform changed. Call CommitRenderedFrame on the scene after the frame is presented.
    bool WriteRenderScene(Scene& scene, float alpha);

    Camera CameraAt(float alpha) const { return player_.CameraAt(alpha); }
    std::optional<InteractionTarget> CurrentInteraction() const;

    const TwoRoomLevel& Level() const { return level_; }
    Player& GetPlayer() { return player_; }
    Door& GetDoor() { return door_; }
    Lamp& GetLamp() { return lamp_; }
    Threat& GetThreat() { return threat_; }
    Scene& GetScene() { return level_.description.scene; }
    const Scene& GetScene() const { return level_.description.scene; }
    std::uint64_t Ticks() const { return ticks_; }

    // Resolves an entity name used by replay checks to a stable instance id (0 when unknown).
    std::uint32_t StableIdOf(const std::string& entity) const;

private:
    TwoRoomLevel level_;
    Player player_;
    Door door_;
    Lamp lamp_;
    Threat threat_;
    std::uint64_t ticks_ = 0;
    // Last written render state, to avoid bumping transform revisions when nothing moved.
    bool wroteOnce_ = false;
    float lastDoorAngle_ = 0.0f;
    PoseSpec lastLampPose_;
    PoseSpec lastThreatPose_;
};

}  // namespace lc::game
