// Root signature creation and compute pipeline state from precompiled DXIL (.cso) files.
#pragma once

#include "graphics/d3d12/d3d12_common.h"

#include <filesystem>
#include <span>

namespace lc::gfx {

class Device;

ComPtr<ID3D12RootSignature> CreateRootSignature(Device& device, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc,
                                                std::wstring_view name);

class ComputePipeline {
public:
    // From a .cso file compiled by the build (signed DXIL).
    ComputePipeline(Device& device, ID3D12RootSignature* rootSignature, const std::filesystem::path& csoFile,
                    std::wstring_view name);
    // From DXIL bytecode held in memory (a library's embedded shader); `source` names it in errors.
    ComputePipeline(Device& device, ID3D12RootSignature* rootSignature, std::span<const std::uint8_t> bytecode,
                    std::wstring_view name, std::string_view source);

    ID3D12PipelineState* Get() const { return pso_.Get(); }
    const std::filesystem::path& Source() const { return source_; }
    // FNV-1a of the DXIL bytes: identifies the shader in benchmark metadata.
    std::uint64_t Hash() const { return hash_; }

private:
    void Create(Device& device, ID3D12RootSignature* rootSignature, std::span<const std::uint8_t> bytecode,
                std::wstring_view name, std::string_view source);

    ComPtr<ID3D12PipelineState> pso_;
    std::filesystem::path source_;
    std::uint64_t hash_ = 0;
};

}  // namespace lc::gfx
