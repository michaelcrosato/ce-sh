#include "render/scene_gpu.h"

#include "core/error.h"
#include "core/log.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/graphics_queue.h"

#include <cstring>
#include <format>

namespace lc {

namespace {

template <class T>
std::span<const std::uint8_t> AsBytes(const std::vector<T>& v) {
    return {reinterpret_cast<const std::uint8_t*>(v.data()), v.size() * sizeof(T)};
}

}  // namespace

SceneGpu::SceneGpu(gfx::Device& device, gfx::GraphicsQueue& queue, const Scene& scene) {
    if (scene.Meshes().empty()) {
        throw Error("SceneGpu: the scene has no meshes");
    }

    // Concatenate every mesh into one position buffer and one index buffer.
    std::vector<math::Vec3> positions;
    std::vector<std::uint32_t> indices;
    std::vector<gpu::MeshRecord> records;
    for (const Mesh& mesh : scene.Meshes()) {
        gpu::MeshRecord r;
        r.firstVertex = static_cast<std::uint32_t>(positions.size());
        r.firstIndex = static_cast<std::uint32_t>(indices.size());
        r.vertexCount = static_cast<std::uint32_t>(mesh.data.positions.size());
        r.indexCount = static_cast<std::uint32_t>(mesh.data.indices.size());
        records.push_back(r);
        positions.insert(positions.end(), mesh.data.positions.begin(), mesh.data.positions.end());
        indices.insert(indices.end(), mesh.data.indices.begin(), mesh.data.indices.end());
    }
    static_assert(sizeof(math::Vec3) == 12, "positions must be tightly packed float3");

    positions_ = gfx::GpuBuffer::CreateDefault(device, L"Scene Positions", positions.size() * sizeof(math::Vec3));
    indices_ = gfx::GpuBuffer::CreateDefault(device, L"Scene Indices", indices.size() * sizeof(std::uint32_t));
    meshRecords_ = gfx::GpuBuffer::CreateDefault(device, L"Scene Mesh Records", records.size() * sizeof(gpu::MeshRecord));

    gfx::ComPtr<ID3D12CommandAllocator> allocator;
    LC_CHECK_HR(device.Get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    gfx::SetName(allocator.Get(), L"Scene Setup Allocator");
    gfx::ComPtr<ID3D12GraphicsCommandList4> list;
    LC_CHECK_HR(device.Get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)));
    gfx::SetName(list.Get(), L"Scene Setup List");

    std::vector<gfx::GpuBuffer> staging;
    staging.push_back(gfx::UploadToDefaultBuffer(device, list.Get(), positions_, AsBytes(positions), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    staging.push_back(gfx::UploadToDefaultBuffer(device, list.Get(), indices_, AsBytes(indices), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
    staging.push_back(gfx::UploadToDefaultBuffer(device, list.Get(), meshRecords_, AsBytes(records), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

    blas_.resize(scene.Meshes().size());
    uploadedMeshCount_ = static_cast<std::uint32_t>(scene.Meshes().size());
    for (std::size_t i = 0; i < scene.Meshes().size(); ++i) {
        gfx::BlasGeometry g;
        g.vertexBuffer = positions_.Address() + static_cast<std::uint64_t>(records[i].firstVertex) * sizeof(math::Vec3);
        g.vertexCount = records[i].vertexCount;
        g.vertexStride = sizeof(math::Vec3);
        g.indexBuffer = indices_.Address() + static_cast<std::uint64_t>(records[i].firstIndex) * sizeof(std::uint32_t);
        g.indexCount = records[i].indexCount;
        blas_[i].Build(device, list.Get(), g, L"BLAS " + Utf8ToWide(scene.Meshes()[i].data.name));
    }
    tlas_.Reserve(device, static_cast<std::uint32_t>(scene.Instances().size()), L"Scene TLAS");

    LC_CHECK_HR(list->Close());
    queue.Execute(list.Get());
    queue.WaitIdle();  // Setup-time full wait (spec §10 permits documented setup waits).
    for (gfx::Blas& b : blas_) {
        b.ReleaseScratch();
    }
    triangleCount_ = scene.TotalTriangles();
    log::Info("Scene uploaded: {} meshes, {} vertices, {} triangles (unique), {} instances, {} triangle instances, AS {} KiB",
              scene.Meshes().size(), positions.size(), indices.size() / 3, scene.Instances().size(), triangleCount_,
              AccelerationStructureBytes() / 1024);
}

std::uint64_t SceneGpu::AccelerationStructureBytes() const {
    std::uint64_t total = tlas_.ResultSize();
    for (const gfx::Blas& b : blas_) {
        total += b.ResultSize();
    }
    return total;
}

void SceneGpu::UpdateInstances(const Scene& scene, gfx::UploadArena& arena, ID3D12GraphicsCommandList4* list) {
    const std::vector<Instance>& instances = scene.Instances();
    const auto count = static_cast<std::uint32_t>(instances.size());
    if (count == 0) {
        throw Error("SceneGpu: the scene has no instances");
    }
    if (count > tlas_.MaxInstances()) {
        throw Error(std::format("SceneGpu: {} instances exceed the reserved TLAS capacity of {}; re-upload the scene (SetScene) "
                                "after adding many instances",
                                count, tlas_.MaxInstances()));
    }
    if (scene.Meshes().size() != uploadedMeshCount_) {
        throw Error(std::format("SceneGpu: the scene now has {} meshes but {} were uploaded; call SetScene again", scene.Meshes().size(),
                                uploadedMeshCount_));
    }

    bool dirty = !tlasValid_ || lastRevisions_.size() != instances.size();
    for (std::size_t i = 0; !dirty && i < instances.size(); ++i) {
        dirty = instances[i].transformRevision != lastRevisions_[i];
    }

    // Instance records: rewritten every frame (previous transforms change after every commit).
    const gfx::UploadAllocation recordAlloc = arena.Allocate(count * sizeof(gpu::InstanceRecord));
    auto* records = static_cast<gpu::InstanceRecord*>(recordAlloc.cpu);
    for (std::uint32_t i = 0; i < count; ++i) {
        const Instance& inst = instances[i];
        gpu::InstanceRecord& r = records[i];
        for (int row = 0; row < 3; ++row) {
            r.objectToWorldRow[row] = {inst.objectToWorld.m[row][0], inst.objectToWorld.m[row][1], inst.objectToWorld.m[row][2], inst.objectToWorld.m[row][3]};
            r.prevObjectToWorldRow[row] = {inst.prevObjectToWorld.m[row][0], inst.prevObjectToWorld.m[row][1], inst.prevObjectToWorld.m[row][2], inst.prevObjectToWorld.m[row][3]};
        }
        r.meshIndex = scene.MeshIndex(inst.mesh);
        r.materialIndex = inst.materialIndex;
        r.stableId = inst.id.value;
        r.transformRevision = inst.transformRevision;
    }
    instanceRecordsAddress_ = recordAlloc.gpu;
    instanceCount_ = count;

    rebuiltThisFrame_ = false;
    if (dirty) {
        const gfx::UploadAllocation descAlloc = arena.Allocate(count * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), 256);
        auto* descs = static_cast<D3D12_RAYTRACING_INSTANCE_DESC*>(descAlloc.cpu);
        for (std::uint32_t i = 0; i < count; ++i) {
            const Instance& inst = instances[i];
            D3D12_RAYTRACING_INSTANCE_DESC& d = descs[i];
            std::memset(&d, 0, sizeof(d));
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 4; ++col) {
                    d.Transform[row][col] = inst.objectToWorld.m[row][col];
                }
            }
            d.InstanceID = inst.id.value & 0xFFFFFFu;
            d.InstanceMask = 0xFF;
            d.InstanceContributionToHitGroupIndex = 0;
            // DXR's default facing rule is numeric: a triangle is front-facing when
            // dot(cross(p1 - p0, p2 - p0), rayDirection) < 0. In this right-handed, +Y-up world that
            // is exactly "counter-clockwise seen from outside", so no winding flag is needed. Setting
            // TRIANGLE_FRONT_COUNTERCLOCKWISE inverted every hit (caught by the rt_boxes facing test).
            d.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            d.AccelerationStructure = blas_[scene.MeshIndex(inst.mesh)].Address();
        }
        tlas_.RecordBuild(list, descAlloc.gpu, count);
        lastRevisions_.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            lastRevisions_[i] = instances[i].transformRevision;
        }
        tlasValid_ = true;
        rebuiltThisFrame_ = true;
    }
}

}  // namespace lc
