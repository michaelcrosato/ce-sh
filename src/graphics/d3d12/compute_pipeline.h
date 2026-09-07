// Root signature creation and compute pipeline state from precompiled DXIL (.cso) files.
#pragma once

#include "graphics/d3d12/d3d12_common.h"

#include <filesystem>

namespace lc::gfx {

class Device;

ComPtr<ID3D12RootSignature> CreateRootSignature(Device& device, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc,
                                                std::wstring_view name);

class ComputePipeline {
public:
    ComputePipeline(Device& device, ID3D12RootSignature* rootSignature, const std::filesystem::path& csoFile,
                    std::wstring_view name);

    ID3D12PipelineState* Get() const { return pso_.Get(); }
    const std::filesystem::path& Source() const { return source_; }

private:
    ComPtr<ID3D12PipelineState> pso_;
    std::filesystem::path source_;
};

}  // namespace lc::gfx
