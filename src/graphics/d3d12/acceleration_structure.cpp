#include "graphics/d3d12/acceleration_structure.h"

#include "core/error.h"
#include "core/log.h"
#include "graphics/d3d12/device.h"

#include <algorithm>
#include <format>

namespace lc::gfx {

void Blas::Build(Device& device, ID3D12GraphicsCommandList4* list, const BlasGeometry& g, std::wstring_view name) {
    if (g.vertexCount == 0 || g.indexCount == 0 || g.indexCount % 3 != 0) {
        throw Error(std::format("BLAS '{}': invalid geometry ({} vertices, {} indices)", WideToUtf8(name), g.vertexCount, g.indexCount));
    }
    D3D12_RAYTRACING_GEOMETRY_DESC geom{};
    geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geom.Triangles.Transform3x4 = 0;
    geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geom.Triangles.IndexCount = g.indexCount;
    geom.Triangles.VertexCount = g.vertexCount;
    geom.Triangles.IndexBuffer = g.indexBuffer;
    geom.Triangles.VertexBuffer.StartAddress = g.vertexBuffer;
    geom.Triangles.VertexBuffer.StrideInBytes = g.vertexStride;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = 1;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = &geom;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device.Get()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    if (info.ResultDataMaxSizeInBytes == 0) {
        throw Error(std::format("BLAS '{}': prebuild info reported zero size", WideToUtf8(name)));
    }

    result_ = GpuBuffer::CreateAccelerationStructure(device, name, info.ResultDataMaxSizeInBytes);
    scratch_ = GpuBuffer::Create(device, std::wstring(name) + L" Scratch", info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT,
                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.DestAccelerationStructureData = result_.Address();
    build.ScratchAccelerationStructureData = scratch_.Address();
    list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const D3D12_RESOURCE_BARRIER barrier = UavBarrier(result_.Get());
    list->ResourceBarrier(1, &barrier);

    log::Debug("BLAS '{}': {} triangles, result {} bytes, scratch {} bytes", WideToUtf8(name), g.indexCount / 3,
               info.ResultDataMaxSizeInBytes, info.ScratchDataSizeInBytes);
}

void Tlas::Reserve(Device& device, std::uint32_t maxInstances, std::wstring_view name) {
    maxInstances_ = std::max<std::uint32_t>(1, maxInstances);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = maxInstances_;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = 0;  // Size query only.

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device.Get()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    result_ = GpuBuffer::CreateAccelerationStructure(device, name, info.ResultDataMaxSizeInBytes);
    scratch_ = GpuBuffer::Create(device, std::wstring(name) + L" Scratch", std::max(info.ScratchDataSizeInBytes, info.UpdateScratchDataSizeInBytes),
                                 D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    log::Debug("TLAS '{}': reserved for {} instances, result {} bytes, scratch {} bytes", WideToUtf8(name), maxInstances_,
               info.ResultDataMaxSizeInBytes, info.ScratchDataSizeInBytes);
}

void Tlas::RecordBuild(ID3D12GraphicsCommandList4* list, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs, std::uint32_t instanceCount) {
    if (instanceCount > maxInstances_) {
        throw Error(std::format("TLAS build with {} instances exceeds the reserved {}", instanceCount, maxInstances_));
    }
    if ((instanceDescs % D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT) != 0) {
        throw Error("TLAS instance descriptors must be 16-byte aligned");
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = instanceCount;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = instanceDescs;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};
    build.Inputs = inputs;
    build.DestAccelerationStructureData = result_.Address();
    build.ScratchAccelerationStructureData = scratch_.Address();

    const D3D12_RESOURCE_BARRIER before = UavBarrier(result_.Get());
    list->ResourceBarrier(1, &before);
    list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const D3D12_RESOURCE_BARRIER after = UavBarrier(result_.Get());
    list->ResourceBarrier(1, &after);
}

}  // namespace lc::gfx
