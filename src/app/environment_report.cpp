#include "app/environment_report.h"

#include "core/build_info.h"
#include "core/clock.h"
#include "core/json_writer.h"
#include "core/log.h"
#include "platform/win_error.h"

#include <windows.h>

#include <intrin.h>

#include <cstring>
#include <format>

namespace lc {

namespace {

std::string OsVersionString() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            fn(&info);
        }
    }
    DWORD ubr = 0;
    DWORD size = sizeof(ubr);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD, nullptr, &ubr, &size);
    return std::format("{}.{}.{}.{}", info.dwMajorVersion, info.dwMinorVersion, info.dwBuildNumber, ubr);
}

std::string OsDisplayVersion() {
    wchar_t buffer[64] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion", RRF_RT_REG_SZ, nullptr,
                     buffer, &size) == ERROR_SUCCESS) {
        return WideToUtf8(buffer);
    }
    return "unknown";
}

std::string CpuBrandString() {
    int regs[4] = {};
    char brand[49] = {};
    __cpuid(regs, 0x80000000);
    if (static_cast<unsigned>(regs[0]) < 0x80000004u) {
        return "unknown";
    }
    for (int i = 0; i < 3; ++i) {
        __cpuid(regs, 0x80000002 + i);
        std::memcpy(brand + i * 16, regs, 16);
    }
    std::string text(brand);
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

}  // namespace

EnvironmentInfo CollectEnvironment(const std::vector<gfx::AdapterInfo>& adapters, const gfx::Device* device) {
    EnvironmentInfo e;
    e.timestamp = Clock::TimestampIso8601();
    e.osVersion = OsVersionString();
    e.osDisplayVersion = OsDisplayVersion();
    e.cpuName = CpuBrandString();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    e.logicalProcessors = si.dwNumberOfProcessors;
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        e.physicalMemoryBytes = ms.ullTotalPhys;
    }
    e.buildCommit = build::kGitCommit;
    e.buildDirty = build::kGitDirty;
    e.buildConfig = build::kConfig;
    e.version = build::kVersion;
    e.adapters = adapters;
    if (device != nullptr) {
        e.deviceCreated = true;
        e.selectedAdapter = device->AdapterDetails().index;
        e.selectedRaytracingTier = gfx::RaytracingTierName(device->Caps().raytracingTier);
        e.selectedShaderModel = gfx::ShaderModelName(device->Caps().highestShaderModel);
        e.debugLayer = device->DebugLayerEnabled();
        e.videoMemoryBudget = device->Caps().videoMemoryBudget;
        e.videoMemoryUsage = device->Caps().videoMemoryCurrentUsage;
    }
    return e;
}

void LogEnvironment(const EnvironmentInfo& e) {
    log::Info("Environment: Windows {} ({}) | {} ({} logical CPUs) | {} GiB RAM | build {}{} {} v{}", e.osVersion, e.osDisplayVersion,
              e.cpuName, e.logicalProcessors, e.physicalMemoryBytes / (1024ull * 1024 * 1024), e.buildCommit,
              e.buildDirty ? "-dirty" : "", e.buildConfig, e.version);
    for (const gfx::AdapterInfo& a : e.adapters) {
        log::Info("Adapter {}", a.Summary());
    }
    if (e.deviceCreated) {
        log::Info("Selected adapter {}: {} | {} | debug layer {} | VRAM budget {} MiB, in use {} MiB", e.selectedAdapter,
                  e.selectedRaytracingTier, e.selectedShaderModel, e.debugLayer ? "on" : "off",
                  e.videoMemoryBudget / (1024 * 1024), e.videoMemoryUsage / (1024 * 1024));
    }
}

std::string EnvironmentToJson(const EnvironmentInfo& e) {
    JsonWriter j;
    j.BeginObject();
    j.Field("timestamp", e.timestamp);
    j.Key("os");
    j.BeginObject();
    j.Field("version", e.osVersion);
    j.Field("displayVersion", e.osDisplayVersion);
    j.EndObject();
    j.Key("cpu");
    j.BeginObject();
    j.Field("name", e.cpuName);
    j.Field("logicalProcessors", e.logicalProcessors);
    j.EndObject();
    j.Field("physicalMemoryBytes", e.physicalMemoryBytes);
    j.Key("build");
    j.BeginObject();
    j.Field("commit", e.buildCommit);
    j.Field("dirty", e.buildDirty);
    j.Field("config", e.buildConfig);
    j.Field("version", e.version);
    j.EndObject();
    j.Key("adapters");
    j.BeginArray();
    for (const gfx::AdapterInfo& a : e.adapters) {
        j.BeginObject();
        j.Field("index", a.index);
        j.Field("description", WideToUtf8(a.description));
        j.Field("vendorId", std::format("0x{:04X}", a.vendorId));
        j.Field("deviceId", std::format("0x{:04X}", a.deviceId));
        j.Field("revision", a.revision);
        j.Field("driverVersion", a.driverVersion);
        j.Field("dedicatedVideoMemoryBytes", a.dedicatedVideoMemory);
        j.Field("software", a.software);
        j.Field("featureLevel12_1", a.supportsFeatureLevel12_1);
        j.Field("raytracingTier", gfx::RaytracingTierName(a.raytracingTier));
        j.Field("shaderModel", gfx::ShaderModelName(a.highestShaderModel));
        j.Field("supported", a.SupportsLastCircuit());
        j.EndObject();
    }
    j.EndArray();
    j.Key("device");
    if (e.deviceCreated) {
        j.BeginObject();
        j.Field("adapterIndex", e.selectedAdapter);
        j.Field("raytracingTier", e.selectedRaytracingTier);
        j.Field("shaderModel", e.selectedShaderModel);
        j.Field("debugLayer", e.debugLayer);
        j.Field("videoMemoryBudgetBytes", e.videoMemoryBudget);
        j.Field("videoMemoryUsageBytes", e.videoMemoryUsage);
        j.EndObject();
    } else {
        j.Null();
    }
    j.EndObject();
    return j.Text() + "\n";
}

}  // namespace lc
