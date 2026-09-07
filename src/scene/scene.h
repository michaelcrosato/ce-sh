// Scene registry: meshes and placed instances with stable identifiers and transform history.
// The game layer edits this; the renderer reads a snapshot of it. No D3D12 types here.
#pragma once

#include "core/ids.h"
#include "core/math/mat.h"
#include "scene/mesh.h"

#include <cstdint>
#include <string>
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
    // Validates the mesh (throws lc::Error listing every problem) and returns its id.
    MeshId AddMesh(MeshData data);

    InstanceId AddInstance(std::string name, MeshId mesh, const math::Mat4& objectToWorld,
                           std::uint32_t materialIndex = 0);

    // Changes the current transform and bumps the revision. prevObjectToWorld is untouched until
    // CommitRenderedFrame so motion vectors always refer to the last rendered image.
    void SetTransform(InstanceId id, const math::Mat4& objectToWorld);

    // Call once after an image has been rendered: previous transforms become the current ones.
    void CommitRenderedFrame();

    const std::vector<Mesh>& Meshes() const { return meshes_; }
    const std::vector<Instance>& Instances() const { return instances_; }
    const Instance* FindInstance(InstanceId id) const;
    std::uint32_t MeshIndex(MeshId id) const { return id.value; }
    std::uint32_t TotalTriangles() const;

private:
    Instance* FindInstanceMutable(InstanceId id);

    std::vector<Mesh> meshes_;
    std::vector<Instance> instances_;
    std::uint32_t nextInstanceId_ = 1;
};

}  // namespace lc
