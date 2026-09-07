#include "scene/material.h"

#include <cmath>
#include <format>

namespace lc {

namespace {

bool InUnitRange(math::Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && v.x >= 0.0f && v.y >= 0.0f && v.z >= 0.0f &&
           v.x <= 1.0f && v.y <= 1.0f && v.z <= 1.0f;
}

bool NonNegativeFinite(math::Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && v.x >= 0.0f && v.y >= 0.0f && v.z >= 0.0f;
}

}  // namespace

const char* MaterialTypeName(MaterialType type) {
    switch (type) {
        case MaterialType::Diffuse: return "diffuse";
        case MaterialType::Mirror: return "mirror";
        case MaterialType::Emitter: return "emitter";
    }
    return "unknown";
}

std::vector<std::string> ValidateMaterial(const Material& m) {
    std::vector<std::string> problems;
    const std::string name = m.name.empty() ? "<unnamed>" : m.name;
    if (!InUnitRange(m.reflectance)) {
        problems.push_back(std::format("material '{}' reflectance ({}, {}, {}) must be finite and within [0, 1]; reflective "
                                       "materials must not gain energy",
                                       name, m.reflectance.x, m.reflectance.y, m.reflectance.z));
    }
    if (!NonNegativeFinite(m.radiance)) {
        problems.push_back(std::format("material '{}' radiance ({}, {}, {}) must be finite and non-negative", name, m.radiance.x,
                                       m.radiance.y, m.radiance.z));
    }
    if (m.type != MaterialType::Emitter && (m.radiance.x > 0.0f || m.radiance.y > 0.0f || m.radiance.z > 0.0f)) {
        problems.push_back(std::format("material '{}' is {} but has radiance; only emitters emit", name, MaterialTypeName(m.type)));
    }
    if (m.type == MaterialType::Emitter && m.emitterOn && Luminance(m.radiance) <= 0.0f) {
        problems.push_back(std::format("material '{}' is an active emitter with zero radiance; turn it off or give it radiance", name));
    }
    return problems;
}

float Luminance(math::Vec3 rgb) { return 0.2126f * rgb.x + 0.7152f * rgb.y + 0.0722f * rgb.z; }

}  // namespace lc
