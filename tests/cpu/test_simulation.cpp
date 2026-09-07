#include "lc_test.h"

#include "game/replay.h"
#include "game/simulation.h"

#include <string>
#include <vector>

LC_TEST(simulation_runs_whole_ticks_and_reports_alpha) {
    lc::game::Simulation sim(60);
    std::vector<std::uint64_t> ticks;
    const auto tick = [&](std::uint64_t t, float dt) {
        ticks.push_back(t);
        LC_CHECK_NEAR(dt, 1.0f / 60.0f, 1e-7);
    };
    // One second delivered in four frames of 0.25 s (the per-frame cap): 60 ticks, alpha 0.
    lc::game::Simulation::StepResult r;
    for (int i = 0; i < 4; ++i) {
        r = sim.Advance(0.25, tick);
        LC_CHECK_EQ(r.ticksRun, 15u);
        LC_CHECK(!r.clamped);
    }
    LC_CHECK_NEAR(r.alpha, 0.0f, 1e-5);
    LC_CHECK_EQ(ticks.size(), std::size_t{60});
    LC_CHECK_EQ(ticks.front(), std::uint64_t{0});
    LC_CHECK_EQ(ticks.back(), std::uint64_t{59});

    r = sim.Advance(0.5 / 60.0, tick);  // Half a tick: nothing runs, alpha 0.5.
    LC_CHECK_EQ(r.ticksRun, 0u);
    LC_CHECK_NEAR(r.alpha, 0.5f, 1e-5);
    r = sim.Advance(0.5 / 60.0, tick);
    LC_CHECK_EQ(r.ticksRun, 1u);
    LC_CHECK_EQ(sim.Tick(), std::uint64_t{61});

    r = sim.Advance(10.0, tick);  // Clamped to 0.25 s = 15 ticks.
    LC_CHECK(r.clamped);
    LC_CHECK_EQ(r.ticksRun, 15u);

    sim.RunTicks(3, tick);
    LC_CHECK_EQ(sim.Tick(), std::uint64_t{79});
    LC_CHECK_NEAR(sim.Advance(0.0, tick).alpha, 0.0f, 0.0f);
}

LC_TEST(replay_records_run_lengths_and_looks_up_input) {
    lc::game::Replay replay;
    replay.scene = "two_room";
    lc::game::InputFrame forward;
    forward.moveZ = 1.0f;
    lc::game::InputFrame turn;
    turn.lookDx = 0.01f;
    for (std::uint64_t t = 0; t < 10; ++t) replay.Record(t, lc::game::InputFrame{});  // Neutral: no segments.
    for (std::uint64_t t = 10; t < 70; ++t) replay.Record(t, forward);
    for (std::uint64_t t = 70; t < 80; ++t) replay.Record(t, lc::game::InputFrame{});
    for (std::uint64_t t = 80; t < 90; ++t) replay.Record(t, turn);
    LC_REQUIRE(replay.segments.size() == 2);
    LC_CHECK_EQ(replay.segments[0].fromTick, std::uint64_t{10});
    LC_CHECK_EQ(replay.segments[0].toTick, std::uint64_t{69});
    LC_CHECK_EQ(replay.segments[1].fromTick, std::uint64_t{80});
    LC_CHECK(replay.InputAt(5).IsNeutral());
    LC_CHECK_NEAR(replay.InputAt(30).moveZ, 1.0f, 0.0f);
    LC_CHECK(replay.InputAt(75).IsNeutral());
    LC_CHECK_NEAR(replay.InputAt(85).lookDx, 0.01f, 0.0f);
    LC_CHECK(replay.InputAt(1000).IsNeutral());
    LC_CHECK_EQ(replay.LastTick(), std::uint64_t{89});
}

LC_TEST(replay_json_round_trip_and_validation) {
    lc::game::Replay replay;
    replay.scene = "two_room";
    replay.seed = 7;
    lc::game::InputFrame forward;
    forward.moveZ = 1.0f;
    forward.sprint = true;
    for (std::uint64_t t = 3; t < 8; ++t) replay.Record(t, forward);
    lc::game::ReplayCheck check;
    check.tick = 120;
    check.kind = "hit";
    check.description = "mirror shows the threat";
    check.point = lc::math::Vec3{1.0f, 2.0f, 3.0f};
    check.entity = "threat_body";
    replay.checks.push_back(check);
    lc::game::ReplayCheck ratio;
    ratio.tick = 5;
    ratio.kind = "patch_ratio";
    ratio.point = lc::math::Vec3{0, 0, 0};
    ratio.otherPoint = lc::math::Vec3{1, 0, 0};
    ratio.ratioFactor = 1.5f;
    replay.checks.push_back(ratio);

    std::string error;
    const auto loaded = lc::game::Replay::FromJson(replay.ToJson(), error);
    LC_REQUIRE(loaded.has_value());
    LC_CHECK_EQ(loaded->scene, std::string("two_room"));
    LC_CHECK_EQ(loaded->seed, 7u);
    LC_REQUIRE(loaded->segments.size() == 1);
    LC_CHECK_EQ(loaded->segments[0].fromTick, std::uint64_t{3});
    LC_CHECK_EQ(loaded->segments[0].toTick, std::uint64_t{7});
    LC_CHECK(loaded->segments[0].input.sprint);
    LC_REQUIRE(loaded->checks.size() == 2);
    LC_CHECK_EQ(loaded->checks[0].entity, std::string("threat_body"));
    LC_CHECK(loaded->checks[0].point.has_value());
    LC_CHECK_NEAR(loaded->checks[0].point->z, 3.0f, 0.0f);
    LC_CHECK_NEAR(loaded->checks[1].ratioFactor, 1.5f, 1e-6);
    LC_CHECK(loaded->checks[1].otherPoint.has_value());
    LC_CHECK_EQ(loaded->LastTick(), std::uint64_t{120});

    LC_CHECK(!lc::game::Replay::FromJson(R"({"version": 2})", error).has_value());
    LC_CHECK(!lc::game::Replay::FromJson(R"({"version": 1, "segments": [{"from": 5, "to": 9}, {"from": 7, "to": 12}]})", error).has_value());
    LC_CHECK(!lc::game::Replay::FromJson(R"({"version": 1, "checks": [{"tick": 1, "kind": "bogus"}]})", error).has_value());
    LC_CHECK(!lc::game::Replay::FromJson("not json", error).has_value());
    LC_CHECK(!error.empty());
}
