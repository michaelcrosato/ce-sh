// Deterministic input replay (spec §4, §21). A replay file holds run-length encoded input segments
// and checks that --validate evaluates when the simulation is stopped at a given tick (or, for
// frame-by-frame runs, at the final tick).
#pragma once

#include "core/math/vec.h"
#include "game/input.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace lc::game {

struct InputSegment {
    std::uint64_t fromTick = 0;
    std::uint64_t toTick = 0;  // Inclusive.
    InputFrame input;
};

// Checks reference entities by name; the application resolves names to stable ids.
//   "hit"            the pixel of `point` (or `pixel`) reports `entity` as the first non-mirror surface.
//   "not_visible"    no pixel reports `entity` directly (zero mirror bounces).
//   "patch_positive" / "patch_dark" / "patch_zero" / "patch_ratio"   luminance patches on the linear image
//                    (raw or reference mean; in denoised mode the recomposed raw mean).
//   "denoised_patch_positive" / "denoised_patch_dark"   the same on the denoised image (denoised mode only).
//   "motion"         the pixel reports `entity` and its guide motion equals the entity's previous minus current
//                    position within `tolerance` metres; `reflected` mirrors the expectation across the level's
//                    mirror plane (the pixel sees the entity through the mirror).
//   "static_motion"  the pixel reports `entity` and its guide motion is exactly zero.
//   "trail_lag"      per-frame patch statistics from `fromFrame` to the final frame; after `entity` leaves the
//                    patch the denoised luminance must settle within `maxLagFrames` frames (T12).
// World-state checks (no image; evaluated on the CPU right after the tick, also without a GPU):
//   "objective_state"  the objective phase equals `state` ("introduction", "lamp_acquired", ...).
//   "threat_state"     the threat's state equals `state` ("patrol", "chase", "investigate", "wait", "return").
//   "caught_count"     the number of catches so far equals `count`.
//   "player_near"      the player's feet are within `tolerance` metres of `point` (horizontal).
struct ReplayCheck {
    std::uint64_t tick = 0;
    std::string kind;
    std::string description;
    std::optional<math::Vec3> point;         // World point projected at validation time.
    std::optional<math::Vec2> pixel;         // Or a normalized pixel.
    std::string entity;
    std::optional<math::Vec3> otherPoint;    // "patch_ratio".
    float minimum = 1e-3f;                   // "patch_positive": mean luminance must exceed.
    float maximum = 1e-3f;                   // "patch_dark": mean luminance must stay below.
    float tolerance = 1e-6f;                 // "patch_zero"; "motion": metres.
    float ratioFactor = 1.2f;                // "patch_ratio".
    std::uint32_t halfSize = 2;
    bool reflected = false;                  // "motion".
    std::uint32_t maxLagFrames = 6;          // "trail_lag": 100 ms at 60 Hz.
    float settleFraction = 0.8f;             // "trail_lag": fraction of the final change that counts as settled.
    std::uint64_t fromFrame = 0;             // "trail_lag": first recorded frame.
    std::string state;                       // "objective_state" / "threat_state".
    std::uint32_t count = 0;                 // "caught_count".
};

inline constexpr const char* kReplayCheckKinds[] = {"hit",    "not_visible",   "patch_positive",          "patch_dark",
                                                    "patch_zero", "patch_ratio", "denoised_patch_positive", "denoised_patch_dark",
                                                    "motion", "static_motion", "trail_lag",
                                                    "objective_state", "threat_state", "caught_count", "player_near"};

// True for kinds that need statistics from every frame, not only the final one.
inline bool IsPerFrameCheck(const ReplayCheck& check) { return check.kind == "trail_lag"; }
// True for kinds evaluated from the world state right after their tick (no image).
inline bool IsStateCheck(const ReplayCheck& check) {
    return check.kind == "objective_state" || check.kind == "threat_state" || check.kind == "caught_count" || check.kind == "player_near";
}

struct Replay {
    std::uint32_t version = 1;
    std::string scene;
    std::uint32_t tickRate = 60;
    std::uint32_t seed = 0;
    bool hunt = false;                   // "threat": "hunt" enables the state machine; the default keeps the deterministic path.
    std::vector<InputSegment> segments;  // Sorted by fromTick, non-overlapping.
    std::vector<ReplayCheck> checks;

    // Input for a tick: the segment covering it, otherwise neutral.
    InputFrame InputAt(std::uint64_t tick) const;

    // Recording: appends the tick, extending the last segment when the input is identical.
    void Record(std::uint64_t tick, const InputFrame& input);

    // Last tick with recorded input (0 when empty).
    std::uint64_t LastTick() const;

    std::string ToJson() const;
    static std::optional<Replay> FromJson(std::string_view text, std::string& error);
    static std::optional<Replay> Load(const std::filesystem::path& path, std::string& error);
    bool Save(const std::filesystem::path& path) const;
};

}  // namespace lc::game
