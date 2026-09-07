// Diagnostic view selection. Diagnostic colours never enter the production lighting output.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace lc {

// Values match LC_VIEW_* in shaders/shared/layouts.hlsli.
enum class ViewMode : std::uint32_t {
    // Diagnostic pass (camera_view.hlsl, --mode diag).
    Normals = 0,       // World-space geometric normal * 0.5 + 0.5 (denoised mode: the virtual guide normal).
    InstanceIds = 1,   // Hash colour per stable instance id.
    Depth = 2,         // Hit distance, 0 m black to 20 m white (denoised mode: |view Z|).
    Barycentrics = 3,  // Triangle barycentric weights as RGB.
    FrontFace = 4,     // Green front, red back, magenta when RayQuery facing and winding disagree.
    PrimitiveIds = 5,  // Hash colour per triangle.
    // Denoised mode only (post/compose.hlsl draws them on the display image).
    Motion = 6,        // World-space motion of the guide surface: rgb = xyz * 10 + 0.5.
    ViewZ = 7,         // |view Z| 0 m black to 20 m white.
    History = 8,       // NRD history: red = reset/rejected, green = diffuse age, blue = specular age.
    Diffuse = 9,       // Denoised diffuse radiance (modulated), exposed.
    Specular = 10,     // Denoised specular radiance (modulated), exposed.
    Raw = 11,          // This frame's recomposed raw sample, exposed.
    Direct = 12,       // Direct light at the guide surface (this frame's sample), exposed.
    Indirect = 13,     // Denoised diffuse + specular minus the direct sample, exposed.
    Validation = 14,   // NRD validation overlay over the production image.
};

inline constexpr std::string_view kViewModeNames[] = {"normals", "ids",      "depth",    "bary", "facing", "prims",    "motion",   "viewz",
                                                      "history", "diffuse",  "specular", "raw",  "direct", "indirect", "validation"};

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

// Views that only the denoised mode can draw.
inline bool IsDenoisedOnlyView(ViewMode mode) { return static_cast<std::uint32_t>(mode) >= static_cast<std::uint32_t>(ViewMode::Motion); }

// Views that only the diagnostic pass can draw (identity and winding views).
inline bool IsDiagnosticOnlyView(ViewMode mode) {
    return mode == ViewMode::InstanceIds || mode == ViewMode::Barycentrics || mode == ViewMode::FrontFace || mode == ViewMode::PrimitiveIds;
}

}  // namespace lc
