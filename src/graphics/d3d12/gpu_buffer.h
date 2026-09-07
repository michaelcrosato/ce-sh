// Committed buffer resources with explicit heap type and lifetime. Default-heap buffers are created
// in the COMMON state (the runtime ignores any other initial state for buffers) and rely on the
// documented promotion/decay rules; acceleration structures are the one exception.
#pragma once

#include "graphics/d3d12/d3d12_common.h"

#include <span>

namespace lc::gfx {

class Device;

class GpuBuffer {
public:
    GpuBuffer() = default;
    ~GpuBuffer();
    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    static GpuBuffer Create(Device& device, std::wstring_view name, std::uint64_t size, D3D12_HEAP_TYPE heapType,
                            D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState);
    static GpuBuffer CreateDefault(Device& device, std::wstring_view name, std::uint64_t size,
                                   D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);
    static GpuBuffer CreateUpload(Device& device, std::wstring_view name, std::uint64_t size);
    static GpuBuffer CreateReadback(Device& device, std::wstring_view name, std::uint64_t size);
    // Result buffer for BLAS/TLAS data: default heap, UAV-capable, RAYTRACING_ACCELERATION_STRUCTURE state.
    static GpuBuffer CreateAccelerationStructure(Device& device, std::wstring_view name, std::uint64_t size);

    bool IsValid() const { return resource_ != nullptr; }
    ID3D12Resource* Get() const { return resource_.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS Address() const { return resource_ ? resource_->GetGPUVirtualAddress() : 0; }
    std::uint64_t Size() const { return size_; }

    // Upload and readback heaps only. Map keeps the mapping until Unmap or destruction.
    void* Map();
    void Unmap();

private:
    ComPtr<ID3D12Resource> resource_;
    std::uint64_t size_ = 0;
    D3D12_HEAP_TYPE heapType_ = D3D12_HEAP_TYPE_DEFAULT;
    void* mapped_ = nullptr;
};

// Records a copy of data into dst (a default-heap buffer) through a fresh upload buffer and
// transitions dst to finalState. The returned upload buffer must stay alive until the command
// list has executed.
GpuBuffer UploadToDefaultBuffer(Device& device, ID3D12GraphicsCommandList* list, GpuBuffer& dst,
                                std::span<const std::uint8_t> data, D3D12_RESOURCE_STATES finalState);

}  // namespace lc::gfx
