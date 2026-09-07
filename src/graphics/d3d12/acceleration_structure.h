// Bottom-level (per mesh) and top-level (per scene) acceleration structures.
#pragma once

#include "graphics/d3d12/d3d12_common.h"
#include "graphics/d3d12/gpu_buffer.h"

namespace lc::gfx {

class Device;

struct BlasGeometry {
    D3D12_GPU_VIRTUAL_ADDRESS vertexBuffer = 0;  // float3 positions.
    std::uint32_t vertexCount = 0;
    std::uint32_t vertexStride = 12;
    D3D12_GPU_VIRTUAL_ADDRESS indexBuffer = 0;   // uint32 indices.
    std::uint32_t indexCount = 0;
};

class Blas {
public:
    // Allocates result and scratch memory and records the build plus a UAV barrier. Call
    // ReleaseScratch after the build has executed. Opaque triangles, PREFER_FAST_TRACE.
    void Build(Device& device, ID3D12GraphicsCommandList4* list, const BlasGeometry& geometry, std::wstring_view name);
    void ReleaseScratch() { scratch_ = GpuBuffer{}; }

    D3D12_GPU_VIRTUAL_ADDRESS Address() const { return result_.Address(); }
    std::uint64_t ResultSize() const { return result_.Size(); }

private:
    GpuBuffer result_;
    GpuBuffer scratch_;
};

class Tlas {
public:
    // Allocates result and scratch memory for up to maxInstances instances.
    void Reserve(Device& device, std::uint32_t maxInstances, std::wstring_view name);

    // Rebuilds in place from instance descriptors already in GPU-visible memory (16-byte aligned).
    // UAV barriers before and after order the build against the previous frame's traversal and the
    // upcoming dispatch on the single queue.
    void RecordBuild(ID3D12GraphicsCommandList4* list, D3D12_GPU_VIRTUAL_ADDRESS instanceDescs, std::uint32_t instanceCount);

    D3D12_GPU_VIRTUAL_ADDRESS Address() const { return result_.Address(); }
    std::uint64_t ResultSize() const { return result_.Size(); }
    std::uint32_t MaxInstances() const { return maxInstances_; }

private:
    GpuBuffer result_;
    GpuBuffer scratch_;
    std::uint32_t maxInstances_ = 0;
};

}  // namespace lc::gfx
