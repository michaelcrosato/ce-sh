// Spec §15: the threat's state machine on gameplay data, the objective phases with checkpoints,
// the catch that restarts from the checkpoint, and the world-state hash that only inputs change.
#include "lc_test.h"

#include "game/replay.h"
#include "game/simulation.h"
#include "game/state_checks.h"
#include "game/world.h"
#include "scene/scene_file.h"
#include "scene/two_room_level.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#ifndef LC_REPLAY_DIR
#define LC_REPLAY_DIR "tests/replay"
#endif

namespace {

using lc::game::InputFrame;
using lc::game::ThreatBehaviour;
using lc::game::ThreatState;
using lc::game::World;
using lc::math::Vec3;

void Run(World& world, const InputFrame& input, int ticks) {
    for (int i = 0; i < ticks; ++i) world.Tick(input, 1.0f / 60.0f);
}

InputFrame Move(float x, float z, bool sprint = false) {
    InputFrame f;
    f.moveX = x;
    f.moveZ = z;
    f.sprint = sprint;
    return f;
}

InputFrame Look(float dx, float dy) {
    InputFrame f;
    f.lookDx = dx;
    f.lookDy = dy;
    return f;
}

InputFrame Interact() {
    InputFrame f;
    f.interactPressed = true;
    return f;
}

InputFrame LampKey() {
    InputFrame f;
    f.lampPressed = true;
    return f;
}

// Takes the lamp from the floor socket at the start pose (looks down, E, looks up).
void TakeLamp(World& w) {
    Run(w, Look(0.0f, -0.11f), 10);
    Run(w, Interact(), 1);
    Run(w, Look(0.0f, 0.11f), 10);
}

// A committed replay run on the CPU with its world-state checks, exactly as --simulate-only does.
struct SimulatedReplay {
    lc::game::Replay replay;
    std::unique_ptr<World> world;
    std::unique_ptr<lc::game::StateCheckLog> checks;
    std::uint64_t ticks = 0;

    explicit SimulatedReplay(const char* file) {
        std::string error;
        const auto loaded = lc::game::Replay::Load(std::filesystem::path(LC_REPLAY_DIR) / file, error);
        if (!loaded) {
            lc::test::Fail(__FILE__, __LINE__, "cannot load replay: " + error);
            throw lc::test::RequireFailed{};
        }
        replay = *loaded;
        world = std::make_unique<World>(lc::BuildTwoRoomLevel(), replay.hunt ? ThreatBehaviour::Hunt : ThreatBehaviour::Patrol);
        checks = std::make_unique<lc::game::StateCheckLog>(replay);
        lc::game::Simulation simulation(replay.tickRate);
        simulation.RunTicks(static_cast<std::uint32_t>(replay.LastTick() + 1), [&](std::uint64_t tick, float dt) {
            world->Tick(replay.InputAt(tick), dt);
            checks->AfterTick(*world, tick + 1);
        });
        ticks = simulation.Tick();
    }
};

}  // namespace

namespace {

std::string ReadText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t pos = text.find(from);
    LC_REQUIRE(pos != std::string::npos);
    return text.replace(pos, from.size(), to);
}

// The two-room document with M6 mechanics grafted on: a fuse item that hides when carried, a fuse box
// and an exit panel that accept it, circuit b powered by the fuse in the panel, a fan on circuit b,
// and the door locked until circuit b comes on.
lc::TwoRoomLevel LevelWithFuseFanAndLockedDoor() {
    std::string text = ReadText(lc::AssetRoot() / lc::kTwoRoomSceneFile);
    text = Replace(text, "{\"id\": \"b\", \"on\": false}", "{\"id\": \"b\", \"on\": false, \"poweredBy\": {\"item\": \"fuse\", \"socket\": \"exit_panel\"}}");
    text = Replace(text, "{\"id\": \"door\", \"object\": \"door\"}", "{\"id\": \"door\", \"object\": \"door\", \"locked\": true, \"opensWithCircuit\": \"b\"}");
    const std::string shelf = "{\"id\": \"shelf\", \"position\": [4.25, 0.95, 10.25], \"yaw\": -1.5707963, \"accepts\": [\"lamp\"], \"text\": \"shelf\"}";
    text = Replace(text, shelf,
                   shelf + ",\n    {\"id\": \"fuse_box\", \"position\": [3.3, 1.0, 0.5], \"accepts\": [\"fuse\"], \"text\": \"fuse box\"},\n"
                           "    {\"id\": \"exit_panel\", \"position\": [3.3, 1.0, 1.2], \"accepts\": [\"fuse\"], \"text\": \"exit panel\"}");
    const std::string lampItem = "\"startSocket\": \"floor_a\"}";
    text = Replace(text, lampItem, lampItem + ",\n      {\"id\": \"fuse\", \"text\": \"fuse\", \"object\": \"fuse_body\", \"startSocket\": \"fuse_box\", \"hidesWhenCarried\": true}");
    const std::string crate = "{\"id\": \"crate\", \"kind\": \"box\", \"half\": [0.4, 0.4, 0.4], \"centre\": [0.8, 0.4, 3.0], \"material\": \"crate\"}";
    text = Replace(text, crate,
                   crate + ",\n    {\"id\": \"fuse_body\", \"kind\": \"box\", \"half\": [0.03, 0.05, 0.02], \"material\": \"crate\", \"collider\": false},\n"
                           "    {\"id\": \"fan\", \"kind\": \"fan\", \"centre\": [2.0, 2.3, 3.5], \"axis\": [0, 0, 1], \"radius\": 0.4, \"blades\": 3, \"bladeWidth\": 0.12, "
                           "\"bladeThickness\": 0.02, \"hubRadius\": 0.06, \"rpm\": 60, \"circuit\": \"b\", \"material\": \"crate\"}");
    lc::SceneFileResult r = lc::ParseSceneFile(text, "fuse_fan_door");
    for (const std::string& e : r.errors) std::printf("    error: %s\n", e.c_str());
    LC_REQUIRE(r.level.has_value());
    return std::move(*r.level);
}

bool EmitterOn(const World& w, const std::string& material) {
    for (const lc::Material& m : w.GetScene().Materials()) {
        if (m.name == material) return m.emitterOn;
    }
    return false;
}

}  // namespace

LC_TEST(powered_circuit_fan_locked_door_and_pocketed_item_follow_the_fuse) {
    World w(LevelWithFuseFanAndLockedDoor(), ThreatBehaviour::Patrol);
    LC_REQUIRE(w.Items().size() == 2u);
    LC_REQUIRE(w.Fans().size() == 1u);
    const lc::game::Item* fuse = w.FindItem("fuse");
    LC_REQUIRE(fuse != nullptr);
    LC_CHECK(fuse->HidesWhenCarried());
    LC_CHECK(!w.CircuitOn("b"));
    LC_CHECK(!EmitterOn(w, "fixture_b"));
    LC_CHECK(!w.Fans()[0].Turning());
    LC_CHECK(w.GetDoor().Locked());
    // The locked door straight ahead is never offered; the fuse box beside the line of sight is:
    // the prompt names the fuse, E takes it, and it hides in the torso.
    const auto target = w.CurrentInteraction();
    LC_REQUIRE(target.has_value());
    LC_CHECK_EQ(target->name, std::string("fuse"));
    LC_CHECK_EQ(target->text, std::string("Take the fuse"));
    Run(w, Interact(), 1);
    LC_CHECK(w.GetDoor().State() == lc::game::DoorState::Closed);
    LC_CHECK(fuse->State() == lc::game::ItemState::Held);
    w.WriteRenderScene(w.GetScene(), 1.0f);
    const lc::Instance* body = w.GetScene().FindInstance(w.Level().items[1].body);
    LC_REQUIRE(body != nullptr);
    const Vec3 torso = lc::PlayerTorsoTransform({w.GetPlayer().Current().position, w.GetPlayer().Current().yaw, 0.0f}).TranslationPart();
    LC_CHECK(lc::math::Length(body->objectToWorld.TranslationPart() - torso) < 0.3f);
    LC_CHECK(!w.CircuitOn("b"));
    // The exit panel is straight ahead: E inserts the fuse; circuit b comes on, the fixture lights, the
    // fan spins up, and the locked door opens by itself.
    const auto panel = w.CurrentInteraction();
    LC_REQUIRE(panel.has_value());
    LC_CHECK_EQ(panel->name, std::string("exit_panel"));
    LC_CHECK_EQ(panel->text, std::string("Put the fuse in the exit panel"));
    Run(w, Interact(), 1);
    LC_CHECK(fuse->State() == lc::game::ItemState::Placed);
    LC_CHECK_EQ(fuse->SocketName(), std::string("exit_panel"));
    LC_CHECK(w.CircuitOn("b"));
    LC_CHECK(EmitterOn(w, "fixture_b"));
    LC_CHECK(w.GetDoor().State() == lc::game::DoorState::Opening);
    Run(w, InputFrame{}, 90);  // 1.5 s: half speed; the door reached open.
    LC_CHECK(w.Fans()[0].Turning());
    LC_CHECK(w.Fans()[0].SpeedFraction() > 0.4f && w.Fans()[0].SpeedFraction() < 0.6f);
    LC_CHECK(w.GetDoor().State() == lc::game::DoorState::Open);
    Run(w, InputFrame{}, 120);
    LC_CHECK_NEAR(w.Fans()[0].SpeedFraction(), 1.0f, 1e-4f);
    // The blades turn: their transforms change every tick.
    const lc::Instance* blade = w.GetScene().FindInstance(w.Level().fans[0].blades[0]);
    LC_REQUIRE(blade != nullptr);
    w.WriteRenderScene(w.GetScene(), 1.0f);
    const std::uint32_t revision = blade->transformRevision;
    Run(w, InputFrame{}, 1);
    w.WriteRenderScene(w.GetScene(), 1.0f);
    LC_CHECK(blade->transformRevision > revision);
    // Taking the fuse back darkens the circuit and the fan spins down to a stop; the door stays open.
    Run(w, Interact(), 1);
    LC_CHECK(fuse->State() == lc::game::ItemState::Held);
    LC_CHECK(!w.CircuitOn("b"));
    LC_CHECK(!EmitterOn(w, "fixture_b"));
    Run(w, InputFrame{}, 90);
    LC_CHECK(w.Fans()[0].Turning());
    Run(w, InputFrame{}, 120);
    LC_CHECK(!w.Fans()[0].Turning());
    LC_CHECK(w.GetDoor().State() == lc::game::DoorState::Open);
    // The state hash covers the circuit: an identical run agrees, a run without the fuse move differs.
    World again(LevelWithFuseFanAndLockedDoor(), ThreatBehaviour::Patrol);
    Run(again, Interact(), 1);
    Run(again, Interact(), 1);
    World other(LevelWithFuseFanAndLockedDoor(), ThreatBehaviour::Patrol);
    Run(other, InputFrame{}, 2);
    LC_CHECK(again.StateHash() != other.StateHash());
    LC_CHECK(again.CircuitOn("b") && !other.CircuitOn("b"));
}

LC_TEST(replay_t15_route_completes_without_a_catch_and_is_deterministic) {
    SimulatedReplay a("t15_route.json");
    LC_CHECK(a.replay.hunt);
    for (const std::string& line : a.checks->Lines()) {
        if (line.find("FAIL") != std::string::npos) lc::test::Fail(__FILE__, __LINE__, line);
    }
    LC_CHECK_EQ(a.checks->Failed(), 0u);
    LC_CHECK_EQ(a.checks->Pending(), 0u);
    LC_CHECK(a.checks->Evaluated() >= 8u);
    LC_CHECK(a.world->Phase() == "escaped");
    LC_CHECK(a.world->Complete());
    LC_CHECK_EQ(a.world->Catches(), 0u);
    LC_CHECK(a.world->GetLamp().State() == lc::game::LampState::Held);
    SimulatedReplay b("t15_route.json");
    LC_CHECK_EQ(a.world->StateHash(), b.world->StateHash());
    LC_CHECK_EQ(a.ticks, 1481u);
}

LC_TEST(replay_t15_catch_restarts_from_the_checkpoint_and_still_completes) {
    SimulatedReplay run("t15_catch_restart.json");
    for (const std::string& line : run.checks->Lines()) {
        if (line.find("FAIL") != std::string::npos) lc::test::Fail(__FILE__, __LINE__, line);
    }
    LC_CHECK_EQ(run.checks->Failed(), 0u);
    LC_CHECK_EQ(run.checks->Pending(), 0u);
    LC_CHECK_EQ(run.world->Catches(), 1u);
    LC_CHECK_EQ(run.world->Restarts(), 1u);
    LC_CHECK(run.world->Phase() == "escaped");
    // Two different runs never share a hash (the catch is part of the state).
    SimulatedReplay route("t15_route.json");
    LC_CHECK(run.world->StateHash() != route.world->StateHash());
}

LC_TEST(objective_phases_advance_with_the_lamp_and_the_exit_and_save_checkpoints) {
    World w(lc::BuildTwoRoomLevel(), ThreatBehaviour::Patrol);
    LC_CHECK(w.Phase() == "introduction");
    LC_CHECK_EQ(w.ObjectiveLine(), std::string("Find the lamp and pick it up"));
    LC_CHECK(w.Level().hasExit);
    LC_CHECK_NEAR(w.Level().exitRadius, 0.8f, 1e-6);
    TakeLamp(w);
    LC_CHECK(w.Phase() == "lamp_acquired");
    LC_CHECK_EQ(w.CompletedSteps(), 1u);
    LC_CHECK_EQ(w.LastCheckpoint().completedSteps, 1u);
    LC_REQUIRE(w.LastCheckpoint().items.size() == 1u);
    LC_CHECK(w.LastCheckpoint().items[0].state == lc::game::ItemState::Held);
    // Placing on the floor socket again is not the shelf: no advance.
    Run(w, Look(0.0f, -0.11f), 10);
    Run(w, Interact(), 1);  // The floor socket is in reach: the lamp goes back down.
    LC_CHECK(w.GetLamp().State() == lc::game::LampState::Placed);
    LC_CHECK(w.Phase() == "lamp_acquired");
    Run(w, Interact(), 1);  // Pick it up again.
    Run(w, Look(0.0f, 0.11f), 10);
    LC_CHECK(w.GetLamp().State() == lc::game::LampState::Held);
    LC_CHECK(w.Phase() == "lamp_acquired");
    // Walk the t06 route to the shelf and place the lamp.
    Run(w, Interact(), 1);  // Door.
    Run(w, InputFrame{}, 60);
    Run(w, Move(0.0f, 1.0f), 172);
    Run(w, Look(-0.15707963f, 0.0f), 10);
    Run(w, Move(0.0f, 1.0f), 364);
    Run(w, Move(1.0f, 0.0f), 64);
    Run(w, Look(-0.15707963f, 0.0f), 10);
    Run(w, Look(0.0f, -0.10f), 10);
    Run(w, Interact(), 1);
    LC_REQUIRE(w.GetLamp().SocketName() == w.Level().shelfSocket.name);
    LC_CHECK(w.Phase() == "lamp_placed");
    LC_CHECK_EQ(w.LastCheckpoint().completedSteps, 2u);
    Run(w, Interact(), 1);  // Take it back.
    LC_CHECK(w.Phase() == "lamp_retrieved");
    LC_CHECK_EQ(w.ObjectiveLine(), std::string("Return to the exit in the equipment room with the lamp"));
    // Teleporting the player is not possible; walking back is the replay's job. The exit rule itself:
    // the phase only completes with the lamp held inside the radius (checked through a fresh world).
    World e(lc::BuildTwoRoomLevel(), ThreatBehaviour::Patrol);
    TakeLamp(e);
    Run(e, Look(lc::math::kPi, 0.0f), 1);   // Face -X, toward the exit corner.
    Run(e, Move(-1.0f, 1.0f), 200);          // Diagonal toward (0.9, 0.9): blocked by the walls, ends in the corner.
    LC_CHECK(e.Phase() == "lamp_acquired");  // Not retrieved yet: reaching the exit means nothing.
}

LC_TEST(threat_hunts_on_gameplay_data_and_a_catch_restarts_from_the_checkpoint) {
    World w(lc::BuildTwoRoomLevel(), ThreatBehaviour::Hunt);
    LC_CHECK(w.GetThreat().Behaviour() == ThreatBehaviour::Hunt);
    LC_CHECK(w.GetThreat().State() == ThreatState::Patrol);
    // Behind the closed door with the lamp on: the wall and the leaf block the line of sight.
    TakeLamp(w);
    Run(w, InputFrame{}, 120);
    LC_CHECK(w.GetThreat().State() == ThreatState::Patrol);
    LC_CHECK_EQ(w.Catches(), 0u);
    // Open the door and walk into the doorway with the lamp on: seen within 8 m, chased, caught.
    Run(w, Interact(), 1);
    Run(w, InputFrame{}, 62);
    bool chased = false;
    int ticksUntilCatch = 0;
    while (w.Catches() == 0 && ticksUntilCatch < 900) {
        Run(w, ticksUntilCatch < 80 ? Move(0.0f, 1.0f) : InputFrame{}, 1);  // 2 m toward the door, then stand.
        chased = chased || w.GetThreat().State() == ThreatState::Chase;
        ++ticksUntilCatch;
    }
    LC_CHECK(chased);
    LC_REQUIRE(w.Catches() == 1u);
    LC_CHECK_EQ(w.Restarts(), 1u);
    // Restart: the checkpoint is the lamp pick-up (phase kept), the player is back at that pose, the
    // door closed again, the threat on its path.
    LC_CHECK(w.Phase() == "lamp_acquired");
    const Vec3 feet = w.GetPlayer().Current().position;
    LC_CHECK_NEAR(feet.x, w.LastCheckpoint().player.position.x, 1e-6);
    LC_CHECK_NEAR(feet.z, w.LastCheckpoint().player.position.z, 1e-6);
    LC_CHECK(w.GetDoor().State() == lc::game::DoorState::Closed);
    LC_CHECK(w.GetLamp().State() == lc::game::LampState::Held);
    LC_CHECK(w.GetThreat().State() == ThreatState::Patrol);
    LC_CHECK_NEAR(w.GetThreat().Distance(), 0.0f, 1e-6);
    LC_CHECK(!w.Colliders().CapsuleOverlaps(feet, lc::game::Player::kCapsule));
}

LC_TEST(threat_detection_needs_range_facing_and_a_clear_line) {
    World w(lc::BuildTwoRoomLevel(), ThreatBehaviour::Hunt);
    TakeLamp(w);
    Run(w, Interact(), 1);  // Open the door.
    Run(w, InputFrame{}, 62);
    // Lamp off in the doorway: the dark range is 2.5 m, the machine passes within it only near the door end.
    Run(w, LampKey(), 1);
    LC_CHECK(!w.GetLamp().IsOn());
    Run(w, Move(0.0f, 1.0f), 40);  // Just inside the doorway (x ~ 3.5).
    int seenTicks = 0;
    for (int i = 0; i < 300; ++i) {
        Run(w, InputFrame{}, 1);
        if (w.GetThreat().SeesPlayer()) ++seenTicks;
    }
    // With the lamp off the player 1.1 m inside Room A is beyond 2.5 m from the path for most of the
    // round; a full lit range would have found them at once.
    World lit(lc::BuildTwoRoomLevel(), ThreatBehaviour::Hunt);
    TakeLamp(lit);
    Run(lit, Interact(), 1);
    Run(lit, InputFrame{}, 62);
    Run(lit, Move(0.0f, 1.0f), 40);
    int litSeen = 0;
    for (int i = 0; i < 300; ++i) {
        Run(lit, InputFrame{}, 1);
        if (lit.GetThreat().SeesPlayer()) ++litSeen;
        if (lit.Catches() > 0) break;
    }
    LC_CHECK(litSeen > seenTicks);
    LC_CHECK(litSeen > 0);
}

LC_TEST(state_hash_depends_only_on_the_inputs) {
    World a(lc::BuildTwoRoomLevel(), ThreatBehaviour::Hunt);
    World b(lc::BuildTwoRoomLevel(), ThreatBehaviour::Hunt);
    TakeLamp(a);
    TakeLamp(b);
    Run(a, Move(0.0f, 1.0f), 30);
    Run(b, Move(0.0f, 1.0f), 30);
    LC_CHECK_EQ(a.StateHash(), b.StateHash());
    Run(a, Move(0.0f, 1.0f), 1);
    Run(b, Move(1.0f, 0.0f), 1);
    LC_CHECK(a.StateHash() != b.StateHash());
    // The hash covers the tick count: an extra idle tick changes it.
    World c(lc::BuildTwoRoomLevel(), ThreatBehaviour::Hunt);
    World d(lc::BuildTwoRoomLevel(), ThreatBehaviour::Hunt);
    Run(c, InputFrame{}, 5);
    Run(d, InputFrame{}, 6);
    LC_CHECK(c.StateHash() != d.StateHash());
}

LC_TEST(state_checks_read_the_world_after_their_tick) {
    lc::game::Replay replay;
    lc::game::ReplayCheck phase;
    phase.tick = 21;
    phase.kind = "objective_state";
    phase.state = "lamp_acquired";
    lc::game::ReplayCheck threat;
    threat.tick = 21;
    threat.kind = "threat_state";
    threat.state = "chase";  // Wrong on purpose.
    lc::game::ReplayCheck caught;
    caught.tick = 21;
    caught.kind = "caught_count";
    caught.count = 0;
    lc::game::ReplayCheck near;
    near.tick = 21;
    near.kind = "player_near";
    near.point = Vec3{2.5f, 0.0f, 1.2f};
    near.tolerance = 0.05f;
    lc::game::ReplayCheck later;
    later.tick = 5000;
    later.kind = "caught_count";
    replay.checks = {phase, threat, caught, near, later};
    World w(lc::BuildTwoRoomLevel(), ThreatBehaviour::Hunt);
    lc::game::StateCheckLog log(replay);
    std::uint64_t ticks = 0;
    auto step = [&](const InputFrame& input, int n) {
        for (int i = 0; i < n; ++i) {
            w.Tick(input, 1.0f / 60.0f);
            log.AfterTick(w, ++ticks);
        }
    };
    step(Look(0.0f, -0.11f), 10);
    step(Interact(), 1);
    step(Look(0.0f, 0.11f), 10);
    LC_CHECK_EQ(log.Evaluated(), 4u);
    LC_CHECK_EQ(log.Failed(), 1u);
    LC_CHECK_EQ(log.Pending(), 1u);
    LC_CHECK_EQ(log.Lines().size(), 4u);
    LC_CHECK(log.Lines()[0].find("PASS") != std::string::npos);
    LC_CHECK(log.Lines()[1].find("FAIL") != std::string::npos);
}
