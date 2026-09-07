#include "graphics/d3d12/descriptor_heap.h"

#include "core/error.h"
#include "graphics/d3d12/device.h"

#include <format>

namespace lc::gfx {

DescriptorHeap::DescriptorHeap(Device& device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t capacity, bool shaderVisible,
                               std::wstring_view name)
    : capacity_(capacity), shaderVisible_(shaderVisible) {
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = capacity;
    desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    LC_CHECK_HR(device.Get()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap_)));
    SetName(heap_.Get(), name);
    increment_ = device.Get()->GetDescriptorHandleIncrementSize(type);
    cpuStart_ = heap_->GetCPUDescriptorHandleForHeapStart();
    if (shaderVisible) {
        gpuStart_ = heap_->GetGPUDescriptorHandleForHeapStart();
    }
}

std::uint32_t DescriptorHeap::AllocateRange(std::uint32_t count) {
    if (used_ + count > capacity_) {
        throw Error(std::format("descriptor heap exhausted ({} of {} used, {} requested)", used_, capacity_, count));
    }
    const std::uint32_t first = used_;
    used_ += count;
    return first;
}

DescriptorHandle DescriptorHeap::At(std::uint32_t index) const {
    DescriptorHandle h;
    h.index = index;
    h.cpu.ptr = cpuStart_.ptr + static_cast<SIZE_T>(index) * increment_;
    if (shaderVisible_) {
        h.gpu.ptr = gpuStart_.ptr + static_cast<UINT64>(index) * increment_;
    }
    return h;
}

}  // namespace lc::gfx
