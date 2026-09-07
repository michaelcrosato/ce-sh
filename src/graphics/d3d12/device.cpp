#include "graphics/d3d12/device.h"

#include "core/error.h"
#include "core/log.h"

#include <dxgidebug.h>

#include <format>

namespace lc::gfx {

namespace {

bool g_debugLayerEnabled = false;

std::string DriverVersionString(IDXGIAdapter1* adapter) {
    LARGE_INTEGER version{};
    if (FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
        return "unknown";
    }
    return std::format("{}.{}.{}.{}", HIWORD(version.HighPart), LOWORD(version.HighPart), HIWORD(version.LowPart),
                       LOWORD(version.LowPart));
}

D3D_SHADER_MODEL HighestShaderModel(ID3D12Device* device) {
    static constexpr D3D_SHADER_MODEL kCandidates[] = {
        D3D_SHADER_MODEL_6_9, D3D_SHADER_MODEL_6_8, D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5,
        D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0,
    };
    for (const D3D_SHADER_MODEL candidate : kCandidates) {
        D3D12_FEATURE_DATA_SHADER_MODEL data{candidate};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data)))) {
            return data.HighestShaderModel;
        }
    }
    return D3D_SHADER_MODEL_5_1;
}

AdapterInfo DescribeAdapter(std::uint32_t index, IDXGIAdapter1* adapter) {
    DXGI_ADAPTER_DESC1 desc{};
    LC_CHECK_HR(adapter->GetDesc1(&desc));
    AdapterInfo info;
    info.index = index;
    info.description = desc.Description;
    info.vendorId = desc.VendorId;
    info.deviceId = desc.DeviceId;
    info.subSysId = desc.SubSysId;
    info.revision = desc.Revision;
    info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
    info.dedicatedSystemMemory = desc.DedicatedSystemMemory;
    info.sharedSystemMemory = desc.SharedSystemMemory;
    info.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    info.luid = desc.AdapterLuid;
    info.driverVersion = DriverVersionString(adapter);
    return info;
}

void ProbeAdapter(IDXGIAdapter1* adapter, AdapterInfo& info) {
    if (info.software) {
        return;  // Never probe (or use) software adapters.
    }
    ComPtr<ID3D12Device5> device;
    if (FAILED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)))) {
        return;
    }
    info.supportsFeatureLevel12_1 = true;
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
        info.raytracingTier = options5.RaytracingTier;
    }
    info.highestShaderModel = HighestShaderModel(device.Get());
}

bool TypedUavStoreSupported(ID3D12Device* device, DXGI_FORMAT format) {
    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) {
        return false;
    }
    return (support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0 &&
           (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
}

void __stdcall InfoQueueCallback(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id,
                                 LPCSTR description, void* context) {
    static_cast<Device*>(context)->OnDebugMessage(severity, id, description);
}

const char* BreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op) {
    switch (op) {
        case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
        case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DrawInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DrawIndexedInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return "ExecuteIndirect";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "Dispatch";
        case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "CopyBufferRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "CopyTextureRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "CopyResource";
        case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "ResourceBarrier";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return "ResolveQueryData";
        case D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE: return "BuildRaytracingAccelerationStructure";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS: return "DispatchRays";
        default: return "other";
    }
}

}  // namespace

bool AdapterInfo::SupportsLastCircuit() const {
    return !software && supportsFeatureLevel12_1 && raytracingTier >= D3D12_RAYTRACING_TIER_1_1 &&
           highestShaderModel >= D3D_SHADER_MODEL_6_5;
}

std::string AdapterInfo::Summary() const {
    return std::format("[{}] {} | vendor 0x{:04X} device 0x{:04X} rev {} | driver {} | VRAM {} MiB | {} | FL12_1 {} | DXR {} | {}",
                       index, WideToUtf8(description), vendorId, deviceId, revision, driverVersion,
                       dedicatedVideoMemory / (1024 * 1024), software ? "SOFTWARE" : "hardware",
                       supportsFeatureLevel12_1 ? "yes" : "no", RaytracingTierName(raytracingTier),
                       ShaderModelName(highestShaderModel));
}

std::string RaytracingTierName(D3D12_RAYTRACING_TIER tier) {
    if (tier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
        return "not supported";
    }
    // Enumerators encode the tier as major * 10 + minor (10 = 1.0, 11 = 1.1, 12 = 1.2, ...).
    const auto value = static_cast<int>(tier);
    return std::format("Tier {}.{}", value / 10, value % 10);
}

std::string ShaderModelName(D3D_SHADER_MODEL model) {
    const auto value = static_cast<std::uint32_t>(model);
    return std::format("SM {}.{}", value >> 4, value & 0xF);
}

void Device::EnableDebugLayer(bool gpuValidation) {
    if (g_debugLayerEnabled) {
        return;
    }
    ComPtr<ID3D12Debug> debug;
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        log::Warn("D3D12 debug layer unavailable (install the 'Graphics Tools' optional Windows feature)");
        return;
    }
    debug->EnableDebugLayer();
    g_debugLayerEnabled = true;
    if (gpuValidation) {
        ComPtr<ID3D12Debug1> debug1;
        if (SUCCEEDED(debug.As(&debug1))) {
            debug1->SetEnableGPUBasedValidation(TRUE);
            log::Info("D3D12 GPU-based validation enabled");
        } else {
            log::Warn("GPU-based validation unavailable on this runtime");
        }
    }
    log::Info("D3D12 debug layer enabled");
}

std::vector<AdapterInfo> Device::EnumerateAdapters() {
    ComPtr<IDXGIFactory6> factory;
    LC_CHECK_HR(CreateDXGIFactory2(g_debugLayerEnabled ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory)));
    std::vector<AdapterInfo> adapters;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hr = factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        LC_CHECK_HR(hr);
        AdapterInfo info = DescribeAdapter(i, adapter.Get());
        ProbeAdapter(adapter.Get(), info);
        adapters.push_back(std::move(info));
    }
    return adapters;
}

void Device::ReportLiveObjects() {
    if (!g_debugLayerEnabled) {
        return;
    }
    ComPtr<IDXGIDebug1> dxgiDebug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    }
}

Device::Device(const DeviceOptions& options) {
    if (options.debugLayer) {
        EnableDebugLayer(options.gpuValidation);
    }
    debugLayer_ = g_debugLayerEnabled;

    if (options.dred) {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            log::Info("DRED auto-breadcrumbs and page-fault reporting enabled");
        } else {
            log::Warn("DRED settings interface unavailable; device-removal diagnostics will be limited");
        }
    }

    LC_CHECK_HR(CreateDXGIFactory2(debugLayer_ ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory_)));

    const std::vector<AdapterInfo> adapters = EnumerateAdapters();
    int chosen = -1;
    if (options.adapterIndex >= 0) {
        if (static_cast<std::size_t>(options.adapterIndex) >= adapters.size()) {
            throw UsageError(std::format("--adapter {} is out of range; {} adapter(s) found", options.adapterIndex, adapters.size()));
        }
        const AdapterInfo& a = adapters[static_cast<std::size_t>(options.adapterIndex)];
        if (!a.SupportsLastCircuit()) {
            throw UnsupportedHardware(std::format(
                "Adapter {} does not meet the requirements (hardware adapter, feature level 12_1, DXR Tier 1.1, Shader Model 6.5): {}",
                options.adapterIndex, a.Summary()));
        }
        chosen = options.adapterIndex;
    } else {
        for (const AdapterInfo& a : adapters) {
            if (a.SupportsLastCircuit()) {
                chosen = static_cast<int>(a.index);
                break;
            }
        }
        if (chosen < 0) {
            std::string list;
            for (const AdapterInfo& a : adapters) {
                list += "\n  " + a.Summary();
            }
            throw UnsupportedHardware(
                "No graphics adapter supports the requirements: hardware adapter with Direct3D feature level 12_1, "
                "DXR Tier 1.1 (inline ray queries), and Shader Model 6.5. Last Circuit has no raster fallback. "
                "Adapters found:" + list);
        }
    }

    ComPtr<IDXGIAdapter1> adapter1;
    LC_CHECK_HR(factory_->EnumAdapterByGpuPreference(static_cast<UINT>(chosen), DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter1)));
    LC_CHECK_HR(adapter1.As(&adapter_));
    adapterInfo_ = adapters[static_cast<std::size_t>(chosen)];

    LC_CHECK_HR(D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device_)));
    SetName(device_.Get(), L"LastCircuit Device");

    // Capabilities.
    D3D12_FEATURE_DATA_D3D12_OPTIONS options0{};
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options0, sizeof(options0)))) {
        caps_.resourceBindingTier = options0.ResourceBindingTier;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    LC_CHECK_HR(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));
    caps_.raytracingTier = options5.RaytracingTier;
    caps_.highestShaderModel = HighestShaderModel(device_.Get());
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSig{D3D_ROOT_SIGNATURE_VERSION_1_1};
    if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSig, sizeof(rootSig)))) {
        caps_.rootSignatureVersion = rootSig.HighestVersion;
    }
    caps_.typedUavStoreRgba8 = TypedUavStoreSupported(device_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
    caps_.typedUavStoreRgba32Float = TypedUavStoreSupported(device_.Get(), DXGI_FORMAT_R32G32B32A32_FLOAT);
    caps_.typedUavStoreRgba32Uint = TypedUavStoreSupported(device_.Get(), DXGI_FORMAT_R32G32B32A32_UINT);
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory_.As(&factory5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) {
            caps_.tearingSupported = allow != FALSE;
        }
    }
    UpdateVideoMemoryInfo();

    if (caps_.raytracingTier < D3D12_RAYTRACING_TIER_1_1) {
        throw UnsupportedHardware(std::format("Selected adapter reports {} after device creation; DXR Tier 1.1 is required",
                                              RaytracingTierName(caps_.raytracingTier)));
    }
    if (caps_.highestShaderModel < D3D_SHADER_MODEL_6_5) {
        throw UnsupportedHardware(std::format("Selected adapter supports only {}; Shader Model 6.5 is required for inline ray queries",
                                              ShaderModelName(caps_.highestShaderModel)));
    }
    if (caps_.rootSignatureVersion < D3D_ROOT_SIGNATURE_VERSION_1_1) {
        throw UnsupportedHardware("Root signature version 1.1 is required");
    }
    if (!caps_.typedUavStoreRgba8 || !caps_.typedUavStoreRgba32Float || !caps_.typedUavStoreRgba32Uint) {
        throw UnsupportedHardware("Typed UAV stores for R8G8B8A8_UNORM, R32G32B32A32_FLOAT, and R32G32B32A32_UINT are required");
    }

    // Debug messages.
    if (debugLayer_ && SUCCEEDED(device_.As(&infoQueue_))) {
        if (IsDebuggerPresent()) {
            infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue_->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
        if (SUCCEEDED(infoQueue_.As(&infoQueue1_))) {
            if (FAILED(infoQueue1_->RegisterMessageCallback(&InfoQueueCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, this, &callbackCookie_))) {
                infoQueue1_.Reset();
                log::Warn("ID3D12InfoQueue1::RegisterMessageCallback failed; falling back to polling");
            }
        }
    }

    log::Info("Adapter selected: {}", adapterInfo_.Summary());
    log::Info("Device capabilities: {} | {} | root signature 1.{} | binding tier {} | tearing {} | VRAM budget {} MiB, in use {} MiB",
              RaytracingTierName(caps_.raytracingTier), ShaderModelName(caps_.highestShaderModel),
              caps_.rootSignatureVersion == D3D_ROOT_SIGNATURE_VERSION_1_1 ? 1 : 0,
              static_cast<int>(caps_.resourceBindingTier), caps_.tearingSupported ? "yes" : "no",
              caps_.videoMemoryBudget / (1024 * 1024), caps_.videoMemoryCurrentUsage / (1024 * 1024));
}

Device::~Device() {
    DrainInfoQueue();
    if (infoQueue1_ && callbackCookie_ != 0) {
        infoQueue1_->UnregisterMessageCallback(callbackCookie_);
    }
}

void Device::OnDebugMessage(D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, const char* description) {
    switch (severity) {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        case D3D12_MESSAGE_SEVERITY_ERROR:
            infoQueueErrors_.fetch_add(1);
            log::Error("[D3D12 #{}] {}", static_cast<int>(id), description);
            break;
        case D3D12_MESSAGE_SEVERITY_WARNING:
            infoQueueWarnings_.fetch_add(1);
            log::Warn("[D3D12 #{}] {}", static_cast<int>(id), description);
            break;
        default:
            log::Debug("[D3D12 #{}] {}", static_cast<int>(id), description);
            break;
    }
}

void Device::DrainInfoQueue() {
    if (!infoQueue_ || infoQueue1_) {
        return;  // Nothing stored, or the callback already delivered everything.
    }
    const UINT64 count = infoQueue_->GetNumStoredMessages();
    std::vector<char> storage;
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T length = 0;
        if (FAILED(infoQueue_->GetMessage(i, nullptr, &length)) || length == 0) {
            continue;
        }
        storage.resize(length);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (SUCCEEDED(infoQueue_->GetMessage(i, message, &length))) {
            OnDebugMessage(message->Severity, message->ID, message->pDescription);
        }
    }
    infoQueue_->ClearStoredMessages();
}

void Device::UpdateVideoMemoryInfo() {
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (adapter_ && SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        caps_.videoMemoryBudget = info.Budget;
        caps_.videoMemoryCurrentUsage = info.CurrentUsage;
    }
}

void Device::ReportDeviceRemoved() {
    const HRESULT reason = device_->GetDeviceRemovedReason();
    log::Error("Device removed: {}", HrToString(reason));

    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (FAILED(device_.As(&dred))) {
        log::Warn("DRED data unavailable");
        return;
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 crumbs{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&crumbs))) {
        for (const D3D12_AUTO_BREADCRUMB_NODE1* node = crumbs.pHeadAutoBreadcrumbNode; node != nullptr; node = node->pNext) {
            const UINT last = node->pLastBreadcrumbValue != nullptr ? *node->pLastBreadcrumbValue : 0;
            const char* listName = node->pCommandListDebugNameA != nullptr ? node->pCommandListDebugNameA : "<unnamed list>";
            const char* queueName = node->pCommandQueueDebugNameA != nullptr ? node->pCommandQueueDebugNameA : "<unnamed queue>";
            log::Error("DRED breadcrumbs: list '{}' on queue '{}': {} ops, last completed {}", listName, queueName,
                       node->BreadcrumbCount, last);
            const UINT begin = last >= 5 ? last - 5 : 0;
            const UINT end = std::min(node->BreadcrumbCount, last + 2);
            for (UINT i = begin; i < end; ++i) {
                log::Error("  op[{}] {}{}", i, BreadcrumbOpName(node->pCommandHistory[i]), i == last ? "  <-- last completed" : "");
            }
        }
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&fault))) {
        log::Error("DRED page fault VA: 0x{:016X}", fault.PageFaultVA);
        for (const D3D12_DRED_ALLOCATION_NODE1* node = fault.pHeadExistingAllocationNode; node != nullptr; node = node->pNext) {
            log::Error("  existing allocation: '{}' type {}", node->ObjectNameA != nullptr ? node->ObjectNameA : "<unnamed>",
                       static_cast<int>(node->AllocationType));
        }
        for (const D3D12_DRED_ALLOCATION_NODE1* node = fault.pHeadRecentFreedAllocationNode; node != nullptr; node = node->pNext) {
            log::Error("  recently freed: '{}' type {}", node->ObjectNameA != nullptr ? node->ObjectNameA : "<unnamed>",
                       static_cast<int>(node->AllocationType));
        }
    }
}

}  // namespace lc::gfx
