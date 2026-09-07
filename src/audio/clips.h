// Generated sound clips (spec §15: original or generated assets with provenance). Every clip is
// synthesised here from the parameters recorded in its provenance string; nothing is loaded from
// disk. Mono float samples at 48 kHz.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lc::audio {

inline constexpr std::uint32_t kSampleRate = 48000;

enum class ClipId : std::uint8_t {
    FixtureHum,     // Ceiling fixture buzz (loop).
    EmergencyHum,   // Hall emergency fixture, thinner and higher (loop).
    LampHum,        // Portable lamp, faint (loop).
    FanLoop,        // Fan rotation (loop; no fan exists in the two-room proof, staged for M6).
    RoomTone,       // Ambience bed (loop, non-positional).
    ThreatMove,     // Maintenance machine moving: motor tone with ticks (loop).
    DoorCreak,      // A leaf starting to swing.
    DoorThud,       // A leaf reaching its end stop.
    LampClick,      // The lamp switch.
    LampHandle,     // Picking the lamp up or setting it down.
    FootstepA,      // Player footsteps (two variants alternate).
    FootstepB,
    ExitChime,      // Objective progress / exit.
    CatchSting,     // The catch.
    Count
};

struct Clip {
    ClipId id = ClipId::Count;
    std::string name;
    bool loop = false;
    std::vector<float> samples;  // Mono, [-1, 1].
    std::string provenance;      // Generator and parameters, for the records.
};

const char* ClipName(ClipId id);
// Deterministic: the same clips every run (seeded hash noise).
Clip GenerateClip(ClipId id);
std::vector<Clip> GenerateAllClips();

}  // namespace lc::audio
