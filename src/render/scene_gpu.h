// GPU residency for a Scene: one position buffer, one index buffer, one BLAS per mesh, a TLAS
// rebuilt whenever an instance transform revision changes, and per-frame tables (instances,
// materials, emitters) written into the frame's upload arena. All geometry stays resident (spec §11).
#pragma once

#include "graphics/d3d12/acceleration_structure.h"
#include "graphics/d3d12/gpu_buffer.h"
#include "graphics/d3d12/upload_arena.h"
#include "render/gpu_layouts.h"
#include "scene/emitters.h"
#include "scene/scene.h"

#include <vector>

namespace lc::gfx {
class Device;
class GraphicsQueue;
}  // namespace lc::gfx

namespace lc {

class SceneGpu {
public:
    // Uploads geometry and builds every BLAS with a documented full GPU wait (setup only).
    SceneGpu(gfx::Device& device, gfx::GraphicsQueue& queue, const Scene& scene);

    // Per frame: writes instance records, material records, and the emitter tables into the arena;
    // rebuilds the TLAS when any transform changed since the last build (or on first use).
    void UpdateFrame(const Scene& scene, gfx::UploadArena& arena, ID3D12GraphicsCommandList4* list);

    bool TlasRebuiltThisFrame() const { return rebuiltThisFrame_; }
    std::uint32_t InstanceCount() const { return instanceCount_; }
    std::uint32_t TriangleCount() const { return triangleCount_; }
    std::uint32_t EmitterCount() const { return emitterCount_; }
    // Hash of instance transform revisions, instance count, and material revision; changes when
    // the world that the tables describe changes.
    std::uint64_t SceneRevisionHash() const { return sceneRevisionHash_; }

    D3D12_GPU_VIRTUAL_ADDRESS TlasAddress() const { return tlas_.Address(); }
    D3D12_GPU_VIRTUAL_ADDRESS InstanceRecordsAddress() const { return instanceRecordsAddress_; }
    D3D12_GPU_VIRTUAL_ADDRESS MeshRecordsAddress() const { return meshRecords_.Address(); }
    D3D12_GPU_VIRTUAL_ADDRESS PositionsAddress() const { return positions_.Address(); }
    D3D12_GPU_VIRTUAL_ADDRESS IndicesAddress() const { return indices_.Address(); }
    D3D12_GPU_VIRTUAL_ADDRESS MaterialsAddress() const { return materialsAddress_; }
    D3D12_GPU_VIRTUAL_ADDRESS EmittersAddress() const { return emittersAddress_; }
    D3D12_GPU_VIRTUAL_ADDRESS EmitterTrianglesAddress() const { return emitterTrianglesAddress_; }
    std::uint64_t AccelerationStructureBytes() const;

private:
    gfx::GpuBuffer positions_;
    gfx::GpuBuffer indices_;
    gfx::GpuBuffer meshRecords_;
    gfx::GpuBuffer placeholder_;  // One zeroed record bound when a table would otherwise be empty.
    std::vector<gfx::Blas> blas_;
    std::uint32_t uploadedMeshCount_ = 0;
    gfx::Tlas tlas_;
    std::vector<std::uint32_t> lastRevisions_;
    D3D12_GPU_VIRTUAL_ADDRESS instanceRecordsAddress_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS materialsAddress_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS emittersAddress_ = 0;
    D3D12_GPU_VIRTUAL_ADDRESS emitterTrianglesAddress_ = 0;
    std::uint32_t instanceCount_ = 0;
    std::uint32_t triangleCount_ = 0;
    std::uint32_t emitterCount_ = 0;
    std::uint64_t sceneRevisionHash_ = 0;
    bool tlasValid_ = false;
    bool rebuiltThisFrame_ = false;
};

}  // namespace lc
