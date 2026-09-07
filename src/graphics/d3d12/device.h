// Adapter enumeration, feature checks, device creation, debug layer, info queue, and DRED.
#pragma once

#include "graphics/d3d12/d3d12_common.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace lc::gfx {

struct AdapterInfo {
    std::uint32_t index = 0;
    std::wstring description;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t subSysId = 0;
    std::uint32_t revision = 0;
    std::uint64_t dedicatedVideoMemory = 0;
    std::uint64_t dedicatedSystemMemory = 0;
    std::uint64_t sharedSystemMemory = 0;
    bool software = false;
    std::string driverVersion = "unknown";
    LUID luid{};
    // Probe results.
    bool supportsFeatureLevel12_1 = false;
    D3D12_RAYTRACING_TIER raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    D3D_SHADER_MODEL highestShaderModel = D3D_SHADER_MODEL_5_1;

    bool SupportsLastCircuit() const;
    std::string Summary() const;
};

struct DeviceCapabilities {
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_1;
    D3D12_RAYTRACING_TIER raytracingTier = D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
    D3D_SHADER_MODEL highestShaderModel = D3D_SHADER_MODEL_5_1;
    D3D_ROOT_SIGNATURE_VERSION rootSignatureVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    D3D12_RESOURCE_BINDING_TIER resourceBindingTier = D3D12_RESOURCE_BINDING_TIER_1;
    bool tearingSupported = false;
    bool typedUavStoreRgba8 = false;
    bool typedUavStoreRgba32Float = false;
    bool typedUavStoreRgba32Uint = false;
    // Denoiser guide formats (M4): stores by the guided trace, loads by the compose pass.
    bool typedUavStoreRgba16Float = false;
    bool typedUavStoreR32Float = false;
    bool typedUavStoreR10G10B10A2 = false;
    bool typedUavLoadRgba16Float = false;
    bool typedUavLoadRgba32Float = false;
    bool typedUavLoadR32Float = false;
    bool typedUavLoadRgba8 = false;
    bool typedUavLoadR10G10B10A2 = false;
    std::uint64_t videoMemoryBudget = 0;
    std::uint64_t videoMemoryCurrentUsage = 0;
};

struct DeviceOptions {
    bool debugLayer = false;
    bool gpuValidation = false;
    bool dred = true;
    int adapterIndex = -1;  // -1: first adapter that supports the project requirements.
};

class Device {
public:
    // Idempotent. Must run before any D3D12 device exists in the process to take effect.
    static void EnableDebugLayer(bool gpuValidation);

    // Enumerates adapters in high-performance order and probes each hardware adapter for
    // feature level 12_1, DXR tier, and shader model. Software adapters are listed but never probed.
    static std::vector<AdapterInfo> EnumerateAdapters();

    // Prints live D3D/DXGI objects to the debugger output (debug layer only). Call after all
    // graphics objects are released.
    static void ReportLiveObjects();

    // Throws lc::UnsupportedHardware when no adapter meets the requirements.
    explicit Device(const DeviceOptions& options);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    ID3D12Device5* Get() const { return device_.Get(); }
    IDXGIFactory6* Factory() const { return factory_.Get(); }
    IDXGIAdapter3* Adapter() const { return adapter_.Get(); }
    const AdapterInfo& AdapterDetails() const { return adapterInfo_; }
    // The adapter list enumerated when this device was created (avoids re-probing every adapter).
    const std::vector<AdapterInfo>& AllAdapters() const { return allAdapters_; }
    const DeviceCapabilities& Caps() const { return caps_; }
    bool DebugLayerEnabled() const { return debugLayer_; }

    std::size_t InfoQueueErrorCount() const { return infoQueueErrors_.load(); }
    std::size_t InfoQueueWarningCount() const { return infoQueueWarnings_.load(); }

    // Logs stored debug-layer messages when the callback interface is unavailable.
    void DrainInfoQueue();

    // Refreshes videoMemoryBudget / videoMemoryCurrentUsage.
    void UpdateVideoMemoryInfo();

    // Logs the removal reason and DRED breadcrumbs / page-fault data when available.
    void ReportDeviceRemoved();

    // Internal: called by the info queue callback.
    void OnDebugMessage(D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* description);

private:
    ComPtr<IDXGIFactory6> factory_;
    ComPtr<IDXGIAdapter3> adapter_;
    ComPtr<ID3D12Device5> device_;
    ComPtr<ID3D12InfoQueue> infoQueue_;
    ComPtr<ID3D12InfoQueue1> infoQueue1_;
    DWORD callbackCookie_ = 0;
    AdapterInfo adapterInfo_;
    std::vector<AdapterInfo> allAdapters_;
    DeviceCapabilities caps_;
    bool debugLayer_ = false;
    std::atomic<std::size_t> infoQueueErrors_{0};
    std::atomic<std::size_t> infoQueueWarnings_{0};
};

std::string RaytracingTierName(D3D12_RAYTRACING_TIER tier);
std::string ShaderModelName(D3D_SHADER_MODEL model);

}  // namespace lc::gfx
