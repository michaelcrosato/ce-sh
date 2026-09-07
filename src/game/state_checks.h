// Replay checks evaluated from the world state right after their tick (spec §19 T14/T15): the
// objective phase, the threat's state, the catch count, the player's position. They need no image
// and no GPU, so the same code runs in the CPU-only mode and beside the renderer.
#pragma once

#include "game/replay.h"
#include "game/world.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lc::game {

// One check against the current world; `detail` explains the result.
bool EvaluateStateCheck(const World& world, const ReplayCheck& check, std::string& detail);

// Runs every state check of a replay at its tick and keeps the results.
class StateCheckLog {
public:
    explicit StateCheckLog(const Replay& replay);

    // Call after the world has run `tickCount` ticks in total (the simulation's Tick()).
    void AfterTick(const World& world, std::uint64_t tickCount);

    std::size_t Evaluated() const { return evaluated_; }
    std::size_t Failed() const { return failed_; }
    std::size_t Pending() const { return total_ - evaluated_; }  // Checks whose tick was never reached.
    // Checks whose tick lies beyond `tickCount`: a run stopped early on purpose (an image check's
    // --frames) leaves them unevaluated without being at fault.
    std::size_t PendingBeyond(std::uint64_t tickCount) const;
    const std::vector<std::string>& Lines() const { return lines_; }

private:
    const Replay& replay_;
    std::size_t total_ = 0;
    std::size_t evaluated_ = 0;
    std::size_t failed_ = 0;
    std::vector<std::string> lines_;
};

}  // namespace lc::game
