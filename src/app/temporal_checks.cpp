#include "app/temporal_checks.h"

#include <cmath>
#include <format>

namespace lc {

TrailLagResult EvaluateTrailLag(std::span<const double> luminance, std::span<const std::uint32_t> occupiedPixels,
                                const TrailLagSettings& settings) {
    TrailLagResult r;
    if (luminance.size() != occupiedPixels.size() || luminance.size() < 2) {
        r.detail = "not enough frames recorded";
        return r;
    }
    std::size_t departure = 0;
    for (std::size_t i = 1; i < occupiedPixels.size(); ++i) {
        if (occupiedPixels[i] == 0 && occupiedPixels[i - 1] > 0) {
            departure = i;
            break;
        }
    }
    if (departure == 0) {
        r.detail = "the entity never left the patch (or was never in it) during the recorded frames";
        return r;
    }
    const std::size_t needed = departure + settings.settleDelay + settings.settleWindow;
    if (luminance.size() < needed) {
        r.detail = std::format("only {} frames recorded after the departure at frame index {}; {} needed", luminance.size() - departure, departure,
                               settings.settleDelay + settings.settleWindow);
        return r;
    }
    for (std::size_t i = departure; i < needed; ++i) {
        if (occupiedPixels[i] != 0) {
            r.detail = std::format("the entity re-entered the patch at frame index {} before the settle window closed", i);
            return r;
        }
    }
    double settled = 0.0;
    for (std::size_t i = departure + settings.settleDelay; i < needed; ++i) {
        settled += luminance[i];
    }
    settled /= static_cast<double>(settings.settleWindow);
    const double before = luminance[departure - 1];
    const double step = settled - before;
    if (std::fabs(step) < 1e-6) {
        r.detail = std::format("no contrast change to measure (occupied {:.3e}, settled {:.3e})", before, settled);
        return r;
    }
    const double threshold = (1.0 - static_cast<double>(settings.settleFraction)) * std::fabs(step);
    std::uint32_t lag = static_cast<std::uint32_t>(luminance.size());  // No settled frame at all.
    for (std::size_t i = departure; i < needed; ++i) {
        if (std::fabs(luminance[i] - settled) <= threshold) {
            lag = static_cast<std::uint32_t>(i - departure);
            break;
        }
    }
    r.valid = true;
    r.lagFrames = lag;
    r.departureIndex = departure;
    r.occupiedLuminance = before;
    r.settledLuminance = settled;
    r.passed = lag <= settings.maxLagFrames;
    r.detail = std::format("departure at frame index {}: luminance {:.4e} -> settled {:.4e}; settled within {:.0f} % after {} frame(s) (limit {})",
                           departure, before, settled, settings.settleFraction * 100.0f, lag, settings.maxLagFrames);
    return r;
}

}  // namespace lc
