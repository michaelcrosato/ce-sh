// Automated checks attached to test scenes. Positions are world points projected with the actual
// camera and aspect at validation time, so a changed FOV or resolution keeps the checks valid.
#pragma once

#include "core/math/vec.h"

#include <cstdint>
#include <optional>
#include <string>

namespace lc {

// Camera-ray identity check (M1): the pixel must hit a given stable instance id (0 = miss). In the
// path tracer the identity is that of the first non-mirror surface along the path.
struct HitExpectation {
    float u = 0.0f;                          // Used when worldPoint is empty.
    float v = 0.0f;
    std::optional<math::Vec3> worldPoint;    // Projected at validation time when set.
    std::uint32_t expectedStableId = 0;      // 0 = must miss.
    bool expectFrontFace = true;
    std::string description;
};

struct RadianceExpectation {
    enum class Kind {
        ZeroImage,         // Every pixel: max |channel| <= absoluteTolerance (T03, T04 sealed).
        PositivePatch,     // Patch mean luminance > minimum.
        AnalyticPatch,     // |patch mean (channel average) - expected| <= max(relativeTolerance * expected, 3 * standard error).
        RatioGreaterThan,  // R / (G + B) of the patch exceeds ratioFactor times the same ratio at otherPoint.
    };
    Kind kind = Kind::PositivePatch;
    std::string description;
    math::Vec3 point;                 // Patch centre (world).
    math::Vec3 otherPoint;            // RatioGreaterThan comparison patch centre (world).
    std::uint32_t halfSize = 2;       // Patch is (2 * halfSize + 1)^2 pixels.
    float expected = 0.0f;
    float relativeTolerance = 0.02f;
    float minimum = 1e-3f;
    float ratioFactor = 1.2f;
    float absoluteTolerance = 1e-6f;
};

// Named patch whose mean and standard error are written by --stats.
struct StatsPatch {
    std::string name;
    math::Vec3 point;
    std::uint32_t halfSize = 3;
};

}  // namespace lc
