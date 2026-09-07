// Runs the committed replay scripts on the CPU (the simulation is deterministic and needs no GPU)
// and verifies the world state at every check tick with the CPU ray caster, so the GPU tests
// only have to confirm what the renderer sees.
#include "lc_test.h"

#include "cpu_raycast.h"
#include "game/replay.h"
#include "game/simulation.h"
#include "game/world.h"
#include "scene/two_room_level.h"

#include <cmath>
#include <filesystem>
#include <string>

#ifndef LC_REPLAY_DIR
#define LC_REPLAY_DIR "tests/replay"
#endif

using lc::math::Vec3;

namespace {

struct ReplayRun {
    lc::game::Replay replay;
    std::unique_ptr<lc::game::World> world;
    lc::game::Simulation simulation{60};

    explicit ReplayRun(const char* file) {
        std::string error;
        const auto loaded = lc::game::Replay::Load(std::filesystem::path(LC_REPLAY_DIR) / file, error);
        if (!loaded) {
            lc::test::Fail(__FILE__, __LINE__, "cannot load replay: " + error);
            throw lc::test::RequireFailed{};
        }
        replay = *loaded;
        world = std::make_unique<lc::game::World>(lc::BuildTwoRoomLevel());
    }

    void RunTo(std::uint64_t tick) {
        LC_REQUIRE(tick >= simulation.Tick());
        simulation.RunTicks(static_cast<std::uint32_t>(tick - simulation.Tick()), [&](std::uint64_t t, float dt) { world->Tick(replay.InputAt(t), dt); });
        world->WriteRenderScene(world->GetScene(), 1.0f);
    }

    lc::Camera CameraNow() const { return world->CameraAt(1.0f); }

    // First non-mirror hit id for the pixel that shows a world point.
    std::uint32_t HitIdThroughMirrors(Vec3 point) const {
        const lc::Camera cam = CameraNow();
        const float aspect = 1280.0f / 720.0f;
        const auto uv = cam.ProjectToImage(aspect, point);
        if (!uv) return 0;
        const lc::test::CpuHit h = lc::test::RaycastThroughMirrors(world->GetScene(), cam.position, cam.RayDirection(aspect, uv->x, uv->y));
        return h.hit ? h.stableId : 0;
    }

    bool DirectlyVisible(std::uint32_t id) const {
        const lc::Camera cam = CameraNow();
        const float aspect = 1280.0f / 720.0f;
        for (int y = 0; y < 45; ++y) {
            for (int x = 0; x < 80; ++x) {
                const lc::test::CpuHit h = lc::test::RaycastScene(world->GetScene(), cam.position, cam.RayDirection(aspect, (x + 0.5f) / 80.0f, (y + 0.5f) / 45.0f), 0.0f, 100.0f);
                if (h.hit && h.stableId == id) return true;
            }
        }
        return false;
    }

    bool PointOnScreen(Vec3 point) const {
        const auto uv = CameraNow().ProjectToImage(1280.0f / 720.0f, point);
        return uv && uv->x >= 0.0f && uv->x <= 1.0f && uv->y >= 0.0f && uv->y <= 1.0f;
    }
};

float WrapAngle(float a) {
    while (a > lc::math::kPi) a -= 2.0f * lc::math::kPi;
    while (a < -lc::math::kPi) a += 2.0f * lc::math::kPi;
    return a;
}

}  // namespace

LC_TEST(replay_t05_reaches_the_mirror_check_pose_and_sees_the_threat) {
    ReplayRun run("t05_mirror_threat.json");
    LC_CHECK_EQ(run.replay.checks.size(), std::size_t{4});
    run.RunTo(60);
    LC_CHECK(run.world->GetDoor().State() == lc::game::DoorState::Open);

    run.RunTo(740);
    const lc::game::PlayerPose pose = run.world->GetPlayer().Current();
    const lc::TwoRoomLevel& level = run.world->Level();
    LC_CHECK_NEAR(pose.position.x, level.mirrorCheckCamera.position.x, 0.05f);
    LC_CHECK_NEAR(pose.position.z, level.mirrorCheckCamera.position.z, 0.05f);
    LC_CHECK_NEAR(WrapAngle(pose.yaw - level.mirrorCheckCamera.yaw), 0.0f, 0.02f);
    const Vec3 threat = run.world->GetThreat().Current().position;
    LC_CHECK_NEAR(threat.x, level.threatCheckPosition.x, 0.05f);
    LC_CHECK_NEAR(threat.z, level.threatCheckPosition.z, 1e-4f);
    const Vec3 mirrorPoint{6.4f, 1.5f, 11.99f};
    LC_CHECK(run.PointOnScreen(mirrorPoint));
    LC_CHECK_EQ(run.HitIdThroughMirrors(mirrorPoint), level.threatBody.value);
    LC_CHECK(!run.DirectlyVisible(level.threatBody.value));
    LC_CHECK(!run.DirectlyVisible(level.threatHead.value));

    run.RunTo(930);
    const Vec3 threatFar = run.world->GetThreat().Current().position;
    LC_CHECK_NEAR(threatFar.x, level.threatPath[0].x, 0.05f);
    LC_CHECK_EQ(run.HitIdThroughMirrors(mirrorPoint), run.world->StableIdOf("a_h1_wall_neg_z"));
    // The player did not move after tick 669.
    LC_CHECK_NEAR(run.world->GetPlayer().Current().position.x, pose.position.x, 1e-5f);
}

LC_TEST(replay_t06_door_opens_lights_the_hall_and_closes) {
    ReplayRun run("t06_door_light.json");
    run.RunTo(300);
    LC_CHECK(run.world->GetDoor().State() == lc::game::DoorState::Open);
    const lc::game::PlayerPose pose = run.world->GetPlayer().Current();
    LC_CHECK_NEAR(pose.position.x, 5.6f, 0.05f);
    LC_CHECK_NEAR(pose.position.z, 1.2f, 0.05f);
    LC_CHECK_NEAR(WrapAngle(pose.yaw - lc::math::kPi * 0.5f), 0.0f, 0.02f);  // Facing -X.
    const Vec3 hallPatch{4.6f, 0.0f, 1.2f};
    LC_CHECK(run.PointOnScreen(hallPatch));
    // The patch is lit directly by Room A's fixture through the open doorway: the segment is clear.
    const Vec3 fixture{2.0f, 2.79f, 2.0f};
    const lc::test::CpuHit toFixture = lc::test::RaycastScene(run.world->GetScene(), hallPatch + Vec3{0, 0.01f, 0}, lc::math::Normalize(fixture - hallPatch), 0.0f, 100.0f);
    LC_CHECK(toFixture.hit && toFixture.stableId == run.world->StableIdOf("fixture_a"));
    LC_CHECK_EQ(run.HitIdThroughMirrors({4.5f, 1.0f, 0.75f}), run.world->Level().door.id.value);
    LC_CHECK(run.world->CurrentInteraction().has_value() && run.world->CurrentInteraction()->name == "door");

    run.RunTo(420);
    LC_CHECK(run.world->GetDoor().State() == lc::game::DoorState::Closed);
    const lc::test::CpuHit blocked = lc::test::RaycastScene(run.world->GetScene(), hallPatch + Vec3{0, 0.01f, 0}, lc::math::Normalize(fixture - hallPatch), 0.0f, 100.0f);
    LC_CHECK(blocked.hit && blocked.stableId == run.world->Level().door.id.value);
    LC_CHECK_EQ(run.HitIdThroughMirrors({4.01f, 1.0f, 1.2f}), run.world->Level().door.id.value);
}

LC_TEST(replay_t06_lamp_is_carried_to_the_shelf_and_switched_off) {
    ReplayRun run("t06_lamp_shelf.json");
    run.RunTo(11);
    LC_CHECK(run.world->GetLamp().State() == lc::game::LampState::Held);
    run.RunTo(22);
    LC_CHECK(run.world->GetDoor().State() == lc::game::DoorState::Opening);
    run.RunTo(800);
    LC_CHECK(run.world->GetLamp().State() == lc::game::LampState::Placed);
    LC_CHECK_EQ(run.world->GetLamp().SocketName(), std::string("shelf"));
    LC_CHECK(run.world->GetLamp().IsOn());
    const Vec3 floorPatch{4.7f, 0.0f, 10.25f};
    LC_CHECK(run.PointOnScreen(floorPatch));
    LC_CHECK(run.PointOnScreen({4.15f, 0.95f, 10.45f}));
    LC_CHECK_EQ(run.HitIdThroughMirrors({4.15f, 0.95f, 10.45f}), run.world->StableIdOf("lamp_shelf"));
    LC_CHECK_EQ(run.HitIdThroughMirrors({4.2f, 1.02f, 10.25f}), run.world->Level().lampHousing.value);
    // The lamp face (on the shelf, facing +X) has a clear line to the floor patch.
    const lc::Instance* face = run.world->GetScene().FindInstance(run.world->Level().lampFace);
    LC_REQUIRE(face != nullptr);
    const Vec3 facePos = face->objectToWorld.TranslationPart();
    const lc::test::CpuHit toFace = lc::test::RaycastScene(run.world->GetScene(), floorPatch + Vec3{0, 0.01f, 0}, lc::math::Normalize(facePos - floorPatch), 0.0f, 10.0f);
    LC_CHECK(toFace.hit && (toFace.stableId == run.world->Level().lampFace.value || toFace.stableId == run.world->Level().lampHousing.value));
    LC_CHECK(toFace.hit && toFace.stableId == run.world->Level().lampFace.value);

    run.RunTo(900);
    LC_CHECK(!run.world->GetLamp().IsOn());
    LC_CHECK(!run.world->GetScene().Materials()[run.world->Level().lampMaterial].emitterOn);
}
