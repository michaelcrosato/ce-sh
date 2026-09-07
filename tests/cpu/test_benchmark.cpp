#include "lc_test.h"

#include "app/benchmark.h"
#include "core/json_reader.h"

#include <vector>

LC_TEST(frame_time_statistics_use_nearest_rank_percentiles) {
    std::vector<double> ms;
    for (int i = 1; i <= 100; ++i) ms.push_back(static_cast<double>(i));  // 1..100 ms, shuffled order below.
    std::swap(ms[0], ms[99]);
    std::swap(ms[10], ms[50]);
    const lc::FrameTimeStatistics s = lc::ComputeFrameTimeStatistics(ms);
    LC_CHECK_EQ(s.count, 100u);
    LC_CHECK_NEAR(s.averageMs, 50.5, 1e-9);
    LC_CHECK_NEAR(s.medianMs, 50.5, 1e-9);
    LC_CHECK_NEAR(s.p95Ms, 95.0, 1e-9);   // ceil(0.95 * 100) = 95th smallest.
    LC_CHECK_NEAR(s.p99Ms, 99.0, 1e-9);
    LC_CHECK_NEAR(s.maxMs, 100.0, 1e-9);
    LC_CHECK_EQ(s.above33Ms, 67u);  // 34..100
    LC_CHECK_EQ(s.above50Ms, 50u);  // 51..100

    const lc::FrameTimeStatistics empty = lc::ComputeFrameTimeStatistics({});
    LC_CHECK_EQ(empty.count, 0u);
    const double one[] = {7.0};
    const lc::FrameTimeStatistics single = lc::ComputeFrameTimeStatistics(one);
    LC_CHECK_NEAR(single.p99Ms, 7.0, 0.0);
    LC_CHECK_NEAR(single.medianMs, 7.0, 0.0);
}

LC_TEST(benchmark_report_is_valid_json_with_the_required_sections) {
    lc::BenchmarkReport r;
    r.buildCommit = "abc123";
    r.scene = "two_room";
    r.sceneContentHash = 0x1234;
    r.shaderHashes = {{"path_trace_guided.cso", 42}};
    r.replay = "tests/replay/t12_mirror_motion.json";
    r.replayTicks = 930;
    r.replayLoops = 2;
    r.mode = "denoised";
    r.renderWidth = 1280;
    r.renderHeight = 720;
    r.outputWidth = 1920;
    r.outputHeight = 1080;
    const double ms[] = {10.0, 12.0, 11.0};
    r.cpu = lc::ComputeFrameTimeStatistics(ms);
    r.gpu = r.cpu;
    r.passNames = {"path_trace", "denoise"};
    r.passAverageMs = {0.8, 1.1};
    const std::string text = r.ToJson();
    const lc::json::ParseResult parsed = lc::json::Parse(text);
    LC_REQUIRE(parsed.value.has_value());
    const lc::json::Value& root = *parsed.value;
    LC_CHECK(root.Get("build") != nullptr);
    LC_CHECK(root.Get("content") != nullptr);
    LC_CHECK(root.Get("device") != nullptr);
    LC_CHECK(root.Get("settings") != nullptr);
    LC_CHECK(root.Get("timings") != nullptr);
    LC_CHECK(root.Get("memory") != nullptr);
    LC_CHECK(root.Get("durations") != nullptr);
    LC_CHECK_EQ(root.Get("content")->StringOr("sceneContentHash", ""), std::string("0000000000001234"));
    LC_CHECK_NEAR(root.Get("timings")->Get("gpuFrameMs")->NumberOr("p99Ms", 0.0), 12.0, 1e-9);
    LC_CHECK(root.Get("settings")->BoolOr("frameGeneration", true) == false);
}
