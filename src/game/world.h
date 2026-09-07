// The game world: player, doors, carried items (the lamp with its light, the fuse), circuits that
// follow item placement, fans, the machine, and the objective's steps, advanced by the fixed-step
// simulation and written into the Scene as interpolated render transforms. No D3D12 types; the
// renderer only sees the Scene and a Camera. Every rule reads gameplay data, never an image.
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
    Door(const LevelDoor& level) : handle_(level.handle), id_(level.id), locked_(level.locked), circuit_(level.opensWithCircuit) {}
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
    const std::string& Id() const { return id_; }
    bool Locked() const { return locked_; }                 // Ignores the interaction key.
    const std::string& OpensWithCircuit() const { return circuit_; }

private:
    DoorHandle handle_;
    std::string id_ = "door";
    bool locked_ = false;
    std::string circuit_;
    DoorState state_ = DoorState::Closed;
    float angle_ = 0.0f;
    float previousAngle_ = 0.0f;
};

enum class ItemState { Held, Placed };
using LampState = ItemState;

// An item's whole state for checkpoints.
struct ItemSnapshot {
    ItemState state = ItemState::Placed;
    bool on = true;
    std::string socket;
    PoseSpec pose;
};
using LampSnapshot = ItemSnapshot;

// A carried object: the lamp (a lit item with its own supply) or the fuse (a plain box). Held items
// follow the player's eye pose, swept back toward the player when they would enter a solid (spec
// §14); placed items keep their socket pose; a pocketed item hides inside the player's torso.
class Item {
public:
    static constexpr float kReach = 2.0f;
    static constexpr float kHousingRadius = 0.125f;  // Bounding sphere of the lamp housing box.
    static constexpr float kHousingCentreHeight = 0.05f;

    Item(const LevelItem& level, const SocketSpec& startSocket);
    const std::string& Id() const { return id_; }
    const std::string& Text() const { return text_; }
    ItemState State() const { return state_; }
    bool HasLight() const { return hasLight_; }
    bool IsOn() const { return on_; }
    bool HidesWhenCarried() const { return hidesWhenCarried_; }
    const std::string& SocketName() const { return socketName_; }
    void PickUp();
    void Place(const SocketSpec& socket);
    void Toggle(Scene& scene);
    ItemSnapshot Snapshot() const { return {state_, on_, socketName_, current_}; }
    void Restore(const ItemSnapshot& snapshot, Scene& scene);
    void Tick(const Player& player, const CollisionWorld* colliders = nullptr);
    PoseSpec PoseAt(float alpha) const;
    const PoseSpec& Current() const { return current_; }
    std::uint32_t LightMaterial() const { return lightMaterial_; }
    math::Vec3 Half() const { return half_; }
    float FaceOffset() const { return faceOffset_; }

private:
    static PoseSpec HeldPose(const Player& player, const PlayerPose& pose);

    std::string id_;
    std::string text_;
    bool hasLight_ = false;
    std::uint32_t lightMaterial_ = 0;
    math::Vec3 half_;
    float faceOffset_ = 0.1f;
    bool hidesWhenCarried_ = false;
    ItemState state_ = ItemState::Placed;
    bool on_ = true;
    std::string socketName_;
    PoseSpec current_;
    PoseSpec previous_;
};
using Lamp = Item;

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
    // Explicit staging (tests): an off-path position, and the start of a return to the path.
    void Teleport(math::Vec3 position);
    void BeginReturn();
    // The route an off-path move follows (spec §15: hall waypoints, no navigation mesh): the straight
    // line when the collision solids leave it clear at body height, otherwise the patrol polyline
    // between the points closest to `from` and `to`, then `to`.
    std::vector<math::Vec3> PlanRoute(math::Vec3 from, math::Vec3 to, const CollisionWorld* colliders) const;
    const std::vector<math::Vec3>& CurrentRoute() const { return route_; }

private:
    PoseSpec PoseAtDistance(float distance) const;
    void Advance(float dt);
    bool Detect(const ThreatSenses& senses) const;
    // Straight-line step toward a target, sliding along solids; returns the distance still to go.
    float MoveToward(math::Vec3 target, float speed, float dt, const CollisionWorld* colliders);
    // Follows route_ leg by leg; returns the distance to its final point.
    float FollowRoute(float speed, float dt, const CollisionWorld* colliders);
    float ClosestPathDistance() const;
    std::size_t ClosestSegment(math::Vec3 p, float& t, math::Vec3& closest) const;
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
    std::vector<math::Vec3> route_;
    std::size_t routeIndex_ = 0;
    bool seen_ = false;
    bool caught_ = false;
};

// A fan turns while its circuit is on and spins down when it goes off (spec §5, §15: the sound and
// the moving shadows follow the actual state).
class Fan {
public:
    static constexpr float kSpinSeconds = 3.0f;  // Full stop to full speed and back.

    explicit Fan(const LevelFan& level) : level_(level) {}
    void SetPowered(bool on) { powered_ = on; }
    void Tick(float dt);
    float Angle() const { return angle_; }
    float AngleAt(float alpha) const { return previousAngle_ + (angle_ - previousAngle_) * alpha; }
    float SpeedFraction() const { return speed_ / TargetSpeed(); }  // 0 stopped .. 1 full.
    bool Turning() const { return speed_ > 1e-3f; }
    const LevelFan& Level() const { return level_; }
    void Restore(float angle, float speed) { angle_ = previousAngle_ = angle; speed_ = speed; }
    float Speed() const { return speed_; }

private:
    float TargetSpeed() const { return level_.rpm * 2.0f * math::kPi / 60.0f; }

    LevelFan level_;
    bool powered_ = false;
    float angle_ = 0.0f;
    float previousAngle_ = 0.0f;
    float speed_ = 0.0f;  // Radians per second.
};

struct InteractionTarget {
    enum class Kind { Door, Item, Socket };
    Kind kind = Kind::Door;
    std::string name;        // The door, item, or socket id (also the legacy name replay checks use).
    std::string text;        // Player-facing prompt ("Open the door", "Take the lamp").
    float distance = 0.0f;
    float alignment = 0.0f;  // Cosine between the view direction and the direction to the target.
};

struct Checkpoint {
    std::size_t completedSteps = 0;
    PlayerPose player;
    std::vector<std::pair<DoorState, float>> doors;  // State and angle per door.
    std::vector<ItemSnapshot> items;
    std::vector<std::pair<float, float>> fans;       // Angle and speed per fan.
};

class World {
public:
    explicit World(TwoRoomLevel level, ThreatBehaviour behaviour = ThreatBehaviour::Patrol);

    // Back to the initial state (player start, doors closed, items in their start sockets and on,
    // threat at the path start, objective at the introduction) and writes every transform; the
    // caller treats it as a camera cut.
    void Reset();
    // Spec §15 restart command: the last checkpoint (the objective's progress and the world state
    // saved when the current step began); the threat goes back to its path. A catch calls this.
    void RestartFromCheckpoint();

    void Tick(const InputFrame& input, float dt);

    // Objective: the phase is "introduction" before the first step, then the id of the last completed step.
    const std::string& Phase() const { return phase_; }
    std::size_t CompletedSteps() const { return completedSteps_; }
    bool Complete() const { return completedSteps_ >= level_.steps.size(); }
    const std::string& ObjectiveLine() const;  // The pending step's text, or the completion text.
    const Checkpoint& LastCheckpoint() const { return checkpoint_; }
    std::uint32_t Catches() const { return catches_; }
    std::uint32_t Restarts() const { return restarts_; }
    bool JustRestarted() const { return justRestarted_; }   // Set by the tick that restarted (a camera cut).
    bool JustAdvanced() const { return justAdvanced_; }     // The objective completed a step this tick.
    // FNV-1a over every tick's game state (phase, threat state and pose, player pose, items, doors,
    // fans, circuits): identical for identical inputs whatever the renderer does (T14).
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
    // The proof's singular accessors: the first door and the first lit item.
    Door& GetDoor() { return doors_.front(); }
    const Door& GetDoor() const { return doors_.front(); }
    Item& GetLamp() { return items_[lampIndex_]; }
    const Item& GetLamp() const { return items_[lampIndex_]; }
    std::vector<Door>& Doors() { return doors_; }
    const std::vector<Door>& Doors() const { return doors_; }
    std::vector<Item>& Items() { return items_; }
    const std::vector<Item>& Items() const { return items_; }
    const std::vector<Fan>& Fans() const { return fans_; }
    bool CircuitOn(const std::string& id) const;
    const Item* FindItem(const std::string& id) const;
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
    void EvaluateCircuits(bool initial);
    void UpdateObjective();
    void SaveCheckpoint();
    void HashTick();
    void Event(std::string text);
    const SocketSpec* FindSocket(const std::string& name) const;
    Item* HeldItem();
    const Item* HeldItem() const;
    math::Mat4 ItemTransform(const Item& item, const PoseSpec& pose, const PlayerPose& player) const;

    TwoRoomLevel level_;
    CollisionWorld colliders_;
    Player player_;
    std::vector<Door> doors_;
    std::vector<Item> items_;
    std::size_t lampIndex_ = 0;
    std::vector<Fan> fans_;
    std::vector<CircuitState> circuits_;
    Threat threat_;
    ThreatBehaviour behaviour_ = ThreatBehaviour::Patrol;
    std::size_t completedSteps_ = 0;
    std::string phase_ = "introduction";
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
    std::vector<float> lastDoorAngles_;
    std::vector<PoseSpec> lastItemPoses_;
    std::vector<bool> lastItemHidden_;
    std::vector<float> lastFanAngles_;
    PoseSpec lastThreatPose_;
    PoseSpec lastBodyPose_;
};

}  // namespace lc::game
