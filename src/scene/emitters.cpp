#include "scene/emitters.h"

#include "core/error.h"

#include <format>

namespace lc {

EmitterTable BuildEmitterTable(const Scene& scene) {
    EmitterTable table;
    const std::vector<Instance>& instances = scene.Instances();
    for (std::uint32_t i = 0; i < instances.size(); ++i) {
        const Instance& inst = instances[i];
        const Material& material = scene.Materials()[inst.materialIndex];
        if (!material.IsActiveEmitter()) {
            continue;
        }
        const MeshData& mesh = scene.Meshes()[inst.mesh.value].data;
        EmitterCpu e;
        e.instanceIndex = i;
        e.firstTriangle = static_cast<std::uint32_t>(table.triangles.size());
        e.triangleCount = mesh.TriangleCount();
        e.materialIndex = inst.materialIndex;
        e.radiance = material.radiance;
        for (std::uint32_t t = 0; t < mesh.TriangleCount(); ++t) {
            const math::Vec3 p0 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[t * 3 + 0]]);
            const math::Vec3 p1 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[t * 3 + 1]]);
            const math::Vec3 p2 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[t * 3 + 2]]);
            EmitterTriangleCpu tri;
            tri.instanceIndex = i;
            tri.primitiveIndex = t;
            tri.area = 0.5f * math::Length(math::Cross(p1 - p0, p2 - p0));
            e.area += tri.area;
            table.triangles.push_back(tri);
        }
        if (e.area <= 0.0f) {
            throw Error(std::format("emitter instance '{}' has zero world-space area", inst.name));
        }
        float running = 0.0f;
        for (std::uint32_t t = 0; t < e.triangleCount; ++t) {
            running += table.triangles[e.firstTriangle + t].area / e.area;
            table.triangles[e.firstTriangle + t].cdf = running;
        }
        table.triangles[e.firstTriangle + e.triangleCount - 1].cdf = 1.0f;  // Absorb rounding.
        e.power = Luminance(material.radiance) * e.area;
        table.totalPower += e.power;
        table.emitters.push_back(e);
    }

    float running = 0.0f;
    for (EmitterCpu& e : table.emitters) {
        e.selectionPdf = table.totalPower > 0.0f ? e.power / table.totalPower : 0.0f;
        running += e.selectionPdf;
        e.selectionCdf = running;
    }
    if (!table.emitters.empty()) {
        table.emitters.back().selectionCdf = 1.0f;
    }
    return table;
}

}  // namespace lc
