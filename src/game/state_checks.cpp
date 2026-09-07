#include "game/state_checks.h"

#include <cmath>
#include <format>

namespace lc::game {

bool EvaluateStateCheck(const World& world, const ReplayCheck& check, std::string& detail) {
    if (check.kind == "objective_state") {
        const std::string& actual = world.Phase();
        detail = std::format("objective '{}' (expected '{}')", actual, check.state);
        return check.state == actual;
    }
    if (check.kind == "threat_state") {
        const char* actual = ThreatStateName(world.GetThreat().State());
        detail = std::format("threat '{}' (expected '{}')", actual, check.state);
        return check.state == actual;
    }
    if (check.kind == "caught_count") {
        detail = std::format("caught {} time(s) (expected {})", world.Catches(), check.count);
        return world.Catches() == check.count;
    }
    if (check.kind == "player_near") {
        const math::Vec3 feet = world.GetPlayer().Current().position;
        const float dx = feet.x - check.point->x;
        const float dz = feet.z - check.point->z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        detail = std::format("player at ({:.2f}, {:.2f}, {:.2f}), {:.2f} m from ({:.2f}, {:.2f}, {:.2f}) (tolerance {:.2f})", feet.x, feet.y, feet.z, distance,
                             check.point->x, check.point->y, check.point->z, check.tolerance);
        return distance <= check.tolerance;
    }
    detail = "not a world-state check";
    return false;
}

StateCheckLog::StateCheckLog(const Replay& replay) : replay_(replay) {
    for (const ReplayCheck& c : replay_.checks) {
        if (IsStateCheck(c)) ++total_;
    }
}

std::size_t StateCheckLog::PendingBeyond(std::uint64_t tickCount) const {
    std::size_t beyond = 0;
    for (const ReplayCheck& c : replay_.checks) {
        if (IsStateCheck(c) && c.tick > tickCount) ++beyond;
    }
    return beyond;
}

void StateCheckLog::AfterTick(const World& world, std::uint64_t tickCount) {
    for (const ReplayCheck& c : replay_.checks) {
        if (!IsStateCheck(c) || c.tick != tickCount) continue;
        std::string detail;
        const bool ok = EvaluateStateCheck(world, c, detail);
        ++evaluated_;
        if (!ok) ++failed_;
        lines_.push_back(std::format("state check tick {} '{}' ({}): {} -> {}", c.tick, c.description, c.kind, detail, ok ? "PASS" : "FAIL"));
    }
}

}  // namespace lc::game
