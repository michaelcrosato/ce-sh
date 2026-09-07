// Diagnostic view selection. Diagnostic colours never enter the production lighting output.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace lc {

// Values match LC_VIEW_* in shaders/shared/layouts.hlsli.
enum class ViewMode : std::uint32_t {
    Normals = 0,       // World-space geometric normal * 0.5 + 0.5.
    InstanceIds = 1,   // Hash colour per stable instance id.
    Depth = 2,         // Hit distance, 0 m black to 20 m white.
    Barycentrics = 3,  // Triangle barycentric weights as RGB.
    FrontFace = 4,     // Green front, red back, magenta when RayQuery facing and winding disagree.
    PrimitiveIds = 5,  // Hash colour per triangle.
};

inline constexpr std::string_view kViewModeNames[] = {"normals", "ids", "depth", "bary", "facing", "prims"};

inline std::optional<ViewMode> ViewModeFromName(std::string_view name) {
    for (std::uint32_t i = 0; i < std::size(kViewModeNames); ++i) {
        if (kViewModeNames[i] == name) {
            return static_cast<ViewMode>(i);
        }
    }
    return std::nullopt;
}

inline std::string_view ViewModeName(ViewMode mode) {
    const auto index = static_cast<std::uint32_t>(mode);
    return index < std::size(kViewModeNames) ? kViewModeNames[index] : "unknown";
}

}  // namespace lc
