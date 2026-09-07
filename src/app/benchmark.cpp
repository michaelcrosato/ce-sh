#include "app/benchmark.h"

#include "core/json_writer.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace lc {

FrameTimeStatistics ComputeFrameTimeStatistics(std::span<const double> milliseconds) {
    FrameTimeStatistics s;
    if (milliseconds.empty()) {
        return s;
    }
    std::vector<double> sorted(milliseconds.begin(), milliseconds.end());
    std::sort(sorted.begin(), sorted.end());
    const std::size_t n = sorted.size();
    s.count = static_cast<std::uint32_t>(n);
    double sum = 0.0;
    for (const double v : sorted) {
        sum += v;
        if (v > 33.3) ++s.above33Ms;
        if (v > 50.0) ++s.above50Ms;
    }
    s.averageMs = sum / static_cast<double>(n);
    auto nearestRank = [&](double p) {
        const std::size_t rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(n)));
        return sorted[std::min(n - 1, rank == 0 ? 0 : rank - 1)];
    };
    s.medianMs = n % 2 == 1 ? sorted[n / 2] : 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
    s.p95Ms = nearestRank(0.95);
    s.p99Ms = nearestRank(0.99);
    s.maxMs = sorted.back();
    return s;
}

namespace {

void WriteStatistics(JsonWriter& j, const char* key, const FrameTimeStatistics& s) {
    j.Key(key);
    j.BeginObject();
    j.Field("frames", s.count);
    j.Field("averageMs", s.averageMs);
    j.Field("medianMs", s.medianMs);
    j.Field("p95Ms", s.p95Ms);
    j.Field("p99Ms", s.p99Ms);
    j.Field("maxMs", s.maxMs);
    j.Field("framesAbove33ms", s.above33Ms);
    j.Field("framesAbove50ms", s.above50Ms);
    j.EndObject();
}

}  // namespace

std::string BenchmarkReport::ToJson() const {
    JsonWriter j;
    j.BeginObject();
    j.Field("reportVersion", 1u);
    j.Field("timestamp", timestamp);
    j.Key("build");
    j.BeginObject();
    j.Field("commit", buildCommit);
    j.Field("dirty", buildDirty);
    j.Field("config", buildConfig);
    j.EndObject();
    j.Key("content");
    j.BeginObject();
    j.Field("scene", scene);
    j.Field("sceneContentHash", std::format("{:016x}", sceneContentHash));
    j.Key("shaderHashes");
    j.BeginObject();
    for (const NamedHash& h : shaderHashes) j.Field(h.name, std::format("{:016x}", h.hash));
    j.EndObject();
    j.Field("denoiser", denoiserVersion);
    j.Field("replay", replay);
    j.Field("replayTicks", replayTicks);
    j.Field("replayLoops", replayLoops);
    j.EndObject();
    j.Key("device");
    j.BeginObject();
    j.Field("adapter", adapter);
    j.Field("vendorId", vendorId);
    j.Field("deviceId", deviceId);
    j.Field("driver", driver);
    j.Field("os", os);
    j.EndObject();
    j.Key("settings");
    j.BeginObject();
    j.Field("mode", mode);
    j.Key("renderSize");
    j.BeginArray();
    j.Value(renderWidth);
    j.Value(renderHeight);
    j.EndArray();
    j.Key("outputSize");
    j.BeginArray();
    j.Value(outputWidth);
    j.Value(outputHeight);
    j.EndArray();
    j.Field("maxHits", maxHits);
    j.Field("historyFrames", historyFrames);
    j.Field("antiFirefly", antiFirefly);
    j.Field("vsync", vsync);
    j.Field("frameCap", frameCap);
    j.Field("windowed", windowed);
    j.Field("diagnosticOverlay", overlay);
    j.Field("frameGeneration", false);
    j.EndObject();
    j.Key("durations");
    j.BeginObject();
    j.Field("warmupSeconds", warmupSeconds);
    j.Field("warmupFrames", warmupFrames);
    j.Field("measuredSeconds", measuredSeconds);
    j.EndObject();
    j.Key("timings");
    j.BeginObject();
    WriteStatistics(j, "cpuFrameMs", cpu);
    WriteStatistics(j, "gpuFrameMs", gpu);
    j.Key("gpuPassAverageMs");
    j.BeginObject();
    for (std::size_t i = 0; i < passNames.size() && i < passAverageMs.size(); ++i) j.Field(passNames[i], passAverageMs[i]);
    j.EndObject();
    j.EndObject();
    j.Key("memory");
    j.BeginObject();
    j.Field("videoMemoryBudgetBytes", videoMemoryBudget);
    j.Field("videoMemoryUsageBytes", videoMemoryUsage);
    j.Field("processWorkingSetBytes", processWorkingSet);
    j.Field("processPeakWorkingSetBytes", processPeakWorkingSet);
    j.Field("denoiserPoolBytes", denoiserPoolBytes);
    j.EndObject();
    j.EndObject();
    return j.Text() + "\n";
}

}  // namespace lc
