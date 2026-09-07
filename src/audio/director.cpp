#include "audio/director.h"

#include <algorithm>
#include <cmath>

namespace lc::audio {

using math::Vec3;

MixResult ComputeMix(const Listener& listener, Vec3 source, float sourceGain, bool occluded, const MixSettings& settings) {
    MixResult r;
    const Vec3 to = source - listener.position;
    r.distance = math::Length(to);
    if (r.distance >= settings.maxDistance || sourceGain <= 0.0f) {
        return r;
    }
    const float ref = std::max(settings.referenceDistance, 1e-3f);
    const float attenuation = r.distance <= ref ? 1.0f : (ref * ref) / (r.distance * r.distance);
    r.gain = sourceGain * attenuation * (occluded ? settings.occlusionFactor : 1.0f);
    r.pan = r.distance > 0.05f ? std::clamp(math::Dot(to / r.distance, listener.right), -1.0f, 1.0f) : 0.0f;
    r.audible = r.gain > 1e-4f;
    return r;
}

namespace {

Command Loop(Command::Kind kind, std::string key, ClipId clip, Category category, Vec3 position, float gain, bool positional) {
    Command c;
    c.kind = kind;
    c.key = std::move(key);
    c.clip = clip;
    c.category = category;
    c.position = position;
    c.gain = gain;
    c.positional = positional;
    c.occludable = positional;
    return c;
}

Command OneShot(ClipId clip, Vec3 position, float gain, bool positional, bool occludable) {
    Command c;
    c.kind = Command::Kind::PlayOneShot;
    c.clip = clip;
    c.category = Category::Effects;
    c.position = position;
    c.gain = gain;
    c.positional = positional;
    c.occludable = occludable;
    return c;
}

Command Cue(std::string text) {
    Command c;
    c.kind = Command::Kind::Cue;
    c.text = std::move(text);
    return c;
}

std::string HumKey(const std::string& fixture) { return "hum:" + fixture; }

}  // namespace

std::vector<Command> Director::Update(const WorldSnapshot& s, float dt) {
    std::vector<Command> out;
    const bool first = !started_;
    started_ = true;

    // Ambience bed: always on, never positional.
    if (first) {
        out.push_back(Loop(Command::Kind::StartLoop, "room_tone", ClipId::RoomTone, Category::Ambience, {}, 1.0f, false));
    }

    // Fixture hums follow the emitter state (spec §15: turning off a fixture stops its hum).
    std::vector<std::string> humsNow;
    for (const FixtureSnapshot& f : s.fixtures) {
        const std::string key = HumKey(f.name);
        const bool was = std::find(humsOn_.begin(), humsOn_.end(), key) != humsOn_.end();
        if (f.on) {
            humsNow.push_back(key);
            if (!was) {
                out.push_back(Loop(Command::Kind::StartLoop, key, f.hum, Category::Ambience, f.position, 1.0f, true));
            } else {
                // Moving fixtures (the lamp) carry their hum with them.
                const FixtureSnapshot* prev = nullptr;
                for (const FixtureSnapshot& p : last_.fixtures) {
                    if (p.name == f.name) prev = &p;
                }
                if (prev == nullptr || math::Length(prev->position - f.position) > kMoveEpsilon) {
                    out.push_back(Loop(Command::Kind::MoveLoop, key, f.hum, Category::Ambience, f.position, 1.0f, true));
                }
            }
        } else if (was) {
            out.push_back(Loop(Command::Kind::StopLoop, key, f.hum, Category::Ambience, f.position, 1.0f, true));
        }
    }
    for (const std::string& key : humsOn_) {
        // A fixture that vanished from the snapshot (scene reload) stops too.
        const bool stillListed = std::any_of(s.fixtures.begin(), s.fixtures.end(), [&](const FixtureSnapshot& f) { return HumKey(f.name) == key; });
        if (!stillListed) out.push_back(Loop(Command::Kind::StopLoop, key, ClipId::FixtureHum, Category::Ambience, {}, 1.0f, true));
    }
    humsOn_ = std::move(humsNow);

    // Door: creak when a leaf starts to move, a thud when it reaches an end stop.
    if (!first) {
        if (s.doorMoving && !last_.doorMoving) {
            out.push_back(OneShot(ClipId::DoorCreak, s.doorPosition, 1.0f, true, true));
            out.push_back(Cue("The door creaks"));
        }
        if (!s.doorMoving && last_.doorMoving) {
            out.push_back(OneShot(ClipId::DoorThud, s.doorPosition, s.doorClosed ? 1.0f : 0.6f, true, true));
            if (s.doorClosed) out.push_back(Cue("The door shuts"));
        }
        // Lamp: the switch and the handling.
        if (s.lampOn != last_.lampOn) {
            out.push_back(OneShot(ClipId::LampClick, s.lampPosition, 1.0f, true, true));
            out.push_back(Cue(s.lampOn ? "Click: the lamp is on" : "Click: the lamp is off"));
        }
        if (s.lampHeld != last_.lampHeld) {
            out.push_back(OneShot(ClipId::LampHandle, s.lampPosition, 0.8f, true, true));
        }
        // Footsteps every kStepDistance metres of the player's own motion (never occluded).
        walked_ += math::Length(s.playerFeet - last_.playerFeet);
        while (walked_ >= kStepDistance) {
            walked_ -= kStepDistance;
            out.push_back(OneShot(stepVariant_ ? ClipId::FootstepB : ClipId::FootstepA, s.playerFeet, 0.7f, false, false));
            stepVariant_ = !stepVariant_;
        }
    }

    // Threat: the movement loop runs only while it moves (a stopped machine is silent). Positions
    // change per simulation tick, so a frame without a tick keeps the loop for a short hold.
    const bool threatMovedNow = s.threatPresent && !first && math::Length(s.threatPosition - last_.threatPosition) > kMoveEpsilon;
    threatStillSeconds_ = threatMovedNow ? 0.0f : threatStillSeconds_ + dt;
    const bool threatMoving = threatMovedNow || (threatLoopOn_ && threatStillSeconds_ < kMoveHold);
    const Vec3 threatSource = s.threatPosition + Vec3{0.0f, 1.0f, 0.0f};  // Its body, not its feet on the floor slab.
    if (threatMoving && !threatLoopOn_) {
        out.push_back(Loop(Command::Kind::StartLoop, "threat", ClipId::ThreatMove, Category::Effects, threatSource, 1.0f, true));
        threatLoopOn_ = true;
    } else if (!threatMoving && threatLoopOn_) {
        out.push_back(Loop(Command::Kind::StopLoop, "threat", ClipId::ThreatMove, Category::Effects, threatSource, 1.0f, true));
        threatLoopOn_ = false;
    } else if (threatMovedNow) {
        out.push_back(Loop(Command::Kind::MoveLoop, "threat", ClipId::ThreatMove, Category::Effects, threatSource, 1.0f, true));
    }
    threatCueTimer_ = std::max(0.0f, threatCueTimer_ - dt);
    const float threatDistance = math::Length(s.threatPosition - s.playerFeet);
    const bool threatNear = threatDistance < kThreatCueOccludedDistance || (!s.threatOccluded && threatDistance < kThreatCueDistance);
    if (threatMovedNow && threatNear && threatCueTimer_ <= 0.0f) {
        out.push_back(Cue("A machine whirs nearby"));
        threatCueTimer_ = kThreatCuePeriod;
    }

    // Objective events (one frame each).
    if (s.objectiveChime) {
        out.push_back(OneShot(ClipId::ExitChime, s.playerFeet, 0.8f, false, false));
        out.push_back(Cue("A chime"));
    }
    if (s.catchSting) {
        out.push_back(OneShot(ClipId::CatchSting, s.playerFeet, 1.0f, false, false));
        out.push_back(Cue("Caught: back to the last checkpoint"));
    }

    last_ = s;
    return out;
}

}  // namespace lc::audio
