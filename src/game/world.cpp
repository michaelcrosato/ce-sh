#include "game/world.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>

namespace lc::game {

using math::Mat4;
using math::Vec3;

namespace {

float LerpAngle(float a, float b, float t) { return a + (b - a) * t; }

bool SamePose(const PoseSpec& a, const PoseSpec& b) {
    return a.position == b.position && a.yaw == b.yaw && a.pitch == b.pitch;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Player

void Player::Reset(const PlayerPose& pose) {
    current_ = pose;
    previous_ = pose;
}

Vec3 Player::Forward(const PlayerPose& pose) const {
    return {-std::sin(pose.yaw), 0.0f, -std::cos(pose.yaw)};
}

void Player::Tick(const InputFrame& input, float dt) {
    previous_ = current_;
    current_.yaw += input.lookDx;
    current_.pitch = std::clamp(current_.pitch + input.lookDy, -kMaxPitch, kMaxPitch);
    const Vec3 forward = Forward(current_);
    const Vec3 right{std::cos(current_.yaw), 0.0f, -std::sin(current_.yaw)};
    Vec3 move = right * input.moveX + forward * input.moveZ;
    const float length = math::Length(move);
    if (length > 1.0f) {
        move = move / length;
    }
    const float speed = input.sprint ? kSprintSpeed : kWalkSpeed;
    current_.position += move * (speed * dt);
}

PlayerPose Player::At(float alpha) const {
    PlayerPose p;
    p.position = math::Lerp(previous_.position, current_.position, alpha);
    p.yaw = LerpAngle(previous_.yaw, current_.yaw, alpha);
    p.pitch = LerpAngle(previous_.pitch, current_.pitch, alpha);
    return p;
}

Camera Player::CameraAt(float alpha) const {
    const PlayerPose pose = At(alpha);
    Camera cam;
    cam.position = EyePosition(pose);
    cam.yawRadians = pose.yaw;
    cam.pitchRadians = pose.pitch;
    return cam;
}

// ---------------------------------------------------------------------------------------------
// Door

const char* DoorStateName(DoorState state) {
    switch (state) {
        case DoorState::Closed: return "closed";
        case DoorState::Opening: return "opening";
        case DoorState::Open: return "open";
        case DoorState::Closing: return "closing";
    }
    return "unknown";
}

void Door::Interact() {
    switch (state_) {
        case DoorState::Closed:
        case DoorState::Closing:
            state_ = DoorState::Opening;
            break;
        case DoorState::Open:
        case DoorState::Opening:
            state_ = DoorState::Closing;
            break;
    }
}

void Door::Tick(float dt) {
    previousAngle_ = angle_;
    const float rate = kOpenAngle / kDuration;
    if (state_ == DoorState::Opening) {
        angle_ = std::min(kOpenAngle, angle_ + rate * dt);
        if (angle_ >= kOpenAngle) state_ = DoorState::Open;
    } else if (state_ == DoorState::Closing) {
        angle_ = std::max(0.0f, angle_ - rate * dt);
        if (angle_ <= 0.0f) state_ = DoorState::Closed;
    }
}

// ---------------------------------------------------------------------------------------------
// Lamp

Lamp::Lamp(const TwoRoomLevel& level, const SocketSpec& startSocket)
    : material_(level.lampMaterial), housingHalf_(level.lampHousingHalf), faceOffset_(level.lampFaceOffset), socketName_(startSocket.name) {
    current_ = {startSocket.position, startSocket.yaw, 0.0f};
    previous_ = current_;
}

void Lamp::PickUp() {
    state_ = LampState::Held;
    socketName_.clear();
}

void Lamp::Place(const SocketSpec& socket) {
    state_ = LampState::Placed;
    socketName_ = socket.name;
    current_ = {socket.position, socket.yaw, 0.0f};
    previous_ = current_;  // A placement is a discrete event; no interpolation from the hand.
}

void Lamp::Toggle(Scene& scene) {
    on_ = !on_;
    scene.SetEmitterOn(material_, on_);
}

PoseSpec Lamp::HeldPose(const Player& player, const PlayerPose& pose) {
    // Camera-space offset (right, down, forward) so the lamp sits in the lower right of the view.
    Camera cam;
    cam.position = player.EyePosition(pose);
    cam.yawRadians = pose.yaw;
    cam.pitchRadians = pose.pitch;
    const Vec3 offset{0.25f, -0.25f, -0.4f};  // The fixture origin is its base; the housing sits 5 cm above it.
    PoseSpec p;
    p.position = cam.ViewToWorld().TransformPoint(offset);
    p.yaw = pose.yaw;
    p.pitch = pose.pitch;
    return p;
}

void Lamp::Tick(const Player& player) {
    previous_ = current_;
    if (state_ == LampState::Held) {
        current_ = HeldPose(player, player.Current());
    }
}

PoseSpec Lamp::PoseAt(float alpha) const {
    if (state_ == LampState::Placed) {
        return current_;
    }
    PoseSpec p;
    p.position = math::Lerp(previous_.position, current_.position, alpha);
    p.yaw = LerpAngle(previous_.yaw, current_.yaw, alpha);
    p.pitch = LerpAngle(previous_.pitch, current_.pitch, alpha);
    return p;
}

// ---------------------------------------------------------------------------------------------
// Threat

Threat::Threat(std::vector<Vec3> waypoints, float speed) : waypoints_(std::move(waypoints)), speed_(speed) {
    cumulative_.assign(waypoints_.size(), 0.0f);
    for (std::size_t i = 1; i < waypoints_.size(); ++i) {
        cumulative_[i] = cumulative_[i - 1] + math::Length(waypoints_[i] - waypoints_[i - 1]);
    }
    pathLength_ = cumulative_.empty() ? 0.0f : cumulative_.back();
    current_ = PoseAtDistance(0.0f);
    previous_ = current_;
}

PoseSpec Threat::PoseAtDistance(float distance) const {
    PoseSpec p;
    if (waypoints_.empty()) {
        return p;
    }
    if (waypoints_.size() == 1 || pathLength_ <= 0.0f) {
        p.position = waypoints_[0];
        return p;
    }
    distance = std::clamp(distance, 0.0f, pathLength_);
    std::size_t segment = 0;
    while (segment + 2 < waypoints_.size() && distance > cumulative_[segment + 1]) {
        ++segment;
    }
    const Vec3 a = waypoints_[segment];
    const Vec3 b = waypoints_[segment + 1];
    const float segmentLength = cumulative_[segment + 1] - cumulative_[segment];
    const float t = segmentLength > 0.0f ? (distance - cumulative_[segment]) / segmentLength : 0.0f;
    p.position = math::Lerp(a, b, t);
    const Vec3 dir = math::Normalize(forward_ ? (b - a) : (a - b));
    p.yaw = std::atan2(-dir.x, -dir.z);  // Facing the travel direction (forward is -Z at yaw 0).
    return p;
}

void Threat::Tick(float dt) {
    previous_ = current_;
    if (pathLength_ <= 0.0f) {
        return;
    }
    float remaining = speed_ * dt;
    while (remaining > 0.0f) {
        if (forward_) {
            const float room = pathLength_ - distance_;
            if (remaining <= room) {
                distance_ += remaining;
                remaining = 0.0f;
            } else {
                distance_ = pathLength_;
                remaining -= room;
                forward_ = false;
            }
        } else {
            if (remaining <= distance_) {
                distance_ -= remaining;
                remaining = 0.0f;
            } else {
                remaining -= distance_;
                distance_ = 0.0f;
                forward_ = true;
            }
        }
    }
    current_ = PoseAtDistance(distance_);
}

void Threat::SetDistance(float distance) {
    distance_ = std::clamp(distance, 0.0f, pathLength_);
    current_ = PoseAtDistance(distance_);
    previous_ = current_;
}

PoseSpec Threat::At(float alpha) const {
    PoseSpec p;
    p.position = math::Lerp(previous_.position, current_.position, alpha);
    p.yaw = LerpAngle(previous_.yaw, current_.yaw, alpha);
    p.pitch = 0.0f;
    return p;
}

// ---------------------------------------------------------------------------------------------
// World

World::World(TwoRoomLevel level)
    : level_(std::move(level)), door_(level_.door), lamp_(level_, level_.floorSocket), threat_(level_.threatPath, level_.threatSpeed) {
    player_.Reset({level_.playerStart.position, level_.playerStart.yaw, level_.playerStart.pitch});
    lastLampPose_ = lamp_.Current();
    lastThreatPose_ = threat_.Current();
    // The static level parks the threat at the check position; the simulation starts on the path.
    WriteRenderScene(GetScene(), 0.0f);
    GetScene().CommitRenderedFrame();
}

void World::Reset() {
    player_.Reset({level_.playerStart.position, level_.playerStart.yaw, level_.playerStart.pitch});
    door_ = Door(level_.door);
    lamp_ = Lamp(level_, level_.floorSocket);
    GetScene().SetEmitterOn(level_.lampMaterial, true);  // The fixture starts switched on.
    threat_ = Threat(level_.threatPath, level_.threatSpeed);
    ticks_ = 0;
    wroteOnce_ = false;
    lastDoorAngle_ = 0.0f;
    lastLampPose_ = lamp_.Current();
    lastThreatPose_ = threat_.Current();
    WriteRenderScene(GetScene(), 0.0f);
}

std::optional<InteractionTarget> World::CurrentInteraction() const {
    const PlayerPose& pose = player_.Current();
    const Vec3 eye = player_.EyePosition(pose);
    // Full 3D view direction: the target closest to the centre of the view wins, so looking down at
    // the lamp picks the lamp even when the door is also within reach.
    Camera cam;
    cam.position = eye;
    cam.yawRadians = pose.yaw;
    cam.pitchRadians = pose.pitch;
    const Vec3 view = cam.Forward();
    auto consider = [&](const std::string& name, const Vec3& point, float reach) -> std::optional<InteractionTarget> {
        const Vec3 to = point - eye;
        const float distance = math::Length(to);
        if (distance > reach || distance <= 0.0f) return std::nullopt;
        const float alignment = math::Dot(to / distance, view);
        if (alignment < 0.6f) return std::nullopt;  // Must look at the target (within ~53 degrees).
        InteractionTarget t{name, distance};
        t.alignment = alignment;
        return t;
    };
    std::optional<InteractionTarget> best;
    auto pick = [&](std::optional<InteractionTarget> candidate) {
        if (candidate && (!best || candidate->alignment > best->alignment)) best = candidate;
    };
    pick(consider("door", level_.door.centre, 2.0f));
    if (lamp_.State() == LampState::Placed) {
        pick(consider("lamp", lamp_.Current().position + Vec3{0.0f, 0.1f, 0.0f}, Lamp::kReach));
    } else {
        pick(consider(level_.shelfSocket.name, level_.shelfSocket.position, Lamp::kReach));
        pick(consider(level_.floorSocket.name, level_.floorSocket.position, Lamp::kReach));
    }
    return best;
}

void World::Tick(const InputFrame& input, float dt) {
    if (input.interactPressed) {
        if (const auto target = CurrentInteraction()) {
            if (target->name == "door") {
                door_.Interact();
                log::Debug("tick {}: door -> {}", ticks_, DoorStateName(door_.State()));
            } else if (target->name == "lamp") {
                lamp_.PickUp();
                log::Debug("tick {}: lamp picked up", ticks_);
            } else if (target->name == level_.shelfSocket.name) {
                lamp_.Place(level_.shelfSocket);
                log::Debug("tick {}: lamp placed on {}", ticks_, target->name);
            } else if (target->name == level_.floorSocket.name) {
                lamp_.Place(level_.floorSocket);
                log::Debug("tick {}: lamp placed on {}", ticks_, target->name);
            }
        }
    }
    if (input.lampPressed) {
        lamp_.Toggle(GetScene());
        log::Debug("tick {}: lamp {}", ticks_, lamp_.IsOn() ? "on" : "off");
    }
    player_.Tick(input, dt);
    door_.Tick(dt);
    threat_.Tick(dt);
    lamp_.Tick(player_);
    ++ticks_;
}

bool World::WriteRenderScene(Scene& scene, float alpha) {
    bool changed = false;
    const float doorAngle = door_.AngleAt(alpha);
    if (!wroteOnce_ || doorAngle != lastDoorAngle_) {
        scene.SetTransform(level_.door.id, DoorTransform(level_.door, doorAngle));
        lastDoorAngle_ = doorAngle;
        changed = true;
    }
    const PoseSpec lampPose = lamp_.PoseAt(alpha);
    if (!wroteOnce_ || !SamePose(lampPose, lastLampPose_)) {
        scene.SetTransform(level_.lampHousing, LampHousingTransform(lampPose));
        scene.SetTransform(level_.lampFace, LampFaceTransform(lampPose, level_.lampFaceOffset));
        lastLampPose_ = lampPose;
        changed = true;
    }
    const PoseSpec threatPose = threat_.At(alpha);
    if (!wroteOnce_ || !SamePose(threatPose, lastThreatPose_)) {
        scene.SetTransform(level_.threatBody, ThreatBodyTransform(threatPose));
        scene.SetTransform(level_.threatHead, ThreatHeadTransform(threatPose));
        lastThreatPose_ = threatPose;
        changed = true;
    }
    wroteOnce_ = true;
    return changed;
}

std::uint32_t World::StableIdOf(const std::string& entity) const {
    if (entity == "threat_body") return level_.threatBody.value;
    if (entity == "threat_head") return level_.threatHead.value;
    if (entity == "door") return level_.door.id.value;
    if (entity == "mirror") return level_.mirror.value;
    if (entity == "lamp_housing") return level_.lampHousing.value;
    if (entity == "lamp_face") return level_.lampFace.value;
    if (entity == "floor") return level_.hallFloorId.value;
    for (const Instance& inst : GetScene().Instances()) {
        if (inst.name == entity) return inst.id.value;
    }
    return 0;
}

}  // namespace lc::game
