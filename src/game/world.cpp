#include "game/world.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <format>

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

void Player::Tick(const InputFrame& input, float dt, const CollisionWorld* colliders) {
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
    const Vec3 delta = move * (speed * dt);
    if (colliders != nullptr) {
        current_.position = colliders->MoveCapsule(current_.position, delta, kCapsule);
    } else {
        current_.position += delta;
    }
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

void Door::Block() {
    angle_ = previousAngle_;
    state_ = DoorState::Opening;
}

void Door::Restore(DoorState state, float angle) {
    state_ = state;
    angle_ = angle;
    previousAngle_ = angle;
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

void Lamp::Restore(const LampSnapshot& snapshot, Scene& scene) {
    state_ = snapshot.state;
    on_ = snapshot.on;
    socketName_ = snapshot.socket;
    current_ = snapshot.pose;
    previous_ = snapshot.pose;
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

void Lamp::Tick(const Player& player, const CollisionWorld* colliders) {
    previous_ = current_;
    if (state_ == LampState::Held) {
        current_ = HeldPose(player, player.Current());
        if (colliders != nullptr) {
            // Sweep the housing volume from the eye to the held pose; stop where it is free of solids.
            const Vec3 lift{0.0f, kHousingCentreHeight, 0.0f};
            const Vec3 eye = player.EyePosition(player.Current());
            const Vec3 housing = colliders->SweepSphere(eye, current_.position + lift, kHousingRadius);
            current_.position = housing - lift;
        }
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

const char* ThreatStateName(ThreatState state) {
    switch (state) {
        case ThreatState::Patrol: return "patrol";
        case ThreatState::Chase: return "chase";
        case ThreatState::Investigate: return "investigate";
        case ThreatState::Wait: return "wait";
        case ThreatState::Return: return "return";
    }
    return "unknown";
}

const char* ThreatBehaviourName(ThreatBehaviour behaviour) {
    return behaviour == ThreatBehaviour::Hunt ? "hunt" : "patrol";
}

void Threat::Tick(float dt) {
    previous_ = current_;
    seen_ = false;
    caught_ = false;
    Advance(dt);
}

bool Threat::Detect(const ThreatSenses& senses) const {
    Vec3 to = senses.playerFeet - current_.position;
    to.y = 0.0f;
    const float distance = math::Length(to);
    const float range = senses.lampHeld && senses.lampOn ? kDetectRangeLit : kDetectRangeDark;
    if (distance >= range || distance <= 1e-4f) return distance <= 1e-4f;
    const float facing = math::Dot(to / distance, Forward());
    return facing > kFacingCos && senses.lineOfSight;
}

float Threat::MoveToward(Vec3 target, float speed, float dt, const CollisionWorld* colliders) {
    Vec3 to = target - current_.position;
    to.y = 0.0f;
    const float distance = math::Length(to);
    if (distance <= 1e-5f) return 0.0f;
    const Vec3 dir = to / distance;
    const float step = std::min(distance, speed * dt);
    const Vec3 before = current_.position;
    if (colliders != nullptr) {
        current_.position = colliders->MoveCapsule(current_.position, dir * step, kCapsule);
    } else {
        current_.position += dir * step;
    }
    current_.yaw = std::atan2(-dir.x, -dir.z);
    const float progress = math::Length(current_.position - before);
    stuckSeconds_ = progress < 0.25f * step ? stuckSeconds_ + dt : 0.0f;
    return math::Length(target - current_.position);
}

float Threat::ClosestPathDistance() const {
    float best = 0.0f;
    float bestGap = 1e30f;
    for (std::size_t i = 0; i + 1 < waypoints_.size(); ++i) {
        const Vec3 a = waypoints_[i];
        const Vec3 b = waypoints_[i + 1];
        const Vec3 ab = b - a;
        const float len2 = math::Dot(ab, ab);
        const float t = len2 > 0.0f ? std::clamp(math::Dot(current_.position - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const Vec3 p = a + ab * t;
        const float gap = math::Length(p - current_.position);
        if (gap < bestGap) {
            bestGap = gap;
            best = cumulative_[i] + t * std::sqrt(len2);
        }
    }
    return best;
}

void Threat::Enter(ThreatState state) {
    state_ = state;
    stateSeconds_ = 0.0f;
    stuckSeconds_ = 0.0f;
    if (state == ThreatState::Return && !waypoints_.empty()) {
        const float d = ClosestPathDistance();
        returnTarget_ = PoseAtDistance(d).position;
        returnWaypoint_ = 0;
    }
}

void Threat::Tick(float dt, const ThreatSenses& senses, const CollisionWorld* colliders) {
    if (behaviour_ == ThreatBehaviour::Patrol) {
        Tick(dt);
        return;
    }
    previous_ = current_;
    caught_ = false;
    stateSeconds_ += dt;
    seen_ = Detect(senses);
    Vec3 toPlayer = senses.playerFeet - current_.position;
    toPlayer.y = 0.0f;
    const float playerDistance = math::Length(toPlayer);

    if (seen_ && state_ != ThreatState::Chase) {
        Enter(ThreatState::Chase);
    }
    switch (state_) {
        case ThreatState::Patrol:
            Advance(dt);
            break;
        case ThreatState::Chase:
            if (seen_) {
                lastSeen_ = senses.playerFeet;
                MoveToward(lastSeen_, kChaseSpeed, dt, colliders);
            } else {
                Enter(ThreatState::Investigate);
            }
            break;
        case ThreatState::Investigate: {
            const float left = MoveToward(lastSeen_, kInvestigateSpeed, dt, colliders);
            if (left < kArriveDistance || stateSeconds_ > kInvestigateTimeout || stuckSeconds_ > kStuckSeconds) Enter(ThreatState::Wait);
            break;
        }
        case ThreatState::Wait:
            if (stateSeconds_ >= kWaitSeconds) Enter(ThreatState::Return);
            break;
        case ThreatState::Return: {
            const float left = MoveToward(returnTarget_, kInvestigateSpeed, dt, colliders);
            if (left < kArriveDistance) {
                distance_ = ClosestPathDistance();
                current_ = PoseAtDistance(distance_);
                state_ = ThreatState::Patrol;
                stateSeconds_ = 0.0f;
            } else if (stuckSeconds_ > kStuckSeconds && !waypoints_.empty()) {
                // Blocked on the straight line: aim at the waypoints in turn instead.
                returnTarget_ = waypoints_[returnWaypoint_ % waypoints_.size()];
                ++returnWaypoint_;
                stuckSeconds_ = 0.0f;
            }
            break;
        }
    }
    // Contact in any state is a catch (the machine's body against the player's capsule).
    if (playerDistance < kCatchDistance) {
        caught_ = true;
    }
}

void Threat::Advance(float dt) {
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
    state_ = ThreatState::Patrol;
    stateSeconds_ = 0.0f;
}

// ---------------------------------------------------------------------------------------------
// Objective

const char* ObjectivePhaseName(ObjectivePhase phase) {
    switch (phase) {
        case ObjectivePhase::Introduction: return "introduction";
        case ObjectivePhase::LampAcquired: return "lamp_acquired";
        case ObjectivePhase::LampPlaced: return "lamp_placed";
        case ObjectivePhase::LampRetrieved: return "lamp_retrieved";
        case ObjectivePhase::Escaped: return "escaped";
    }
    return "unknown";
}

const char* ObjectiveText(ObjectivePhase phase) {
    switch (phase) {
        case ObjectivePhase::Introduction: return "Find the lamp and pick it up";
        case ObjectivePhase::LampAcquired: return "Carry the lamp to the shelf in the inspection room";
        case ObjectivePhase::LampPlaced: return "Check the mirror, then take the lamp back";
        case ObjectivePhase::LampRetrieved: return "Return to the exit in the equipment room with the lamp";
        case ObjectivePhase::Escaped: return "You made it out";
    }
    return "";
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

World::World(TwoRoomLevel level, ThreatBehaviour behaviour)
    : level_(std::move(level)), door_(level_.door), lamp_(level_, level_.floorSocket), threat_(level_.threatPath, level_.threatSpeed), behaviour_(behaviour) {
    colliders_.Build(GetScene(), level_.colliders);
    colliders_.SetTransform(GetScene(), level_.door.id, DoorTransform(level_.door, 0.0f));
    player_.Reset({level_.playerStart.position, level_.playerStart.yaw, level_.playerStart.pitch});
    threat_.SetBehaviour(behaviour_);
    lastLampPose_ = lamp_.Current();
    lastThreatPose_ = threat_.Current();
    SaveCheckpoint();
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
    threat_.SetBehaviour(behaviour_);
    colliders_.SetTransform(GetScene(), level_.door.id, DoorTransform(level_.door, 0.0f));
    phase_ = ObjectivePhase::Introduction;
    SaveCheckpoint();
    catches_ = 0;
    restarts_ = 0;
    justRestarted_ = false;
    justAdvanced_ = false;
    stateHash_ = 14695981039346656037ull;
    events_.clear();
    ticks_ = 0;
    doorBlocks_ = 0;
    wroteOnce_ = false;
    lastDoorAngle_ = 0.0f;
    lastLampPose_ = lamp_.Current();
    lastThreatPose_ = threat_.Current();
    WriteRenderScene(GetScene(), 0.0f);
}

void World::SaveCheckpoint() {
    checkpoint_.phase = phase_;
    checkpoint_.player = player_.Current();
    checkpoint_.door = door_.State();
    checkpoint_.doorAngle = door_.Angle();
    checkpoint_.lamp = lamp_.Snapshot();
}

void World::RestartFromCheckpoint() {
    phase_ = checkpoint_.phase;
    player_.Reset(checkpoint_.player);
    door_.Restore(checkpoint_.door, checkpoint_.doorAngle);
    colliders_.SetTransform(GetScene(), level_.door.id, DoorTransform(level_.door, door_.Angle()));
    lamp_.Restore(checkpoint_.lamp, GetScene());
    // The machine goes back to its round; nothing is ever staged into the player's view.
    threat_ = Threat(level_.threatPath, level_.threatSpeed);
    threat_.SetBehaviour(behaviour_);
    ++restarts_;
    justRestarted_ = true;
    Event(std::format("tick {}: restart {} from the checkpoint '{}' (player at {:.2f}, {:.2f}, {:.2f})", ticks_, restarts_, ObjectivePhaseName(phase_),
                      checkpoint_.player.position.x, checkpoint_.player.position.y, checkpoint_.player.position.z));
    WriteRenderScene(GetScene(), 1.0f);
}

ThreatSenses World::Sense() const {
    ThreatSenses s;
    const PlayerPose& pose = player_.Current();
    s.playerFeet = pose.position;
    s.lampHeld = lamp_.State() == LampState::Held;
    s.lampOn = lamp_.IsOn();
    const Vec3 head = threat_.Current().position + Vec3{0.0f, 1.35f, 0.0f};
    s.lineOfSight = colliders_.SegmentClear(head, player_.EyePosition(pose));
    return s;
}

void World::UpdateObjective() {
    const ObjectivePhase before = phase_;
    const bool held = lamp_.State() == LampState::Held;
    switch (phase_) {
        case ObjectivePhase::Introduction:
            if (held) phase_ = ObjectivePhase::LampAcquired;
            break;
        case ObjectivePhase::LampAcquired:
            if (!held && lamp_.SocketName() == level_.shelfSocket.name) phase_ = ObjectivePhase::LampPlaced;
            break;
        case ObjectivePhase::LampPlaced:
            if (held) phase_ = ObjectivePhase::LampRetrieved;
            break;
        case ObjectivePhase::LampRetrieved:
            if (held && level_.hasExit) {
                Vec3 to = player_.Current().position - level_.exitPosition;
                to.y = 0.0f;
                if (math::Length(to) <= level_.exitRadius) phase_ = ObjectivePhase::Escaped;
            }
            break;
        case ObjectivePhase::Escaped:
            break;
    }
    if (phase_ != before) {
        justAdvanced_ = true;
        SaveCheckpoint();
        Event(std::format("tick {}: objective {} -> {}", ticks_, ObjectivePhaseName(before), ObjectivePhaseName(phase_)));
    }
}

void World::HashTick() {
    auto mix = [&](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            stateHash_ ^= bytes[i];
            stateHash_ *= 1099511628211ull;
        }
    };
    auto mixFloat = [&](float f) { mix(&f, sizeof(f)); };
    const std::uint8_t phase = static_cast<std::uint8_t>(phase_);
    const std::uint8_t threatState = static_cast<std::uint8_t>(threat_.State());
    const std::uint8_t door = static_cast<std::uint8_t>(door_.State());
    const std::uint8_t lamp = static_cast<std::uint8_t>((lamp_.State() == LampState::Held ? 1 : 0) | (lamp_.IsOn() ? 2 : 0));
    mix(&ticks_, sizeof(ticks_));
    mix(&phase, 1);
    mix(&threatState, 1);
    mix(&door, 1);
    mix(&lamp, 1);
    mixFloat(door_.Angle());
    const PlayerPose& p = player_.Current();
    mixFloat(p.position.x);
    mixFloat(p.position.y);
    mixFloat(p.position.z);
    mixFloat(p.yaw);
    mixFloat(p.pitch);
    const PoseSpec t = threat_.Current();
    mixFloat(t.position.x);
    mixFloat(t.position.z);
    mixFloat(t.yaw);
    mix(&catches_, sizeof(catches_));
}

void World::Event(std::string text) {
    events_.push_back(std::move(text));  // The application logs them (Info) after the ticks.
}

std::vector<std::string> World::TakeEvents() {
    std::vector<std::string> out;
    out.swap(events_);
    return out;
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
    // The leaf is tall: aim at it at eye height (within the leaf) so standing close and looking
    // straight ahead still targets it.
    Vec3 doorPoint = level_.door.centre;
    doorPoint.y = std::clamp(eye.y, level_.door.centre.y - 0.8f, level_.door.centre.y + 0.8f);
    pick(consider("door", doorPoint, 2.0f));
    if (lamp_.State() == LampState::Placed) {
        pick(consider("lamp", lamp_.Current().position + Vec3{0.0f, 0.1f, 0.0f}, Lamp::kReach));
    } else {
        pick(consider(level_.shelfSocket.name, level_.shelfSocket.position, Lamp::kReach));
        pick(consider(level_.floorSocket.name, level_.floorSocket.position, Lamp::kReach));
    }
    return best;
}

void World::Tick(const InputFrame& input, float dt) {
    justRestarted_ = false;
    justAdvanced_ = false;
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
    player_.Tick(input, dt, &colliders_);
    door_.Tick(dt);
    // The leaf's collision follows its state; a leaf closing into the player swings back open.
    colliders_.SetTransform(GetScene(), level_.door.id, DoorTransform(level_.door, door_.Angle()));
    if (door_.State() == DoorState::Closing && colliders_.CapsuleOverlaps(player_.Current().position, Player::kCapsule)) {
        door_.Block();
        colliders_.SetTransform(GetScene(), level_.door.id, DoorTransform(level_.door, door_.Angle()));
        ++doorBlocks_;
        log::Debug("tick {}: door blocked by the player, reopening", ticks_);
    }
    const ThreatState threatBefore = threat_.State();
    threat_.Tick(dt, Sense(), &colliders_);
    if (threat_.State() != threatBefore) {
        Event(std::format("tick {}: threat {} -> {}", ticks_, ThreatStateName(threatBefore), ThreatStateName(threat_.State())));
    }
    lamp_.Tick(player_, &colliders_);
    UpdateObjective();
    if (threat_.CaughtPlayer() && phase_ != ObjectivePhase::Escaped) {
        ++catches_;
        Event(std::format("tick {}: caught ({}) at ({:.2f}, {:.2f}, {:.2f})", ticks_, catches_, player_.Current().position.x, player_.Current().position.y,
                          player_.Current().position.z));
        RestartFromCheckpoint();
        lamp_.Tick(player_, &colliders_);
    }
    ++ticks_;
    HashTick();
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
    if (level_.playerTorso.value != 0) {
        const PlayerPose pose = player_.At(alpha);
        const PoseSpec feet{pose.position, pose.yaw, 0.0f};
        if (!wroteOnce_ || !SamePose(feet, lastBodyPose_)) {
            scene.SetTransform(level_.playerTorso, PlayerTorsoTransform(feet));
            scene.SetTransform(level_.playerHandLeft, PlayerHandTransform(feet, false));
            scene.SetTransform(level_.playerHandRight, PlayerHandTransform(feet, true));
            lastBodyPose_ = feet;
            changed = true;
        }
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
    if (entity == "player_torso") return level_.playerTorso.value;
    for (const Instance& inst : GetScene().Instances()) {
        if (inst.name == entity) return inst.id.value;
    }
    return 0;
}

}  // namespace lc::game
