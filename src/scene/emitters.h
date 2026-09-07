// Emitter extraction: the sampling distribution is derived from the very triangles that are
// visible as emitting surfaces, transformed with the same instance transforms (spec §7).
#pragma once

#include "core/math/vec.h"
#include "scene/scene.h"

#include <cstdint>
#include <vector>

namespace lc {

struct EmitterTriangleCpu {
    std::uint32_t instanceIndex = 0;   // Index into Scene::Instances() (== TLAS instance index).
    std::uint32_t primitiveIndex = 0;  // Triangle index within the instance's mesh.
    float area = 0.0f;                 // World-space area in square metres.
    float cdf = 0.0f;                  // Cumulative area fraction within the emitter; last triangle = 1.
};

struct EmitterCpu {
    std::uint32_t instanceIndex = 0;
    std::uint32_t firstTriangle = 0;   // Index into EmitterTable::triangles.
    std::uint32_t triangleCount = 0;
    std::uint32_t materialIndex = 0;
    float area = 0.0f;                 // Total world-space area.
    float power = 0.0f;                // Luminance(radiance) * area; selection weight.
    float selectionPdf = 0.0f;         // power / totalPower; exact probability used by the integrator.
    float selectionCdf = 0.0f;         // Cumulative selection probability; last emitter = 1.
    math::Vec3 radiance;
};

struct EmitterTable {
    std::vector<EmitterCpu> emitters;
    std::vector<EmitterTriangleCpu> triangles;
    float totalPower = 0.0f;
};

// One emitter per instance whose material is an active emitter. Every active emitter receives a
// positive selection probability. Instances of off emitters are not listed (their surfaces still
// exist in the scene and reflect light).
EmitterTable BuildEmitterTable(const Scene& scene);

}  // namespace lc
