// Explicit material types (spec §7). Parameters are scene-linear RGB, unitless:
//   Diffuse:        reflectance (albedo) in [0, 1] per channel, Lambertian.
//   Mirror:         ideal specular reflectance in [0, 1] per channel (1 allowed for test mirrors).
//   Emitter:        front-side radiance >= 0 in the documented radiance scale plus a diffuse
//                   reflectance in [0, 1] for the surface itself (used when off, from the back, or
//                   for light arriving at the surface). The back of a one-sided emitter never emits.
//   RoughConductor: GGX microfacet conductor; reflectance is the Fresnel reflectance at normal
//                   incidence (F0) in [0, 1], roughness in [0.02, 1] is perceptual (alpha = roughness^2).
#pragma once

#include "core/math/vec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lc {

enum class MaterialType : std::uint32_t { Diffuse = 0, Mirror = 1, Emitter = 2, RoughConductor = 3 };

inline constexpr float kMinRoughness = 0.02f;
inline constexpr float kMaxRoughness = 1.0f;

struct Material {
    std::string name;
    MaterialType type = MaterialType::Diffuse;
    math::Vec3 reflectance{0.5f, 0.5f, 0.5f};
    math::Vec3 radiance{0.0f, 0.0f, 0.0f};  // Emitter only.
    bool emitterOn = true;                   // Emitter only: an off source stays in the scene and reflects.
    float roughness = 0.5f;                  // RoughConductor only.

    bool IsActiveEmitter() const { return type == MaterialType::Emitter && emitterOn; }
};

const char* MaterialTypeName(MaterialType type);

// Empty when valid; otherwise one message per problem.
std::vector<std::string> ValidateMaterial(const Material& material);

// Rec. 709 luminance of scene-linear RGB; used for emitter power weights.
float Luminance(math::Vec3 rgb);

}  // namespace lc
