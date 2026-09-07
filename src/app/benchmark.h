// Benchmark statistics and report (spec §17 protocol, §21 report contents). Pure data and
// formatting so the CPU tests can pin the percentile definitions.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lc {

struct FrameTimeStatistics {
    std::uint32_t count = 0;
    double averageMs = 0.0;
    double medianMs = 0.0;
    double p95Ms = 0.0;       // Nearest-rank percentile: the ceil(0.95 * count)-th smallest value.
    double p99Ms = 0.0;
    double maxMs = 0.0;
    std::uint32_t above33Ms = 0;  // Frames longer than 33.3 ms (two 60 Hz periods).
    std::uint32_t above50Ms = 0;
};

FrameTimeStatistics ComputeFrameTimeStatistics(std::span<const double> milliseconds);

struct NamedHash {
    std::string name;
    std::uint64_t hash = 0;
};

struct BenchmarkReport {
    // Identification.
    std::string buildCommit;
    bool buildDirty = false;
    std::string buildConfig;
    std::string scene;
    std::uint64_t sceneContentHash = 0;
    std::vector<NamedHash> shaderHashes;
    std::string denoiserVersion;
    std::string replay;            // Replay identifier (path) and its tick count.
    std::uint64_t replayTicks = 0;
    std::uint32_t replayLoops = 0;
    // Device.
    std::string adapter;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::string driver;
    std::string os;
    // Settings.
    std::string mode;
    std::uint32_t renderWidth = 0;
    std::uint32_t renderHeight = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    std::uint32_t maxHits = 0;
    std::uint32_t historyFrames = 0;
    bool antiFirefly = false;
    bool vsync = false;
    bool frameCap = false;
    bool windowed = false;
    bool overlay = false;
    // Durations.
    double warmupSeconds = 0.0;
    double measuredSeconds = 0.0;
    std::uint32_t warmupFrames = 0;
    // Timings.
    FrameTimeStatistics cpu;
    FrameTimeStatistics gpu;
    std::vector<NamedHash> gpuPassAverages;  // name -> average milliseconds * 1e6 (fixed point), see ToJson.
    std::vector<std::string> passNames;
    std::vector<double> passAverageMs;
    // Memory.
    std::uint64_t videoMemoryBudget = 0;
    std::uint64_t videoMemoryUsage = 0;
    std::uint64_t processWorkingSet = 0;
    std::uint64_t processPeakWorkingSet = 0;
    std::uint64_t denoiserPoolBytes = 0;
    std::string timestamp;

    std::string ToJson() const;
};

}  // namespace lc
