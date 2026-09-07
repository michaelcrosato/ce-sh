#include "scene/scene.h"

#include "core/error.h"

#include <algorithm>
#include <format>

namespace lc {

Scene::Scene() {
    Material grey;
    grey.name = "default_grey";
    grey.type = MaterialType::Diffuse;
    grey.reflectance = {0.5f, 0.5f, 0.5f};
    materials_.push_back(std::move(grey));
}

MeshId Scene::AddMesh(MeshData data) {
    const std::vector<std::string> problems = ValidateMesh(data);
    if (!problems.empty()) {
        std::string message = std::format("mesh '{}' rejected:", data.name);
        for (const std::string& p : problems) {
            message += "\n  - " + p;
        }
        throw Error(message);
    }
    const MeshId id{static_cast<std::uint32_t>(meshes_.size())};
    meshes_.push_back(Mesh{id, std::move(data)});
    return id;
}

std::uint32_t Scene::AddMaterial(Material material) {
    const std::vector<std::string> problems = ValidateMaterial(material);
    if (!problems.empty()) {
        std::string message = std::format("material '{}' rejected:", material.name);
        for (const std::string& p : problems) {
            message += "\n  - " + p;
        }
        throw Error(message);
    }
    materials_.push_back(std::move(material));
    ++materialRevision_;
    return static_cast<std::uint32_t>(materials_.size() - 1);
}

InstanceId Scene::AddInstance(std::string name, MeshId mesh, const math::Mat4& objectToWorld,
                              std::uint32_t materialIndex) {
    if (!mesh.IsValid() || mesh.value >= meshes_.size()) {
        throw Error(std::format("instance '{}' references unknown mesh id {}", name, mesh.value));
    }
    if (materialIndex >= materials_.size()) {
        throw Error(std::format("instance '{}' references unknown material index {} ({} materials exist)", name, materialIndex,
                                materials_.size()));
    }
    if (nextInstanceId_ > kMaxInstanceId) {
        throw Error(std::format("instance id space exhausted (limit {})", kMaxInstanceId));
    }
    {
        const math::Vec3 cx{objectToWorld.m[0][0], objectToWorld.m[1][0], objectToWorld.m[2][0]};
        const math::Vec3 cy{objectToWorld.m[0][1], objectToWorld.m[1][1], objectToWorld.m[2][1]};
        const math::Vec3 cz{objectToWorld.m[0][2], objectToWorld.m[1][2], objectToWorld.m[2][2]};
        const float det = math::Dot(cx, math::Cross(cy, cz));
        if (!(det > 1e-12f)) {
            throw Error(std::format("instance '{}' has a mirrored or degenerate transform (determinant {}); negative scale is not supported",
                                    name, det));
        }
        const float lx = math::Length(cx);
        const float ly = math::Length(cy);
        const float lz = math::Length(cz);
        const float lo = std::min(lx, std::min(ly, lz));
        const float hi = std::max(lx, std::max(ly, lz));
        if (hi - lo > 1e-4f * hi) {
            throw Error(std::format("instance '{}' has non-uniform scale ({}, {}, {}); not supported until tested", name, lx, ly, lz));
        }
    }
    Instance inst;
    inst.id = InstanceId{nextInstanceId_++};
    inst.name = std::move(name);
    inst.mesh = mesh;
    inst.materialIndex = materialIndex;
    inst.objectToWorld = objectToWorld;
    inst.prevObjectToWorld = objectToWorld;
    inst.transformRevision = 0;
    instanceIndexById_[inst.id.value] = static_cast<std::uint32_t>(instances_.size());
    instances_.push_back(std::move(inst));
    return instances_.back().id;
}

void Scene::SetTransform(InstanceId id, const math::Mat4& objectToWorld) {
    Instance* inst = FindInstanceMutable(id);
    if (inst == nullptr) {
        throw Error(std::format("SetTransform: unknown instance id {}", id.value));
    }
    inst->objectToWorld = objectToWorld;
    ++inst->transformRevision;
}

void Scene::SetEmitterOn(std::uint32_t materialIndex, bool on) {
    if (materialIndex >= materials_.size()) {
        throw Error(std::format("SetEmitterOn: unknown material index {}", materialIndex));
    }
    Material& m = materials_[materialIndex];
    if (m.type != MaterialType::Emitter) {
        throw Error(std::format("SetEmitterOn: material '{}' is not an emitter", m.name));
    }
    if (m.emitterOn != on) {
        m.emitterOn = on;
        ++materialRevision_;
    }
}

void Scene::CommitRenderedFrame() {
    for (Instance& inst : instances_) {
        inst.prevObjectToWorld = inst.objectToWorld;
    }
}

const Instance* Scene::FindInstance(InstanceId id) const {
    const auto it = instanceIndexById_.find(id.value);
    return it == instanceIndexById_.end() ? nullptr : &instances_[it->second];
}

Instance* Scene::FindInstanceMutable(InstanceId id) {
    const auto it = instanceIndexById_.find(id.value);
    return it == instanceIndexById_.end() ? nullptr : &instances_[it->second];
}

std::uint32_t Scene::TotalTriangles() const {
    std::uint32_t total = 0;
    for (const Instance& inst : instances_) {
        total += meshes_[inst.mesh.value].data.TriangleCount();
    }
    return total;
}

}  // namespace lc
