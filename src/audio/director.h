// The audio rules without a device (spec §15): positional attenuation and stereo placement from
// the listener frame, a documented wall-occlusion factor, and sound events that follow the world
// state (a fixture that switches off stops humming, a machine that stops moving falls silent).
// The director turns world snapshots into commands; the device layer (audio_system.h) plays them.
#pragma once

#include "audio/clips.h"
#include "core/math/vec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lc::audio {

struct Listener {
    math::Vec3 position;              // The eye.
    math::Vec3 forward{0.0f, 0.0f, -1.0f};
    math::Vec3 right{1.0f, 0.0f, 0.0f};
};

struct MixSettings {
    float referenceDistance = 1.0f;   // Full level up to this distance.
    float maxDistance = 30.0f;        // Silent beyond it.
    float occlusionFactor = 0.3f;     // Documented approximation: a solid between source and listener.
};

struct MixResult {
    float gain = 0.0f;      // Distance and occlusion applied (the source's own level included).
    float pan = 0.0f;       // -1 left .. +1 right (constant-power law applied by the device layer).
    float distance = 0.0f;
    bool audible = false;
};

// Inverse-square attenuation clamped to the reference distance; pan from the listener's right axis.
MixResult ComputeMix(const Listener& listener, math::Vec3 source, float sourceGain, bool occluded, const MixSettings& settings);

enum class Category : std::uint8_t { Effects, Ambience };

struct Command {
    enum class Kind : std::uint8_t { StartLoop, StopLoop, MoveLoop, PlayOneShot, Cue };
    Kind kind = Kind::PlayOneShot;
    std::string key;                // Loop identity ("hum:fixture_a", "threat", "room_tone").
    ClipId clip = ClipId::Count;
    Category category = Category::Effects;
    math::Vec3 position;            // World position for positional sounds.
    float gain = 1.0f;
    bool positional = true;
    bool occludable = true;         // Non-positional sounds and the player's own steps are never occluded.
    std::string text;               // Kind::Cue: the text cue for an important sound.
};

struct FixtureSnapshot {
    std::string name;
    math::Vec3 position;
    bool on = true;
    ClipId hum = ClipId::FixtureHum;
};

struct DoorSnapshot {
    bool moving = false;
    bool closed = true;             // Fully closed.
    math::Vec3 position;
};

struct ItemSnapshot {
    bool held = false;
    math::Vec3 position;            // The item transform's origin (the same transform as the render).
};

struct FanSnapshot {
    math::Vec3 position;
    float speedFraction = 0.0f;     // 0 stopped .. 1 full speed.
};

// Everything the rules need, read from the world each frame (no rendering data).
struct WorldSnapshot {
    math::Vec3 playerFeet;
    std::vector<DoorSnapshot> doors;
    bool lampOn = true;             // The lit item's switch.
    bool lampHeld = false;
    math::Vec3 lampPosition;        // The fixture transform's origin (the same transform as the render).
    std::vector<ItemSnapshot> items;  // Every carried item (handling sounds on pick-up and placement).
    std::vector<FanSnapshot> fans;
    math::Vec3 threatPosition;
    bool threatPresent = true;
    bool threatOccluded = false;    // A solid between the listener and the threat (the cue respects it).
    std::vector<FixtureSnapshot> fixtures;
    bool objectiveChime = false;    // One-frame events from the objective (M5 Task 5).
    bool catchSting = false;
};

class Director {
public:
    static constexpr float kStepDistance = 0.62f;   // Metres of walking per footstep.
    static constexpr float kThreatCueDistance = 6.0f;         // Cue range with a clear line.
    static constexpr float kThreatCueOccludedDistance = 3.0f; // Cue range through a wall.
    static constexpr float kThreatCuePeriod = 5.0f; // Seconds between "nearby" cues.
    static constexpr float kMoveEpsilon = 1e-4f;    // Position change that counts as motion per update.
    static constexpr float kMoveHold = 0.25f;       // Seconds the movement loop survives without a new position (frames between ticks).

    // Commands for this update. The first update starts every loop that the state calls for.
    std::vector<Command> Update(const WorldSnapshot& snapshot, float dt);
    void Reset() { started_ = false; }

private:
    bool started_ = false;
    WorldSnapshot last_;
    float walked_ = 0.0f;
    bool stepVariant_ = false;
    bool threatLoopOn_ = false;
    float threatStillSeconds_ = 0.0f;
    float threatCueTimer_ = 0.0f;
    std::vector<std::string> humsOn_;
};

}  // namespace lc::audio
