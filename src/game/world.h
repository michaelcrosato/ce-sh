// The game world for the two-room proof: player, door, lamp fixture, and threat, advanced by the
// fixed-step simulation and written into the Scene as interpolated render transforms. No D3D12
// types; the renderer only sees the Scene and a Camera.
#pragma once

#include "game/collision.h"
#include "game/input.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/two_room_level.h"

#include <cmath>
#include <cstdint>
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
    static constexpr Capsule kCapsule{0.3f, 1.7f, 0.05f};  // Spec §14: capsule-like controller.

    void Reset(const PlayerPose& pose);
    // Moves against the collision solids when given (walls and furniture; slides along them).
    void Tick(const InputFrame& input, float dt, const CollisionWorld* colliders = nullptr);
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
    // A closing leaf that would enter the player's volume swings back open (spec §14: never trap the player).
    void Block();
    // Checkpoint restore: an explicit state and angle, no interpolation from the previous pose.
    void Restore(DoorState state, float angle);
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

// The lamp's whole state for checkpoints.
struct LampSnapshot {
    LampState state = LampState::Placed;
    bool on = true;
    std::string socket;
    PoseSpec pose;
};

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
    LampSnapshot Snapshot() const { return {state_, on_, socketName_, current_}; }
    void Restore(const LampSnapshot& snapshot, Scene& scene);
    // Held lamps follow the player's eye pose, swept back toward the player when the housing
    // would enter a solid (spec §14); placed lamps keep the socket pose.
    void Tick(const Player& player, const CollisionWorld* colliders = nullptr);
    static constexpr float kHousingRadius = 0.125f;  // Bounding sphere of the housing box.
    static constexpr float kHousingCentreHeight = 0.05f;
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

// Spec §15: the deterministic path for the visual tests ("patrol"), or the small state machine for
// the playable proof ("hunt"): patrol -> chase while the player is detected -> investigate the last
// seen position -> wait -> return to the path. Detection uses gameplay data only: the lamp state,
// distance, facing, and a line of sight through the collision solids (never a rendered image).
enum class ThreatBehaviour { Patrol, Hunt };
enum class ThreatState { Patrol, Chase, Investigate, Wait, Return };
const char* ThreatStateName(ThreatState state);
const char* ThreatBehaviourName(ThreatBehaviour behaviour);

struct ThreatSenses {
    math::Vec3 playerFeet;
    bool lampHeld = false;
    bool lampOn = false;
    bool lineOfSight = false;  // Threat head to player eye clear of solids (the world computes it).
};

class Threat {
public:
    static constexpr float kChaseSpeed = 1.8f;         // Faster than walking (1.5), slower than sprinting (2.6).
    static constexpr float kInvestigateSpeed = 1.4f;
    static constexpr float kDetectRangeLit = 8.0f;     // The carried, switched-on lamp gives the player away.
    static constexpr float kDetectRangeDark = 2.5f;
    static constexpr float kFacingCos = 0.5f;          // Within 60 degrees of the machine's facing.
    static constexpr float kCatchDistance = 0.6f;      // Horizontal centre distance: bodies in contact.
    static constexpr float kArriveDistance = 0.35f;
    static constexpr float kWaitSeconds = 2.0f;
    static constexpr float kInvestigateTimeout = 4.0f;
    static constexpr float kStuckSeconds = 1.5f;       // No progress for this long ends a move.
    static constexpr Capsule kCapsule{0.3f, 1.7f, 0.05f};

    Threat(std::vector<math::Vec3> waypoints, float speed);
    void SetBehaviour(ThreatBehaviour behaviour) { behaviour_ = behaviour; }
    ThreatBehaviour Behaviour() const { return behaviour_; }
    // Patrol only (the deterministic path).
    void Tick(float dt);
    // The full state machine; falls back to the path when the behaviour is Patrol.
    void Tick(float dt, const ThreatSenses& senses, const CollisionWorld* colliders);
    PoseSpec Current() const { return current_; }
    PoseSpec At(float alpha) const;
    math::Vec3 Forward() const { return {-std::sin(current_.yaw), 0.0f, -std::cos(current_.yaw)}; }
    ThreatState State() const { return state_; }
    bool SeesPlayer() const { return seen_; }          // This tick.
    bool CaughtPlayer() const { return caught_; }      // This tick (the world restarts from the checkpoint).
    math::Vec3 LastSeen() const { return lastSeen_; }
    // Places the threat at a distance along the path (tests and staging); resets history.
    void SetDistance(float distance);
    float Distance() const { return distance_; }
    float PathLength() const { return pathLength_; }

private:
    PoseSpec PoseAtDistance(float distance) const;
    void Advance(float dt);
    bool Detect(const ThreatSenses& senses) const;
    // Straight-line step toward a target, sliding along solids; returns the distance still to go.
    float MoveToward(math::Vec3 target, float speed, float dt, const CollisionWorld* colliders);
    float ClosestPathDistance() const;
    void Enter(ThreatState state);

    std::vector<math::Vec3> waypoints_;
    std::vector<float> cumulative_;
    float pathLength_ = 0.0f;
    float speed_ = 1.2f;
    float distance_ = 0.0f;
    bool forward_ = true;
    PoseSpec current_;
    PoseSpec previous_;
    ThreatBehaviour behaviour_ = ThreatBehaviour::Patrol;
    ThreatState state_ = ThreatState::Patrol;
    float stateSeconds_ = 0.0f;
    float stuckSeconds_ = 0.0f;
    math::Vec3 lastSeen_;
    math::Vec3 returnTarget_;
    std::size_t returnWaypoint_ = 0;
    bool seen_ = false;
    bool caught_ = false;
};

// Spec §15: the proof's objective, a checkpoint per phase, a catch restarts from the checkpoint.
enum class ObjectivePhase { Introduction, LampAcquired, LampPlaced, LampRetrieved, Escaped };
const char* ObjectivePhaseName(ObjectivePhase phase);
const char* ObjectiveText(ObjectivePhase phase);

struct Checkpoint {
    ObjectivePhase phase = ObjectivePhase::Introduction;
    PlayerPose player;
    DoorState door = DoorState::Closed;
    float doorAngle = 0.0f;
    LampSnapshot lamp;
};

struct InteractionTarget {
    std::string name;  // "door", "lamp", or a socket name.
    float distance = 0.0f;
    float alignment = 0.0f;  // Cosine between the view direction and the direction to the target.
};

class World {
public:
    explicit World(TwoRoomLevel level, ThreatBehaviour behaviour = ThreatBehaviour::Patrol);

    // Back to the initial state (player start, door closed, lamp on its floor socket and on, threat
    // at the path start, objective at the introduction) and writes every transform; the caller
    // treats it as a camera cut.
    void Reset();
    // Spec §15 restart command: the last checkpoint (objective phase and the world state saved when
    // it was reached); the threat goes back to its path. A catch calls this within its tick.
    void RestartFromCheckpoint();

    void Tick(const InputFrame& input, float dt);

    ObjectivePhase Phase() const { return phase_; }
    const Checkpoint& LastCheckpoint() const { return checkpoint_; }
    std::uint32_t Catches() const { return catches_; }
    std::uint32_t Restarts() const { return restarts_; }
    bool JustRestarted() const { return justRestarted_; }   // Set by the tick that restarted (a camera cut).
    bool JustAdvanced() const { return justAdvanced_; }     // The objective moved to a new phase this tick.
    // FNV-1a over every tick's game state (phase, threat state and pose, player pose, lamp, door):
    // identical for identical inputs whatever the renderer does (T14).
    std::uint64_t StateHash() const { return stateHash_; }
    // Human-readable state changes since the last call ("tick 120: threat patrol -> chase").
    std::vector<std::string> TakeEvents();

    // Writes interpolated transforms into the scene for everything that moved and returns whether
    // any transform changed. Call CommitRenderedFrame on the scene after the frame is presented.
    bool WriteRenderScene(Scene& scene, float alpha);

    Camera CameraAt(float alpha) const { return player_.CameraAt(alpha); }
    std::optional<InteractionTarget> CurrentInteraction() const;

    const TwoRoomLevel& Level() const { return level_; }
    Player& GetPlayer() { return player_; }
    const Player& GetPlayer() const { return player_; }
    Door& GetDoor() { return door_; }
    const Door& GetDoor() const { return door_; }
    Lamp& GetLamp() { return lamp_; }
    const Lamp& GetLamp() const { return lamp_; }
    Threat& GetThreat() { return threat_; }
    const Threat& GetThreat() const { return threat_; }
    ThreatBehaviour Behaviour() const { return behaviour_; }
    Scene& GetScene() { return level_.description.scene; }
    const Scene& GetScene() const { return level_.description.scene; }
    const CollisionWorld& Colliders() const { return colliders_; }
    std::uint64_t Ticks() const { return ticks_; }
    std::uint32_t DoorBlocks() const { return doorBlocks_; }

    // Resolves an entity name used by replay checks to a stable instance id (0 when unknown).
    std::uint32_t StableIdOf(const std::string& entity) const;

private:
    ThreatSenses Sense() const;
    void UpdateObjective();
    void SaveCheckpoint();
    void HashTick();
    void Event(std::string text);

    TwoRoomLevel level_;
    CollisionWorld colliders_;
    Player player_;
    Door door_;
    Lamp lamp_;
    Threat threat_;
    ThreatBehaviour behaviour_ = ThreatBehaviour::Patrol;
    ObjectivePhase phase_ = ObjectivePhase::Introduction;
    Checkpoint checkpoint_;
    std::uint32_t catches_ = 0;
    std::uint32_t restarts_ = 0;
    bool justRestarted_ = false;
    bool justAdvanced_ = false;
    std::uint64_t stateHash_ = 14695981039346656037ull;
    std::vector<std::string> events_;
    std::uint64_t ticks_ = 0;
    std::uint32_t doorBlocks_ = 0;
    // Last written render state, to avoid bumping transform revisions when nothing moved.
    bool wroteOnce_ = false;
    float lastDoorAngle_ = 0.0f;
    PoseSpec lastLampPose_;
    PoseSpec lastThreatPose_;
    PoseSpec lastBodyPose_;
};

}  // namespace lc::game
