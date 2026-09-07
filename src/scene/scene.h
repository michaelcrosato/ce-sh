// Scene registry: meshes, materials, and placed instances with stable identifiers and transform
// history. The game layer edits this; the renderer reads a snapshot of it. No D3D12 types here.
#pragma once

#include "core/ids.h"
#include "core/math/mat.h"
#include "scene/material.h"
#include "scene/mesh.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lc {

struct Mesh {
    MeshId id;  // Equals the index in Scene::Meshes(); meshes are never removed.
    MeshData data;
};

struct Instance {
    InstanceId id;  // Stable id, starts at 1, never reused. Also the TLAS InstanceID (24-bit).
    std::string name;
    MeshId mesh;
    std::uint32_t materialIndex = 0;
    math::Mat4 objectToWorld = math::Mat4::Identity();
    math::Mat4 prevObjectToWorld = math::Mat4::Identity();  // Transform used for the previous rendered image.
    std::uint32_t transformRevision = 0;                    // Incremented by every SetTransform.
};

inline constexpr std::uint32_t kMaxInstanceId = (1u << 24) - 1;  // D3D12 InstanceID width.

class Scene {
public:
    // Material 0 is a default grey diffuse so instances without an explicit material are valid.
    Scene();

    // Validates the mesh (throws lc::Error listing every problem) and returns its id.
    MeshId AddMesh(MeshData data);

    // Validates the material (throws lc::Error) and returns its index.
    std::uint32_t AddMaterial(Material material);

    // Rejects transforms with a non-positive determinant (mirroring) or non-uniform scale until
    // their handling is tested (spec §11).
    InstanceId AddInstance(std::string name, MeshId mesh, const math::Mat4& objectToWorld,
                           std::uint32_t materialIndex = 0);

    // Changes the current transform and bumps the revision. prevObjectToWorld is untouched until
    // CommitRenderedFrame so motion vectors always refer to the last rendered image.
    void SetTransform(InstanceId id, const math::Mat4& objectToWorld);

    // Switches an emitter material on or off (a circuit change). Bumps the material revision.
    void SetEmitterOn(std::uint32_t materialIndex, bool on);

    // Call once after an image has been rendered: previous transforms become the current ones.
    void CommitRenderedFrame();

    const std::vector<Mesh>& Meshes() const { return meshes_; }
    const std::vector<Material>& Materials() const { return materials_; }
    const std::vector<Instance>& Instances() const { return instances_; }
    const Instance* FindInstance(InstanceId id) const;
    std::uint32_t MeshIndex(MeshId id) const { return id.value; }
    std::uint32_t TotalTriangles() const;
    std::uint32_t MaterialRevision() const { return materialRevision_; }

private:
    Instance* FindInstanceMutable(InstanceId id);

    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    std::vector<Instance> instances_;
    std::unordered_map<std::uint32_t, std::uint32_t> instanceIndexById_;
    std::uint32_t nextInstanceId_ = 1;
    std::uint32_t materialRevision_ = 0;
};

}  // namespace lc
