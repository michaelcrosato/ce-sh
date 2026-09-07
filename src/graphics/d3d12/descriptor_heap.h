// Bump-allocated descriptor heap. Descriptors are allocated once at setup and rewritten in place
// (for example after a resize); nothing is freed individually in this milestone.
#pragma once

#include "graphics/d3d12/d3d12_common.h"

namespace lc::gfx {

class Device;

struct DescriptorHandle {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    std::uint32_t index = 0;
};

class DescriptorHeap {
public:
    DescriptorHeap(Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity, bool shaderVisible,
                   std::wstring_view name);

    ID3D12DescriptorHeap* Get() const { return heap_.Get(); }

    // Returns the index of the first descriptor of a contiguous range. Throws when exhausted.
    std::uint32_t AllocateRange(std::uint32_t count);
    DescriptorHandle Allocate() { return At(AllocateRange(1)); }
    DescriptorHandle At(std::uint32_t index) const;

private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    std::uint32_t capacity_ = 0;
    std::uint32_t used_ = 0;
    std::uint32_t increment_ = 0;
    bool shaderVisible_ = false;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart_{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart_{};
};

}  // namespace lc::gfx
