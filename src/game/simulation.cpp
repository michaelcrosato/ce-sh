#include "game/simulation.h"

#include <algorithm>

namespace lc::game {

Simulation::Simulation(std::uint32_t tickRate) : tickRate_(tickRate), dt_(1.0f / static_cast<float>(tickRate)) {}

Simulation::StepResult Simulation::Advance(double realSeconds, const std::function<void(std::uint64_t, float)>& tick) {
    StepResult result;
    if (realSeconds > MaxFrameSeconds()) {
        realSeconds = MaxFrameSeconds();
        result.clamped = true;
    }
    if (realSeconds > 0.0) {
        accumulator_ += realSeconds;
    }
    const double step = 1.0 / static_cast<double>(tickRate_);
    while (accumulator_ >= step - 1e-9) {
        tick(tick_, dt_);
        ++tick_;
        ++result.ticksRun;
        accumulator_ -= step;
    }
    if (accumulator_ < 0.0) {
        accumulator_ = 0.0;
    }
    result.alpha = static_cast<float>(std::clamp(accumulator_ / step, 0.0, 1.0));
    return result;
}

void Simulation::RunTicks(std::uint32_t n, const std::function<void(std::uint64_t, float)>& tick) {
    for (std::uint32_t i = 0; i < n; ++i) {
        tick(tick_, dt_);
        ++tick_;
    }
    accumulator_ = 0.0;
}

}  // namespace lc::game
