// Test-only CPU ray caster (Möller–Trumbore over every instance). Used to check that hand-written
// hit expectations are self-consistent before the GPU is asked to reproduce them.
#pragma once

#include "scene/scene.h"

#include <cstdint>
#include <limits>

namespace lc::test {

struct CpuHit {
    bool hit = false;
    float t = std::numeric_limits<float>::infinity();
    std::uint32_t stableId = 0;
    std::uint32_t primitiveIndex = 0;
    bool frontFace = false;
};

inline bool IntersectTriangle(math::Vec3 origin, math::Vec3 dir, math::Vec3 p0, math::Vec3 p1, math::Vec3 p2,
                              float tMin, float tMax, float& tOut) {
    const math::Vec3 e1 = p1 - p0;
    const math::Vec3 e2 = p2 - p0;
    const math::Vec3 p = math::Cross(dir, e2);
    const float det = math::Dot(e1, p);
    if (det > -1e-12f && det < 1e-12f) return false;
    const float invDet = 1.0f / det;
    const math::Vec3 s = origin - p0;
    const float u = math::Dot(s, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    const math::Vec3 q = math::Cross(s, e1);
    const float v = math::Dot(dir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = math::Dot(e2, q) * invDet;
    if (t < tMin || t > tMax) return false;
    tOut = t;
    return true;
}

inline CpuHit RaycastScene(const Scene& scene, math::Vec3 origin, math::Vec3 dir, float tMin, float tMax) {
    CpuHit best;
    for (const Instance& inst : scene.Instances()) {
        const MeshData& mesh = scene.Meshes()[inst.mesh.value].data;
        for (std::uint32_t tri = 0; tri < mesh.TriangleCount(); ++tri) {
            const math::Vec3 p0 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[tri * 3 + 0]]);
            const math::Vec3 p1 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[tri * 3 + 1]]);
            const math::Vec3 p2 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[tri * 3 + 2]]);
            float t = 0.0f;
            if (IntersectTriangle(origin, dir, p0, p1, p2, tMin, tMax, t) && t < best.t) {
                best.hit = true;
                best.t = t;
                best.stableId = inst.id.value;
                best.primitiveIndex = tri;
                best.frontFace = math::Dot(math::Cross(p1 - p0, p2 - p0), dir) < 0.0f;
            }
        }
    }
    return best;
}

}  // namespace lc::test
