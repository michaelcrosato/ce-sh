// Fixed-step simulation clock (spec §3: 60 updates per second). Real time is accumulated and
// consumed in whole ticks; the remainder becomes the interpolation factor for rendering.
#pragma once

#include <cstdint>
#include <functional>

namespace lc::game {

class Simulation {
public:
    struct StepResult {
        std::uint32_t ticksRun = 0;
        float alpha = 0.0f;  // Fraction of the next tick already elapsed, for render interpolation.
        bool clamped = false; // Real time exceeded the cap and was dropped (spiral-of-death guard).
    };

    explicit Simulation(std::uint32_t tickRate = 60);

    // Advances by real seconds, calling tick(tickIndex, dt) for every whole tick.
    StepResult Advance(double realSeconds, const std::function<void(std::uint64_t, float)>& tick);

    // Runs exactly n ticks (replay and tests); alpha becomes 0.
    void RunTicks(std::uint32_t n, const std::function<void(std::uint64_t, float)>& tick);

    std::uint64_t Tick() const { return tick_; }
    float Dt() const { return dt_; }
    std::uint32_t TickRate() const { return tickRate_; }
    double MaxFrameSeconds() const { return 0.25; }

private:
    std::uint32_t tickRate_ = 60;
    float dt_ = 1.0f / 60.0f;
    double accumulator_ = 0.0;
    std::uint64_t tick_ = 0;
};

}  // namespace lc::game
