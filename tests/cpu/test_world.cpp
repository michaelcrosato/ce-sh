#include "lc_test.h"

#include "cpu_raycast.h"
#include "game/world.h"
#include "scene/two_room_level.h"

#include <cmath>

using lc::math::Vec3;

namespace {

lc::game::InputFrame Forward() {
    lc::game::InputFrame f;
    f.moveZ = 1.0f;
    return f;
}

}  // namespace

LC_TEST(world_player_moves_along_its_facing_and_clamps_pitch) {
    lc::game::Player player;
    player.Reset({{0, 0, 0}, 0.0f, 0.0f});
    player.Tick(Forward(), 1.0f);
    LC_CHECK(lc::math::NearlyEqual(player.Current().position, {0, 0, -lc::game::Player::kWalkSpeed}, 1e-5f));
    lc::game::InputFrame turn;
    turn.lookDx = lc::math::kPi * 0.5f;  // Turn left: forward becomes -X.
    player.Tick(turn, 1.0f / 60.0f);
    lc::game::InputFrame sprint = Forward();
    sprint.sprint = true;
    player.Reset({{0, 0, 0}, lc::math::kPi * 0.5f, 0.0f});
    player.Tick(sprint, 0.5f);
    LC_CHECK(lc::math::NearlyEqual(player.Current().position, {-lc::game::Player::kSprintSpeed * 0.5f, 0, 0}, 1e-4f));
    lc::game::InputFrame up;
    up.lookDy = 10.0f;
    player.Tick(up, 1.0f / 60.0f);
    LC_CHECK_NEAR(player.Current().pitch, lc::game::Player::kMaxPitch, 1e-6);
    // Interpolation halfway between previous and current.
    player.Reset({{0, 0, 0}, 0.0f, 0.0f});
    player.Tick(Forward(), 1.0f);
    LC_CHECK_NEAR(player.At(0.5f).position.z, -lc::game::Player::kWalkSpeed * 0.5f, 1e-5);
    LC_CHECK_NEAR(player.CameraAt(1.0f).position.y, lc::game::Player::kEyeHeight, 1e-6);
}

LC_TEST(world_door_states_and_angle) {
    lc::TwoRoomLevel level = lc::BuildTwoRoomLevel();
    lc::game::Door door(level.door);
    LC_CHECK(door.State() == lc::game::DoorState::Closed);
    door.Tick(0.1f);
    LC_CHECK_NEAR(door.Angle(), 0.0f, 0.0f);
    door.Interact();
    LC_CHECK(door.State() == lc::game::DoorState::Opening);
    door.Tick(0.5f);
    LC_CHECK_NEAR(door.Angle(), lc::game::Door::kOpenAngle * 0.5f, 1e-5);
    LC_CHECK(door.IsMoving());
    LC_CHECK_NEAR(door.AngleAt(0.5f), lc::game::Door::kOpenAngle * 0.25f, 1e-5);  // Between previous (0) and current.
    door.Tick(0.6f);
    LC_CHECK(door.State() == lc::game::DoorState::Open);
    LC_CHECK_NEAR(door.Angle(), lc::game::Door::kOpenAngle, 1e-6);
    door.Interact();
    LC_CHECK(door.State() == lc::game::DoorState::Closing);
    door.Tick(0.25f);
    door.Interact();  // Reverse mid-swing.
    LC_CHECK(door.State() == lc::game::DoorState::Opening);
    door.Tick(2.0f);
    LC_CHECK(door.State() == lc::game::DoorState::Open);
}

LC_TEST(world_threat_walks_and_ping_pongs) {
    lc::game::Threat threat({{6.8f, 0, 3.0f}, {6.8f, 0, 7.5f}}, 1.5f);
    LC_CHECK_NEAR(threat.PathLength(), 4.5f, 1e-6);
    LC_CHECK(lc::math::NearlyEqual(threat.Current().position, {6.8f, 0, 3.0f}, 1e-6f));
    threat.Tick(1.0f);
    LC_CHECK(lc::math::NearlyEqual(threat.Current().position, {6.8f, 0, 4.5f}, 1e-5f));
    // Facing +Z while walking toward +Z: forward (-sin yaw, 0, -cos yaw) == (0, 0, 1) -> yaw = pi.
    LC_CHECK_NEAR(std::fabs(threat.Current().yaw), lc::math::kPi, 1e-5);
    LC_CHECK(lc::math::NearlyEqual(threat.At(0.5f).position, {6.8f, 0, 3.75f}, 1e-5f));
    threat.Tick(2.5f);  // Reaches the end (7.5) and turns back by 0.75.
    LC_CHECK(lc::math::NearlyEqual(threat.Current().position, {6.8f, 0, 6.75f}, 1e-4f));
    threat.Tick(10.0f);  // Bounces several times; stays on the path.
    LC_CHECK(threat.Current().position.z >= 3.0f - 1e-4f && threat.Current().position.z <= 7.5f + 1e-4f);
    threat.SetDistance(2.0f);
    LC_CHECK(lc::math::NearlyEqual(threat.Current().position, {6.8f, 0, 5.0f}, 1e-6f));
}

LC_TEST(world_lamp_follows_the_player_when_held_and_snaps_to_sockets) {
    lc::TwoRoomLevel level = lc::BuildTwoRoomLevel();
    LC_REQUIRE(!level.items.empty());
    lc::game::Item lamp(level.items[0], level.floorSocket);
    LC_CHECK(lamp.State() == lc::game::LampState::Placed);
    LC_CHECK(lc::math::NearlyEqual(lamp.Current().position, level.floorSocket.position, 0.0f));
    lc::game::Player player;
    player.Reset({{1, 0, 1}, 0.0f, 0.0f});
    lamp.PickUp();
    lamp.Tick(player);
    // Held: in front (-Z), right (+X), and below the eye.
    const lc::PoseSpec held = lamp.Current();
    LC_CHECK(held.position.z < 1.0f && held.position.x > 1.0f && held.position.y < 1.6f && held.position.y > 1.0f);
    lamp.Place(level.shelfSocket);
    LC_CHECK(lamp.State() == lc::game::LampState::Placed);
    LC_CHECK_EQ(lamp.SocketName(), std::string("shelf"));
    LC_CHECK(lc::math::NearlyEqual(lamp.PoseAt(0.5f).position, level.shelfSocket.position, 0.0f));
    lamp.Toggle(level.description.scene);
    LC_CHECK(!lamp.IsOn());
    LC_CHECK(!level.description.scene.Materials()[level.lampMaterial].emitterOn);
}

LC_TEST(world_ticks_and_writes_only_moving_transforms) {
    lc::game::World world(lc::BuildTwoRoomLevel());
    lc::Scene& scene = world.GetScene();
    const lc::Instance* threat = scene.FindInstance(world.Level().threatBody);
    LC_REQUIRE(threat != nullptr);
    const std::uint32_t threatRevision = threat->transformRevision;
    const lc::Instance* crate = scene.FindInstance(lc::InstanceId{scene.Instances()[2].id.value});
    const std::uint32_t crateRevision = crate->transformRevision;

    // Neutral input: only the threat moves. Alpha 1 renders the state at the new tick.
    world.Tick(lc::game::InputFrame{}, 1.0f / 60.0f);
    const bool changed = world.WriteRenderScene(scene, 1.0f);
    LC_CHECK(changed);
    LC_CHECK(scene.FindInstance(world.Level().threatBody)->transformRevision == threatRevision + 1);
    LC_CHECK_EQ(scene.FindInstance(crate->id)->transformRevision, crateRevision);
    const lc::math::Mat4 rendered = scene.FindInstance(world.Level().threatBody)->objectToWorld;
    scene.CommitRenderedFrame();
    LC_CHECK(lc::math::NearlyEqual(scene.FindInstance(world.Level().threatBody)->prevObjectToWorld, rendered, 0.0f));

    // Writing the same interpolated state again changes nothing.
    LC_CHECK(!world.WriteRenderScene(scene, 1.0f));
    // Alpha 0 is the previous tick's state: a different (older) pose, so it counts as motion too.
    LC_CHECK(world.WriteRenderScene(scene, 0.0f));

    // Interact with the door from the start position (facing it, 2 m away).
    const auto target = world.CurrentInteraction();
    LC_REQUIRE(target.has_value());
    LC_CHECK_EQ(target->name, std::string("door"));
    lc::game::InputFrame interact;
    interact.interactPressed = true;
    world.Tick(interact, 1.0f / 60.0f);
    LC_CHECK(world.GetDoor().State() == lc::game::DoorState::Opening);
    world.WriteRenderScene(scene, 1.0f);
    LC_CHECK(scene.FindInstance(world.Level().door.id)->transformRevision > 0);
    LC_CHECK_EQ(world.StableIdOf("threat_body"), world.Level().threatBody.value);
    LC_CHECK_EQ(world.StableIdOf("nothing"), 0u);
}

LC_TEST(world_two_room_level_geometry_and_mirror_identity) {
    const lc::TwoRoomLevel level = lc::BuildTwoRoomLevel();
    const lc::Scene& scene = level.description.scene;
    LC_CHECK(scene.TotalTriangles() < 1000);
    std::uint32_t emitters = 0;
    for (const lc::Instance& inst : scene.Instances()) {
        if (scene.Materials()[inst.materialIndex].IsActiveEmitter()) ++emitters;
    }
    LC_CHECK_EQ(emitters, 3u);  // Room A fixture, hall emergency, lamp; Room B's fixture is on an off circuit.

    // The static camera (mirror check position) sees the threat only through the mirror.
    const float aspect = 1280.0f / 720.0f;
    const lc::Camera& cam = level.description.camera;
    const auto uv = cam.ProjectToImage(aspect, level.mirrorCheckPoint);
    LC_REQUIRE(uv.has_value());
    const Vec3 dir = cam.RayDirection(aspect, uv->x, uv->y);
    const lc::test::CpuHit direct = lc::test::RaycastScene(scene, cam.position, dir, 0.0f, 100.0f);
    LC_REQUIRE(direct.hit);
    LC_CHECK_EQ(direct.stableId, level.mirror.value);
    const lc::test::CpuHit reflected = lc::test::RaycastThroughMirrors(scene, cam.position, dir);
    LC_REQUIRE(reflected.hit);
    LC_CHECK_EQ(reflected.stableId, level.threatBody.value);
    // Nothing in the direct view is the threat: sample a grid of camera rays (no mirror following).
    bool threatDirectlyVisible = false;
    for (int y = 0; y < 36; ++y) {
        for (int x = 0; x < 64; ++x) {
            const lc::test::CpuHit h = lc::test::RaycastScene(scene, cam.position, cam.RayDirection(aspect, (x + 0.5f) / 64.0f, (y + 0.5f) / 36.0f), 0.0f, 100.0f);
            if (h.hit && (h.stableId == level.threatBody.value || h.stableId == level.threatHead.value)) threatDirectlyVisible = true;
        }
    }
    LC_CHECK(!threatDirectlyVisible);

    // The door blocks light from Room A's fixture into the hall when closed, not when open.
    const Vec3 fixture{2.0f, 2.7f, 2.0f};
    const Vec3 hallPoint{4.6f, 0.2f, 1.2f};
    const lc::test::CpuHit closedHit = lc::test::RaycastScene(scene, fixture, lc::math::Normalize(hallPoint - fixture), 0.0f, 10.0f);
    LC_CHECK(closedHit.hit && closedHit.stableId == level.door.id.value);
    lc::TwoRoomLevel opened = lc::BuildTwoRoomLevel();
    opened.description.scene.SetTransform(opened.door.id, lc::DoorTransform(opened.door, lc::math::kPi * 0.5f));
    const lc::test::CpuHit openHit = lc::test::RaycastScene(opened.description.scene, fixture, lc::math::Normalize(hallPoint - fixture), 0.0f, 10.0f);
    LC_CHECK(openHit.hit && openHit.stableId != opened.door.id.value);
    LC_CHECK(lc::math::Length(openHit.position - hallPoint) < 0.35f);  // Reaches the hall floor near the point.

    // The open leaf stands in the hall along +X from the hinge.
    const lc::Instance* leaf = opened.description.scene.FindInstance(opened.door.id);
    const Vec3 leafCentre = leaf->objectToWorld.TranslationPart();
    LC_CHECK(leafCentre.x > 4.15f && std::fabs(leafCentre.z - 0.75f) < 0.1f);
}
