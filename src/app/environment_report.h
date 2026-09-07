// Honest environment report: what machine, driver, and build actually ran (spec M0 gate).
#pragma once

#include "graphics/d3d12/device.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lc {

struct EnvironmentInfo {
    std::string timestamp;
    std::string osVersion;         // "10.0.26200.1234"
    std::string osDisplayVersion;  // "25H2"
    std::string cpuName;
    std::uint32_t logicalProcessors = 0;
    std::uint64_t physicalMemoryBytes = 0;
    std::string buildCommit;
    bool buildDirty = false;
    std::string buildConfig;
    std::string version;
    std::vector<gfx::AdapterInfo> adapters;
    bool deviceCreated = false;
    std::uint32_t selectedAdapter = 0;
    std::string selectedRaytracingTier;
    std::string selectedShaderModel;
    bool debugLayer = false;
    std::uint64_t videoMemoryBudget = 0;
    std::uint64_t videoMemoryUsage = 0;
};

EnvironmentInfo CollectEnvironment(const std::vector<gfx::AdapterInfo>& adapters, const gfx::Device* deviceOrNull);
void LogEnvironment(const EnvironmentInfo& info);
std::string EnvironmentToJson(const EnvironmentInfo& info);

}  // namespace lc
