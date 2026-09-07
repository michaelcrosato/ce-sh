// Temporal image checks (spec §19 T12) evaluated on per-frame patch statistics. Pure functions so
// the CPU tests can pin the metric definitions.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace lc {

// Trail lag: the number of frames between the frame in which a moving object has left a patch
// (no pixel of the patch reports its identity any more) and the first frame in which the patch's
// denoised luminance is within (1 - settleFraction) of the step from the occupied value to the
// settled value. The settled value is the mean over `settleWindow` frames starting `settleDelay`
// frames after departure; the object must stay out of the patch for settleDelay + settleWindow frames.
struct TrailLagSettings {
    float settleFraction = 0.8f;
    std::uint32_t maxLagFrames = 6;      // 100 ms at 60 Hz.
    std::uint32_t settleDelay = 20;
    std::uint32_t settleWindow = 12;
};

struct TrailLagResult {
    bool valid = false;                  // The series allows a verdict (departure found, enough frames, contrast present).
    bool passed = false;
    std::uint32_t lagFrames = 0;
    std::size_t departureIndex = 0;      // Index of the first unoccupied frame after an occupied one.
    double occupiedLuminance = 0.0;      // Luminance in the last occupied frame.
    double settledLuminance = 0.0;
    std::string detail;
};

TrailLagResult EvaluateTrailLag(std::span<const double> luminance, std::span<const std::uint32_t> occupiedPixels,
                                const TrailLagSettings& settings);

}  // namespace lc
