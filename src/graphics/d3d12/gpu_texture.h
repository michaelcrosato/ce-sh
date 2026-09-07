// 2D textures written by compute (UAV) and read back for captures. Tracks its own resource state.
#pragma once

#include "graphics/d3d12/d3d12_common.h"

namespace lc::gfx {

class Device;
class GpuBuffer;

struct ReadbackPlan {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    std::uint32_t rowCount = 0;
    std::uint64_t rowSizeBytes = 0;   // Bytes of real data per row (footprint.RowPitch is padded to 256).
    std::uint64_t totalBytes = 0;
};

class GpuTexture2D {
public:
    GpuTexture2D() = default;

    // Created in the UNORDERED_ACCESS state with the ALLOW_UNORDERED_ACCESS flag.
    static GpuTexture2D CreateUav(Device& device, std::wstring_view name, std::uint32_t width, std::uint32_t height,
                                  DXGI_FORMAT format);

    bool IsValid() const { return resource_ != nullptr; }
    ID3D12Resource* Get() const { return resource_.Get(); }
    std::uint32_t Width() const { return width_; }
    std::uint32_t Height() const { return height_; }
    DXGI_FORMAT Format() const { return format_; }
    D3D12_RESOURCE_STATES State() const { return state_; }

    // Records a transition barrier when the state changes.
    void Transition(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES newState);

    ReadbackPlan PlanReadback(Device& device) const;

    // Copies the whole texture into a readback buffer laid out per plan, restoring the prior state.
    void RecordCopyToReadback(ID3D12GraphicsCommandList* list, GpuBuffer& readback, const ReadbackPlan& plan);

private:
    ComPtr<ID3D12Resource> resource_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state_ = D3D12_RESOURCE_STATE_COMMON;
};

}  // namespace lc::gfx
