// Per-frame linear allocator in an upload heap for constants, instance records, and instance
// descriptors. Reset only after the frame that used it has completed on the GPU.
#pragma once

#include "graphics/d3d12/d3d12_common.h"
#include "graphics/d3d12/gpu_buffer.h"

namespace lc::gfx {

class Device;

struct UploadAllocation {
    void* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    std::uint64_t size = 0;
};

class UploadArena {
public:
    UploadArena(Device& device, std::uint64_t capacity, std::wstring_view name);

    // Throws when the arena is full. Alignment must be a power of two.
    UploadAllocation Allocate(std::uint64_t size, std::uint64_t alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    void Reset() { offset_ = 0; }
    std::uint64_t Used() const { return offset_; }
    std::uint64_t Capacity() const { return capacity_; }

private:
    GpuBuffer buffer_;
    std::uint8_t* base_ = nullptr;
    std::uint64_t capacity_ = 0;
    std::uint64_t offset_ = 0;
};

}  // namespace lc::gfx
