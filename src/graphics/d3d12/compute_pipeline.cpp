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
                                csoFile.string(), bytecode.size(), HrToString(hr)));
    }
    SetName(pso_.Get(), name);
    log::Info("Compute pipeline '{}' created from {} ({} bytes)", WideToUtf8(name), csoFile.filename().string(), bytecode.size());
}

}  // namespace lc::gfx
