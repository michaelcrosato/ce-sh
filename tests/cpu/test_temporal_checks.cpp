#include "lc_test.h"

#include "app/temporal_checks.h"

#include <vector>

namespace {

// A dark object leaves a bright patch at index 10; the luminance recovers with a given lag.
void MakeSeries(std::vector<double>& lum, std::vector<std::uint32_t>& occ, std::uint32_t lagFrames, std::size_t frames = 60) {
    lum.assign(frames, 0.02);
    occ.assign(frames, 0u);
    for (std::size_t i = 0; i < 10; ++i) occ[i] = 25;
    for (std::size_t i = 10; i < frames; ++i) {
        const double t = i - 10 < lagFrames ? static_cast<double>(i - 10) / static_cast<double>(lagFrames) : 1.0;
        lum[i] = 0.02 + 0.98 * t;  // Reaches 1.0 exactly lagFrames frames after departure.
    }
}

}  // namespace

LC_TEST(trail_lag_measures_frames_to_settle_after_departure) {
    std::vector<double> lum;
    std::vector<std::uint32_t> occ;
    lc::TrailLagSettings s;
    s.settleFraction = 0.8f;
    s.maxLagFrames = 6;

    MakeSeries(lum, occ, 4);
    lc::TrailLagResult r = lc::EvaluateTrailLag(lum, occ, s);
    LC_REQUIRE(r.valid);
    LC_CHECK(r.passed);
    LC_CHECK_EQ(r.departureIndex, std::size_t{10});
    // 80 % of the step is reached at t = 0.8 -> frame 3.2 -> first frame within 20 %: index 4 (t = 1.0)... or 3? t(3) = 0.75 -> 0.755 vs threshold 0.196: |0.755-1| = 0.245 > 0.196; t(4) = 1.0 -> lag 4.
    LC_CHECK_EQ(r.lagFrames, 4u);

    MakeSeries(lum, occ, 12);
    r = lc::EvaluateTrailLag(lum, occ, s);
    LC_REQUIRE(r.valid);
    LC_CHECK(!r.passed);
    LC_CHECK(r.lagFrames > 6u);
}

LC_TEST(trail_lag_reports_invalid_series_instead_of_guessing) {
    std::vector<double> lum;
    std::vector<std::uint32_t> occ;
    lc::TrailLagSettings s;

    // Never leaves.
    lum.assign(50, 0.5);
    occ.assign(50, 3u);
    LC_CHECK(!lc::EvaluateTrailLag(lum, occ, s).valid);

    // Leaves too late for the settle window.
    MakeSeries(lum, occ, 2, 30);
    LC_CHECK(!lc::EvaluateTrailLag(lum, occ, s).valid);

    // Re-enters before the window closes.
    MakeSeries(lum, occ, 2, 60);
    occ[25] = 4;
    LC_CHECK(!lc::EvaluateTrailLag(lum, occ, s).valid);

    // No contrast change.
    lum.assign(60, 0.3);
    occ.assign(60, 0u);
    for (std::size_t i = 0; i < 10; ++i) occ[i] = 9;
    LC_CHECK(!lc::EvaluateTrailLag(lum, occ, s).valid);
}
