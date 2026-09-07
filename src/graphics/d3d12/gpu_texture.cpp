#include "graphics/d3d12/gpu_texture.h"

#include "core/error.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/gpu_buffer.h"

#include <format>

namespace lc::gfx {

GpuTexture2D GpuTexture2D::CreateUav(Device& device, std::wstring_view name, std::uint32_t width, std::uint32_t height,
                                     DXGI_FORMAT format) {
    if (width == 0 || height == 0) {
        throw Error(std::format("cannot create zero-sized texture '{}' ({}x{})", WideToUtf8(name), width, height));
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc = {1, 0};
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    GpuTexture2D tex;
    LC_CHECK_HR(device.Get()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                      IID_PPV_ARGS(&tex.resource_)));
    SetName(tex.resource_.Get(), name);
    tex.width_ = width;
    tex.height_ = height;
    tex.format_ = format;
    tex.state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return tex;
}

void GpuTexture2D::Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES newState) {
    if (newState == state_) {
        return;
    }
    const D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(resource_.Get(), state_, newState);
    list->ResourceBarrier(1, &barrier);
    state_ = newState;
}

ReadbackPlan GpuTexture2D::PlanReadback(Device& device) const {
    const D3D12_RESOURCE_DESC desc = resource_->GetDesc();
    ReadbackPlan plan;
    UINT rows = 0;
    UINT64 rowSize = 0;
    UINT64 total = 0;
    device.Get()->GetCopyableFootprints(&desc, 0, 1, 0, &plan.footprint, &rows, &rowSize, &total);
    plan.rowCount = rows;
    plan.rowSizeBytes = rowSize;
    plan.totalBytes = total;
    return plan;
}

void GpuTexture2D::RecordCopyToReadback(ID3D12GraphicsCommandList* list, GpuBuffer& readback, const ReadbackPlan& plan) {
    if (readback.Size() < plan.totalBytes) {
        throw Error(std::format("readback buffer too small: {} < {}", readback.Size(), plan.totalBytes));
    }
    const D3D12_RESOURCE_STATES previous = state_;
    Transition(list, D3D12_RESOURCE_STATE_COPY_SOURCE);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = plan.footprint;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = resource_.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    Transition(list, previous);
}

}  // namespace lc::gfx
