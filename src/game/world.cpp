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
// Item

Item::Item(const LevelItem& level, const SocketSpec& startSocket)
    : id_(level.id), text_(level.text), hasLight_(level.hasLight), lightMaterial_(level.lightMaterial), half_(level.half), faceOffset_(level.faceOffset),
      hidesWhenCarried_(level.hidesWhenCarried), socketName_(startSocket.name) {
    current_ = {startSocket.position, startSocket.yaw, 0.0f};
    previous_ = current_;
}

void Item::PickUp() {
    state_ = ItemState::Held;
    socketName_.clear();
}

void Item::Place(const SocketSpec& socket) {
    state_ = ItemState::Placed;
    socketName_ = socket.name;
    current_ = {socket.position, socket.yaw, 0.0f};
    previous_ = current_;  // A placement is a discrete event; no interpolation from the hand.
}

void Item::Toggle(Scene& scene) {
    if (!hasLight_) return;
    on_ = !on_;
    scene.SetEmitterOn(lightMaterial_, on_);
}

void Item::Restore(const ItemSnapshot& snapshot, Scene& scene) {
    state_ = snapshot.state;
    on_ = snapshot.on;
    socketName_ = snapshot.socket;
    current_ = snapshot.pose;
    previous_ = snapshot.pose;
    if (hasLight_) scene.SetEmitterOn(lightMaterial_, on_);
}

PoseSpec Item::HeldPose(const Player& player, const PlayerPose& pose) {
    // Camera-space offset (right, down, forward) so the item sits in the lower right of the view.
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

void Item::Tick(const Player& player, const CollisionWorld* colliders) {
    previous_ = current_;
    if (state_ == ItemState::Held) {
        current_ = HeldPose(player, player.Current());
        if (colliders != nullptr && !hidesWhenCarried_) {
            // Sweep the housing volume from the eye to the held pose; stop where it is free of solids.
            const Vec3 lift{0.0f, kHousingCentreHeight, 0.0f};
            const Vec3 eye = player.EyePosition(player.Current());
            const Vec3 housing = colliders->SweepSphere(eye, current_.position + lift, kHousingRadius);
            current_.position = housing - lift;
        }
    }
}

PoseSpec Item::PoseAt(float alpha) const {
    if (state_ == ItemState::Placed) {
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

std::size_t Threat::ClosestSegment(Vec3 p, float& tOut, Vec3& closest) const {
    std::size_t best = 0;
    float bestGap = 1e30f;
    tOut = 0.0f;
    closest = waypoints_.empty() ? p : waypoints_[0];
    for (std::size_t i = 0; i + 1 < waypoints_.size(); ++i) {
        const Vec3 a = waypoints_[i];
        const Vec3 b = waypoints_[i + 1];
        const Vec3 ab = b - a;
        const float len2 = math::Dot(ab, ab);
        const float t = len2 > 0.0f ? std::clamp(math::Dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        const Vec3 q = a + ab * t;
        const float gap = math::Length(q - p);
        if (gap < bestGap) {
            bestGap = gap;
            best = i;
            tOut = t;
            closest = q;
        }
    }
    return best;
}

float Threat::ClosestPathDistance() const {
    if (waypoints_.size() < 2) return 0.0f;
    float t = 0.0f;
    Vec3 closest;
    const std::size_t i = ClosestSegment(current_.position, t, closest);
    return cumulative_[i] + t * math::Length(waypoints_[i + 1] - waypoints_[i]);
}

std::vector<Vec3> Threat::PlanRoute(Vec3 from, Vec3 to, const CollisionWorld* colliders) const {
    const Vec3 up{0.0f, 0.6f, 0.0f};  // Body height: door openings and the halls are clear there, walls are not.
    if (colliders == nullptr || waypoints_.size() < 2 || colliders->SegmentClear(from + up, to + up)) {
        return {to};
    }
    float ta = 0.0f, tb = 0.0f;
    Vec3 pa, pb;
    const std::size_t ia = ClosestSegment(from, ta, pa);
    const std::size_t ib = ClosestSegment(to, tb, pb);
    std::vector<Vec3> route;
    auto add = [&](Vec3 p) {
        if (route.empty() || math::Length(route.back() - p) > 0.2f) route.push_back(p);
    };
    add(pa);
    if (ia < ib) {
        for (std::size_t i = ia + 1; i <= ib; ++i) add(waypoints_[i]);
    } else if (ia > ib) {
        for (std::size_t i = ia; i > ib; --i) add(waypoints_[i]);
    }
    add(pb);
    add(to);
    return route;
}

float Threat::FollowRoute(float speed, float dt, const CollisionWorld* colliders) {
    if (route_.empty()) return 0.0f;
    if (routeIndex_ >= route_.size()) routeIndex_ = route_.size() - 1;
    const float left = MoveToward(route_[routeIndex_], speed, dt, colliders);
    const bool lastLeg = routeIndex_ + 1 == route_.size();
    if (!lastLeg && (left < kArriveDistance || stuckSeconds_ > kStuckSeconds)) {
        ++routeIndex_;  // The next leg (a blocked leg is skipped rather than pushed against forever).
        stuckSeconds_ = 0.0f;
    }
    Vec3 toEnd = route_.back() - current_.position;
    toEnd.y = 0.0f;
    return math::Length(toEnd);
}

void Threat::Teleport(Vec3 position) {
    current_.position = position;
    previous_ = current_;
}

void Threat::BeginReturn() { Enter(ThreatState::Return); }

void Threat::Enter(ThreatState state) {
    state_ = state;
    stateSeconds_ = 0.0f;
    stuckSeconds_ = 0.0f;
    route_.clear();
    routeIndex_ = 0;
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
            if (route_.empty()) route_ = PlanRoute(current_.position, lastSeen_, colliders);
            const float left = FollowRoute(kInvestigateSpeed, dt, colliders);
            const bool onLastLeg = routeIndex_ + 1 >= route_.size();
            if ((onLastLeg && left < kArriveDistance) || stateSeconds_ > kInvestigateTimeout || (onLastLeg && stuckSeconds_ > kStuckSeconds)) {
                Enter(ThreatState::Wait);
            }
            break;
        }
        case ThreatState::Wait:
            if (stateSeconds_ >= kWaitSeconds) Enter(ThreatState::Return);
            break;
        case ThreatState::Return: {
            if (route_.empty()) route_ = PlanRoute(current_.position, returnTarget_, colliders);
            const float left = FollowRoute(kInvestigateSpeed, dt, colliders);
            const bool onLastLeg = routeIndex_ + 1 >= route_.size();
            if (onLastLeg && left < kArriveDistance) {
                distance_ = ClosestPathDistance();
                current_ = PoseAtDistance(distance_);
                state_ = ThreatState::Patrol;
                stateSeconds_ = 0.0f;
                route_.clear();
            } else if (onLastLeg && stuckSeconds_ > kStuckSeconds && !waypoints_.empty()) {
                // Still blocked at the end: aim at the waypoints in turn instead.
                returnTarget_ = waypoints_[returnWaypoint_ % waypoints_.size()];
                ++returnWaypoint_;
                route_.clear();
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

PoseSpec Threat::At(float alpha) const {
    PoseSpec p;
    p.position = math::Lerp(previous_.position, current_.position, alpha);
    p.yaw = LerpAngle(previous_.yaw, current_.yaw, alpha);
    p.pitch = 0.0f;
    return p;
}

// ---------------------------------------------------------------------------------------------
// Fan

void Fan::Tick(float dt) {
    previousAngle_ = angle_;
    const float target = powered_ ? TargetSpeed() : 0.0f;
    const float rate = TargetSpeed() / kSpinSeconds;
    if (speed_ < target) speed_ = std::min(target, speed_ + rate * dt);
    else if (speed_ > target) speed_ = std::max(target, speed_ - rate * dt);
    angle_ += speed_ * dt;
    const float turn = 2.0f * math::kPi;
    if (angle_ >= turn) angle_ -= turn * std::floor(angle_ / turn);
}

// ---------------------------------------------------------------------------------------------
// World

World::World(TwoRoomLevel level, ThreatBehaviour behaviour)
    : level_(std::move(level)), threat_(level_.threatPath, level_.threatSpeed), behaviour_(behaviour) {
    for (const LevelDoor& d : level_.doors) doors_.push_back(Door(d));
    if (doors_.empty()) doors_.push_back(Door(level_.door));  // A level without doors still has the proof's accessor.
    for (const LevelItem& it : level_.items) {
        const SocketSpec* start = FindSocket(it.startSocket);
        items_.push_back(Item(it, start != nullptr ? *start : SocketSpec{}));
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].HasLight()) {
            lampIndex_ = i;
            break;
        }
    }
    for (const LevelFan& f : level_.fans) fans_.push_back(Fan(f));
    circuits_ = level_.circuits;
    colliders_.Build(GetScene(), level_.colliders);
    for (const Door& d : doors_) colliders_.SetTransform(GetScene(), d.Handle().id, DoorTransform(d.Handle(), 0.0f));
    player_.Reset({level_.playerStart.position, level_.playerStart.yaw, level_.playerStart.pitch});
    threat_.SetBehaviour(behaviour_);
    lastDoorAngles_.assign(doors_.size(), 0.0f);
    lastItemPoses_.resize(items_.size());
    lastItemHidden_.assign(items_.size(), false);
    lastFanAngles_.assign(fans_.size(), 0.0f);
    lastThreatPose_ = threat_.Current();
    EvaluateCircuits(true);
    SaveCheckpoint();
    // The static level parks the threat at the check position; the simulation starts on the path.
    WriteRenderScene(GetScene(), 0.0f);
    GetScene().CommitRenderedFrame();
}

void World::Reset() {
    player_.Reset({level_.playerStart.position, level_.playerStart.yaw, level_.playerStart.pitch});
    doors_.clear();
    for (const LevelDoor& d : level_.doors) doors_.push_back(Door(d));
    if (doors_.empty()) doors_.push_back(Door(level_.door));
    items_.clear();
    for (const LevelItem& it : level_.items) {
        const SocketSpec* start = FindSocket(it.startSocket);
        items_.push_back(Item(it, start != nullptr ? *start : SocketSpec{}));
        if (it.hasLight) GetScene().SetEmitterOn(it.lightMaterial, true);  // Lit items start switched on.
    }
    fans_.clear();
    for (const LevelFan& f : level_.fans) fans_.push_back(Fan(f));
    circuits_ = level_.circuits;
    threat_ = Threat(level_.threatPath, level_.threatSpeed);
    threat_.SetBehaviour(behaviour_);
    for (const Door& d : doors_) colliders_.SetTransform(GetScene(), d.Handle().id, DoorTransform(d.Handle(), 0.0f));
    completedSteps_ = 0;
    phase_ = "introduction";
    EvaluateCircuits(true);
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
    lastDoorAngles_.assign(doors_.size(), 0.0f);
    lastItemPoses_.assign(items_.size(), PoseSpec{});
    lastItemHidden_.assign(items_.size(), false);
    lastFanAngles_.assign(fans_.size(), 0.0f);
    lastThreatPose_ = threat_.Current();
    WriteRenderScene(GetScene(), 0.0f);
}

const SocketSpec* World::FindSocket(const std::string& name) const {
    for (const SocketSpec& s : level_.sockets) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

const Item* World::FindItem(const std::string& id) const {
    for (const Item& it : items_) {
        if (it.Id() == id) return &it;
    }
    return nullptr;
}

Item* World::HeldItem() {
    for (Item& it : items_) {
        if (it.State() == ItemState::Held) return &it;
    }
    return nullptr;
}

const Item* World::HeldItem() const {
    for (const Item& it : items_) {
        if (it.State() == ItemState::Held) return &it;
    }
    return nullptr;
}

bool World::CircuitOn(const std::string& id) const {
    for (const CircuitState& c : circuits_) {
        if (c.id == id) return c.on;
    }
    return false;
}

const std::string& World::ObjectiveLine() const {
    if (Complete()) return level_.objectiveCompleteText;
    return level_.steps[completedSteps_].text;
}

void World::SaveCheckpoint() {
    checkpoint_.completedSteps = completedSteps_;
    checkpoint_.player = player_.Current();
    checkpoint_.doors.clear();
    for (const Door& d : doors_) checkpoint_.doors.emplace_back(d.State(), d.Angle());
    checkpoint_.items.clear();
    for (const Item& it : items_) checkpoint_.items.push_back(it.Snapshot());
    checkpoint_.fans.clear();
    for (const Fan& f : fans_) checkpoint_.fans.emplace_back(f.Angle(), f.Speed());
}

void World::RestartFromCheckpoint() {
    completedSteps_ = checkpoint_.completedSteps;
    phase_ = completedSteps_ == 0 ? "introduction" : level_.steps[completedSteps_ - 1].id;
    player_.Reset(checkpoint_.player);
    for (std::size_t i = 0; i < doors_.size() && i < checkpoint_.doors.size(); ++i) {
        doors_[i].Restore(checkpoint_.doors[i].first, checkpoint_.doors[i].second);
        colliders_.SetTransform(GetScene(), doors_[i].Handle().id, DoorTransform(doors_[i].Handle(), doors_[i].Angle()));
    }
    for (std::size_t i = 0; i < items_.size() && i < checkpoint_.items.size(); ++i) items_[i].Restore(checkpoint_.items[i], GetScene());
    for (std::size_t i = 0; i < fans_.size() && i < checkpoint_.fans.size(); ++i) fans_[i].Restore(checkpoint_.fans[i].first, checkpoint_.fans[i].second);
    EvaluateCircuits(true);
    // The machine goes back to its round; nothing is ever staged into the player's view.
    threat_ = Threat(level_.threatPath, level_.threatSpeed);
    threat_.SetBehaviour(behaviour_);
    ++restarts_;
    justRestarted_ = true;
    Event(std::format("tick {}: restart {} from the checkpoint '{}' (player at {:.2f}, {:.2f}, {:.2f})", ticks_, restarts_, phase_, checkpoint_.player.position.x,
                      checkpoint_.player.position.y, checkpoint_.player.position.z));
    WriteRenderScene(GetScene(), 1.0f);
}

ThreatSenses World::Sense() const {
    ThreatSenses s;
    const PlayerPose& pose = player_.Current();
    s.playerFeet = pose.position;
    if (!items_.empty() && items_[lampIndex_].HasLight()) {
        s.lampHeld = items_[lampIndex_].State() == ItemState::Held;
        s.lampOn = items_[lampIndex_].IsOn();
    }
    const Vec3 head = threat_.Current().position + Vec3{0.0f, 1.35f, 0.0f};
    s.lineOfSight = colliders_.SegmentClear(head, player_.EyePosition(pose));
    return s;
}

// Circuits powered by an item follow where the item sits; their fixtures, fans, and doors follow them.
void World::EvaluateCircuits(bool initial) {
    for (CircuitState& c : circuits_) {
        if (c.poweredByItem.empty()) continue;
        const Item* item = FindItem(c.poweredByItem);
        const bool on = item != nullptr && item->State() == ItemState::Placed && item->SocketName() == c.poweredBySocket;
        if (on == c.on && !initial) continue;
        const bool changed = on != c.on;
        c.on = on;
        for (const auto& [material, circuit] : level_.emitterCircuits) {
            if (circuit == c.id) GetScene().SetEmitterOn(material, on);
        }
        if (changed) Event(std::format("tick {}: circuit '{}' {}", ticks_, c.id, on ? "on" : "off"));
        for (Door& d : doors_) {
            if (d.OpensWithCircuit() == c.id && on && changed && d.State() != DoorState::Open && d.State() != DoorState::Opening) {
                d.Interact();
                Event(std::format("tick {}: door '{}' opens with circuit '{}'", ticks_, d.Id(), c.id));
            }
        }
    }
    for (Fan& f : fans_) f.SetPowered(CircuitOn(f.Level().circuit));
}

void World::UpdateObjective() {
    if (Complete()) return;
    const ObjectiveStep& step = level_.steps[completedSteps_];
    bool done = false;
    if (step.kind == "take") {
        const Item* it = FindItem(step.item);
        done = it != nullptr && it->State() == ItemState::Held;
    } else if (step.kind == "place") {
        const Item* it = FindItem(step.item);
        done = it != nullptr && it->State() == ItemState::Placed && it->SocketName() == step.socket;
    } else if (step.kind == "reach") {
        Vec3 to = player_.Current().position - step.markerPosition;
        to.y = 0.0f;
        done = math::Length(to) <= step.radius;
        if (!step.requiresItem.empty()) {
            const Item* it = FindItem(step.requiresItem);
            done = done && it != nullptr && it->State() == ItemState::Held;
        }
    }
    if (done) {
        const std::string before = phase_;
        ++completedSteps_;
        phase_ = step.id;
        justAdvanced_ = true;
        SaveCheckpoint();
        Event(std::format("tick {}: objective {} -> {}", ticks_, before, phase_));
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
    auto mixU8 = [&](std::uint8_t v) { mix(&v, 1); };
    mix(&ticks_, sizeof(ticks_));
    mixU8(static_cast<std::uint8_t>(completedSteps_));
    mixU8(static_cast<std::uint8_t>(threat_.State()));
    for (const Door& d : doors_) {
        mixU8(static_cast<std::uint8_t>(d.State()));
        mixFloat(d.Angle());
    }
    for (const Item& it : items_) {
        mixU8(static_cast<std::uint8_t>((it.State() == ItemState::Held ? 1 : 0) | (it.IsOn() ? 2 : 0)));
        mixU8(static_cast<std::uint8_t>(it.SocketName().size()));
    }
    for (const Fan& f : fans_) mixFloat(f.Speed());
    for (const CircuitState& c : circuits_) mixU8(c.on ? 1 : 0);
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
    auto consider = [&](InteractionTarget::Kind kind, const std::string& name, const std::string& text, const Vec3& point,
                        float reach) -> std::optional<InteractionTarget> {
        const Vec3 to = point - eye;
        const float distance = math::Length(to);
        if (distance > reach || distance <= 0.0f) return std::nullopt;
        const float alignment = math::Dot(to / distance, view);
        if (alignment < 0.6f) return std::nullopt;  // Must look at the target (within ~53 degrees).
        InteractionTarget t;
        t.kind = kind;
        t.name = name;
        t.text = text;
        t.distance = distance;
        t.alignment = alignment;
        return t;
    };
    std::optional<InteractionTarget> best;
    auto pick = [&](std::optional<InteractionTarget> candidate) {
        if (candidate && (!best || candidate->alignment > best->alignment)) best = candidate;
    };
    for (const Door& d : doors_) {
        if (d.Locked()) continue;
        // The leaf is tall: aim at it at eye height (within the leaf) so standing close and looking
        // straight ahead still targets it.
        Vec3 doorPoint = d.Handle().centre;
        doorPoint.y = std::clamp(eye.y, d.Handle().centre.y - 0.8f, d.Handle().centre.y + 0.8f);
        const bool opening = d.State() == DoorState::Closed || d.State() == DoorState::Closing;
        pick(consider(InteractionTarget::Kind::Door, d.Id(), opening ? "Open the door" : "Close the door", doorPoint, 2.0f));
    }
    const Item* held = HeldItem();
    for (const Item& it : items_) {
        if (it.State() != ItemState::Placed || held != nullptr) continue;  // One item in hand at a time.
        pick(consider(InteractionTarget::Kind::Item, it.Id(), "Take the " + it.Text(), it.Current().position + Vec3{0.0f, 0.1f, 0.0f}, Item::kReach));
    }
    if (held != nullptr) {
        for (const SocketSpec& s : level_.sockets) {
            if (std::find(s.accepts.begin(), s.accepts.end(), held->Id()) == s.accepts.end()) continue;
            bool occupied = false;
            for (const Item& it : items_) occupied = occupied || (it.State() == ItemState::Placed && it.SocketName() == s.name);
            if (occupied) continue;
            const std::string text = held->HasLight() ? "Place the " + held->Text() + " on the " + s.text : "Put the " + held->Text() + " in the " + s.text;
            pick(consider(InteractionTarget::Kind::Socket, s.name, text, s.position, Item::kReach));
        }
    }
    return best;
}

void World::Tick(const InputFrame& input, float dt) {
    justRestarted_ = false;
    justAdvanced_ = false;
    if (input.interactPressed) {
        if (const auto target = CurrentInteraction()) {
            if (target->kind == InteractionTarget::Kind::Door) {
                for (Door& d : doors_) {
                    if (d.Id() == target->name) {
                        d.Interact();
                        log::Debug("tick {}: door '{}' -> {}", ticks_, d.Id(), DoorStateName(d.State()));
                    }
                }
            } else if (target->kind == InteractionTarget::Kind::Item) {
                for (Item& it : items_) {
                    if (it.Id() == target->name) {
                        it.PickUp();
                        log::Debug("tick {}: {} picked up", ticks_, it.Id());
                    }
                }
            } else if (Item* held = HeldItem()) {
                if (const SocketSpec* s = FindSocket(target->name)) {
                    held->Place(*s);
                    log::Debug("tick {}: {} placed on {}", ticks_, held->Id(), s->name);
                }
            }
        }
    }
    if (input.lampPressed && !items_.empty() && items_[lampIndex_].HasLight()) {
        items_[lampIndex_].Toggle(GetScene());
        log::Debug("tick {}: lamp {}", ticks_, items_[lampIndex_].IsOn() ? "on" : "off");
    }
    player_.Tick(input, dt, &colliders_);
    for (Door& d : doors_) {
        d.Tick(dt);
        // The leaf's collision follows its state; a leaf closing into the player swings back open.
        colliders_.SetTransform(GetScene(), d.Handle().id, DoorTransform(d.Handle(), d.Angle()));
        if (d.State() == DoorState::Closing && colliders_.CapsuleOverlaps(player_.Current().position, Player::kCapsule)) {
            d.Block();
            colliders_.SetTransform(GetScene(), d.Handle().id, DoorTransform(d.Handle(), d.Angle()));
            ++doorBlocks_;
            log::Debug("tick {}: door '{}' blocked by the player, reopening", ticks_, d.Id());
        }
    }
    const ThreatState threatBefore = threat_.State();
    threat_.Tick(dt, Sense(), &colliders_);
    if (threat_.State() != threatBefore) {
        Event(std::format("tick {}: threat {} -> {}", ticks_, ThreatStateName(threatBefore), ThreatStateName(threat_.State())));
    }
    for (Item& it : items_) it.Tick(player_, &colliders_);
    EvaluateCircuits(false);
    for (Fan& f : fans_) f.Tick(dt);
    UpdateObjective();
    if (threat_.CaughtPlayer() && !Complete()) {
        ++catches_;
        Event(std::format("tick {}: caught ({}) at ({:.2f}, {:.2f}, {:.2f})", ticks_, catches_, player_.Current().position.x, player_.Current().position.y,
                          player_.Current().position.z));
        RestartFromCheckpoint();
        for (Item& it : items_) it.Tick(player_, &colliders_);
    }
    ++ticks_;
    HashTick();
}

Mat4 World::ItemTransform(const Item& item, const PoseSpec& pose, const PlayerPose& player) const {
    if (item.State() == ItemState::Held && item.HidesWhenCarried()) {
        // Pocketed: inside the torso box, where the closed body geometry hides it.
        const PoseSpec feet{player.position, player.yaw, 0.0f};
        return PlayerTorsoTransform(feet) * Mat4::Translation({0.0f, -0.1f, 0.0f});
    }
    return item.HasLight() ? LampHousingTransform(pose) : ItemBodyTransform(pose, item.Half());
}

bool World::WriteRenderScene(Scene& scene, float alpha) {
    bool changed = false;
    for (std::size_t i = 0; i < doors_.size(); ++i) {
        const float doorAngle = doors_[i].AngleAt(alpha);
        if (!wroteOnce_ || doorAngle != lastDoorAngles_[i]) {
            scene.SetTransform(doors_[i].Handle().id, DoorTransform(doors_[i].Handle(), doorAngle));
            lastDoorAngles_[i] = doorAngle;
            changed = true;
        }
    }
    const PlayerPose playerPose = player_.At(alpha);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        const PoseSpec pose = it.PoseAt(alpha);
        const bool hidden = it.State() == ItemState::Held && it.HidesWhenCarried();
        if (!wroteOnce_ || !SamePose(pose, lastItemPoses_[i]) || hidden != lastItemHidden_[i] || hidden) {
            scene.SetTransform(level_.items[i].body, ItemTransform(it, pose, playerPose));
            if (it.HasLight()) scene.SetTransform(level_.items[i].face, LampFaceTransform(pose, it.FaceOffset()));
            lastItemPoses_[i] = pose;
            lastItemHidden_[i] = hidden;
            changed = true;
        }
    }
    for (std::size_t i = 0; i < fans_.size(); ++i) {
        const float angle = fans_[i].AngleAt(alpha);
        if (!wroteOnce_ || angle != lastFanAngles_[i]) {
            const LevelFan& f = level_.fans[i];
            const Mat4 spin = Mat4::Translation(f.centre) * Mat4::RotationAxis(f.axis, angle);
            scene.SetTransform(f.hub, spin * f.hubLocal);
            for (std::size_t k = 0; k < f.blades.size(); ++k) scene.SetTransform(f.blades[k], spin * f.bladeLocal[k]);
            lastFanAngles_[i] = angle;
            changed = true;
        }
    }
    const PoseSpec threatPose = threat_.At(alpha);
    if (!wroteOnce_ || !SamePose(threatPose, lastThreatPose_)) {
        scene.SetTransform(level_.threatBody, ThreatBodyTransform(threatPose));
        scene.SetTransform(level_.threatHead, ThreatHeadTransform(threatPose));
        lastThreatPose_ = threatPose;
        changed = true;
    }
    if (level_.playerTorso.value != 0) {
        const PoseSpec feet{playerPose.position, playerPose.yaw, 0.0f};
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
