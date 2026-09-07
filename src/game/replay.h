// Deterministic input replay (spec §4, §21). A replay file holds run-length encoded input segments
// and checks that --validate evaluates when the simulation is stopped at a given tick.
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
struct ReplayCheck {
    std::uint64_t tick = 0;
    std::string kind;                        // "hit", "not_visible", "patch_positive", "patch_zero", "patch_ratio".
    std::string description;
    std::optional<math::Vec3> point;         // World point projected at validation time.
    std::optional<math::Vec2> pixel;         // Or a normalized pixel.
    std::string entity;                      // "hit": the entity whose id the first non-mirror hit must carry; "not_visible": must not appear outside the mirror.
    std::optional<math::Vec3> otherPoint;    // "patch_ratio".
    float minimum = 1e-3f;                   // "patch_positive".
    float tolerance = 1e-6f;                 // "patch_zero".
    float ratioFactor = 1.2f;                // "patch_ratio".
    std::uint32_t halfSize = 2;
};

struct Replay {
    std::uint32_t version = 1;
    std::string scene;
    std::uint32_t tickRate = 60;
    std::uint32_t seed = 0;
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
