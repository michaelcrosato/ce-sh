#include "graphics/d3d12/compute_pipeline.h"

#include "core/error.h"
#include "core/log.h"
#include "graphics/d3d12/device.h"
#include "platform/files.h"

#include <format>

namespace lc::gfx {

ComPtr<ID3D12RootSignature> CreateRootSignature(Device& device, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc,
                                                std::wstring_view name) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    const HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, &blob, &error);
    if (FAILED(hr)) {
        const std::string message = error ? std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize()) : "";
        throw Error(std::format("root signature '{}' serialization failed: {} {}", WideToUtf8(name), HrToString(hr), message));
    }
    ComPtr<ID3D12RootSignature> rootSignature;
    LC_CHECK_HR(device.Get()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    SetName(rootSignature.Get(), name);
    return rootSignature;
}

ComputePipeline::ComputePipeline(Device& device, ID3D12RootSignature* rootSignature, const std::filesystem::path& csoFile,
                                 std::wstring_view name)
    : source_(csoFile) {
    if (!std::filesystem::exists(csoFile)) {
        throw Error(std::format("compiled shader not found: {}. The build compiles shaders into bin/shaders next to the executable; "
                                "run the CMake build (target lc_shaders) and keep the shaders directory beside LastCircuit.exe.",
                                csoFile.string()));
    }
    const std::vector<std::uint8_t> bytecode = files::ReadBinaryFile(csoFile);
    Create(device, rootSignature, bytecode, name, csoFile.string());
    log::Info("Compute pipeline '{}' created from {} ({} bytes)", WideToUtf8(name), csoFile.filename().string(), bytecode.size());
}

ComputePipeline::ComputePipeline(Device& device, ID3D12RootSignature* rootSignature, std::span<const std::uint8_t> bytecode,
                                 std::wstring_view name, std::string_view source)
    : source_(source) {
    if (bytecode.empty()) {
        throw Error(std::format("compute pipeline '{}': no DXIL bytecode for {}", WideToUtf8(name), source));
    }
    Create(device, rootSignature, bytecode, name, source);
    log::Debug("Compute pipeline '{}' created from {} ({} bytes)", WideToUtf8(name), source, bytecode.size());
}

void ComputePipeline::Create(Device& device, ID3D12RootSignature* rootSignature, std::span<const std::uint8_t> bytecode,
                             std::wstring_view name, std::string_view source) {
    hash_ = 0xCBF29CE484222325ull;
    for (const std::uint8_t b : bytecode) {
        hash_ ^= b;
        hash_ *= 0x100000001B3ull;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature;
    desc.CS.pShaderBytecode = bytecode.data();
    desc.CS.BytecodeLength = bytecode.size();
    desc.NodeMask = 0;
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    const HRESULT hr = device.Get()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso_));
    if (FAILED(hr)) {
        throw Error(std::format("CreateComputePipelineState failed for {} ({} bytes of DXIL): {}. Check the debug-layer output above; "
                                "an unsigned or mismatched DXIL blob is the usual cause.",
                                source, bytecode.size(), HrToString(hr)));
    }
    SetName(pso_.Get(), name);
}

}  // namespace lc::gfx
