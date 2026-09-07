#include "lc_test.h"

#include "game/collision.h"
#include "game/world.h"
#include "scene/two_room_level.h"

#include <cmath>

namespace {

using lc::game::InputFrame;
using lc::game::World;
using lc::math::Vec3;

void Run(World& world, const InputFrame& input, int ticks) {
    for (int i = 0; i < ticks; ++i) world.Tick(input, 1.0f / 60.0f);
}

InputFrame Move(float x, float z) {
    InputFrame f;
    f.moveX = x;
    f.moveZ = z;
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

}  // namespace

LC_TEST(collision_boxes_come_from_the_kit_parts_and_resolve_circles) {
    const lc::TwoRoomLevel level = lc::BuildTwoRoomLevel();
    lc::game::CollisionWorld cw;
    cw.Build(level.description.scene, level.colliders);
    LC_CHECK_EQ(cw.Skipped(), 0u);
    LC_CHECK(cw.Boxes().size() >= 20);
    bool floorFound = false;
    for (const lc::game::ColliderBox& b : cw.Boxes()) {
        if (b.id.value == level.hallFloorId.value) {
            floorFound = true;
            LC_CHECK_NEAR(b.half.y, 0.075f, 1e-5f);
            LC_CHECK_NEAR(b.centre.y, -0.075f, 1e-5f);
            LC_CHECK_NEAR(b.yaw, 0.0f, 1e-6f);
        }
    }
    LC_CHECK(floorFound);

    // A synthetic box turned by 90 degrees: its thin axis runs along world X after the yaw.
    lc::game::CollisionWorld synthetic;
    lc::game::ColliderBox box;
    box.centre = {0.0f, 1.0f, 0.0f};
    box.half = {1.0f, 1.0f, 0.1f};
    box.yaw = lc::math::kPi * 0.5f;
    synthetic.AddBox(box);
    const lc::game::Capsule capsule{0.3f, 1.7f, 0.05f};
    const Vec3 moved = synthetic.MoveCapsule({0.5f, 0.0f, 0.0f}, {-0.6f, 0.0f, 0.0f}, capsule);
    LC_CHECK_NEAR(moved.x, 0.4f, 1e-4f);  // Pushed back to the +X face (0.1) plus the radius.
    LC_CHECK_NEAR(moved.z, 0.0f, 1e-5f);
    LC_CHECK(!synthetic.SegmentClear({0.5f, 1.0f, 0.0f}, {-0.5f, 1.0f, 0.0f}));
    LC_CHECK(synthetic.SegmentClear({0.5f, 1.0f, 3.0f}, {-0.5f, 1.0f, 3.0f}));  // Beyond the box's extent along Z.
    LC_CHECK(synthetic.SegmentClear({0.5f, 4.0f, 0.0f}, {-0.5f, 4.0f, 0.0f}));  // Above it.
    LC_CHECK(synthetic.SphereOverlaps({0.3f, 1.0f, 0.0f}, 0.25f));
    LC_CHECK(!synthetic.SphereOverlaps({0.4f, 1.0f, 0.0f}, 0.25f));
    const Vec3 swept = synthetic.SweepSphere({2.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.25f);
    LC_CHECK_NEAR(swept.x, 0.375f, 1e-4f);  // The first free sample moving back from the target.
}

LC_TEST(capsule_stops_at_walls_and_slides_along_them) {
    World w(lc::BuildTwoRoomLevel());
    Run(w, Look(lc::math::kPi, 0.0f), 1);  // Yaw -pi/2 + pi = pi/2: facing -X.
    Run(w, Move(0.0f, 1.0f), 180);          // 3 s at 1.5 m/s would end at x = -2 without walls.
    const Vec3 p = w.GetPlayer().Current().position;
    LC_CHECK(p.x >= lc::game::Player::kCapsule.radius - 1e-3f);
    LC_CHECK(p.x < 0.35f);
    LC_CHECK(!w.Colliders().CapsuleOverlaps(p, lc::game::Player::kCapsule));
    // A diagonal push into the wall slides along it: the -Z part of the motion survives.
    const float zBefore = p.z;
    Run(w, Move(1.0f, 1.0f), 60);
    const Vec3 q = w.GetPlayer().Current().position;
    LC_CHECK(q.x < 0.35f);
    LC_CHECK(zBefore - q.z > 0.5f);
    LC_CHECK(q.z >= lc::game::Player::kCapsule.radius - 1e-3f);  // Stopped by the -Z wall as well.
}

LC_TEST(closed_door_blocks_and_open_door_passes) {
    World w(lc::BuildTwoRoomLevel());
    Run(w, Move(0.0f, 1.0f), 120);  // Toward the door (+X) for 2 s.
    const Vec3 blocked = w.GetPlayer().Current().position;
    LC_CHECK(blocked.x > 3.6f);
    LC_CHECK(blocked.x < 3.75f);  // Leaf face at 4.005 minus the capsule radius.
    LC_CHECK(w.GetDoor().State() == lc::game::DoorState::Closed);
    Run(w, Look(0.0f, -0.6f), 1);  // Look at the leaf's centre (below eye level this close).
    Run(w, Interact(), 1);
    Run(w, Look(0.0f, 0.6f), 1);
    Run(w, InputFrame{}, 72);
    LC_CHECK(w.GetDoor().State() == lc::game::DoorState::Open);
    Run(w, Move(0.0f, 1.0f), 120);
    LC_CHECK(w.GetPlayer().Current().position.x > 4.5f);  // Into the hall.
    LC_CHECK(!w.Colliders().CapsuleOverlaps(w.GetPlayer().Current().position, lc::game::Player::kCapsule));
}

LC_TEST(held_lamp_is_kept_out_of_walls) {
    World w(lc::BuildTwoRoomLevel());
    Run(w, Look(0.0f, -0.11f), 10);
    Run(w, Interact(), 1);
    LC_REQUIRE(w.GetLamp().State() == lc::game::LampState::Held);
    Run(w, Look(0.0f, 0.11f), 10);
    Run(w, Look(lc::math::kPi, 0.0f), 1);   // Face the -X wall.
    Run(w, Move(0.0f, 1.0f), 180);           // Walk into it; the held pose would put the housing inside the wall.
    const Vec3 lamp = w.GetLamp().Current().position;
    const Vec3 housing = lamp + Vec3{0.0f, lc::game::Lamp::kHousingCentreHeight, 0.0f};
    LC_CHECK(housing.x >= lc::game::Lamp::kHousingRadius - 1e-3f);
    LC_CHECK(!w.Colliders().SphereOverlaps(housing, lc::game::Lamp::kHousingRadius));
    LC_CHECK(lamp.x < w.GetPlayer().Current().position.x);  // Still in front of the player, just closer.
}

LC_TEST(closing_door_swings_back_when_it_meets_the_player) {
    World w(lc::BuildTwoRoomLevel());
    Run(w, Interact(), 1);
    Run(w, InputFrame{}, 72);
    LC_REQUIRE(w.GetDoor().State() == lc::game::DoorState::Open);
    Run(w, Move(0.0f, 1.0f), 56);            // Into the doorway (x ~ 3.9; the open leaf lies along +X at z ~ 0.75).
    LC_CHECK(w.GetPlayer().Current().position.x > 3.85f);
    Run(w, Look(0.0f, -0.12f), 10);          // Look down at the leaf's closed centre to interact with it.
    Run(w, Interact(), 1);
    LC_CHECK(w.GetDoor().State() == lc::game::DoorState::Closing);
    Run(w, InputFrame{}, 70);                // A full swing would take 60 ticks; the leaf meets the player first.
    LC_CHECK(w.DoorBlocks() >= 1u);
    LC_CHECK(w.GetDoor().State() != lc::game::DoorState::Closing);
    LC_CHECK(w.GetDoor().State() != lc::game::DoorState::Closed);
    LC_CHECK(!w.Colliders().CapsuleOverlaps(w.GetPlayer().Current().position, lc::game::Player::kCapsule));
}

LC_TEST(segments_respect_walls_and_the_door) {
    World w(lc::BuildTwoRoomLevel());
    const lc::game::CollisionWorld& cw = w.Colliders();
    LC_CHECK(!cw.SegmentClear({2.0f, 1.6f, 1.2f}, {5.0f, 1.6f, 1.2f}));  // Through the closed door.
    LC_CHECK(cw.SegmentClear({1.0f, 1.0f, 1.0f}, {3.0f, 1.0f, 3.0f}));    // Inside Room A.
    LC_CHECK(!cw.SegmentClear({2.0f, 1.6f, 2.0f}, {2.0f, 1.6f, 6.0f}));  // Through the +Z wall.
    Run(w, Interact(), 1);
    Run(w, InputFrame{}, 72);
    LC_REQUIRE(w.GetDoor().State() == lc::game::DoorState::Open);
    LC_CHECK(cw.SegmentClear({2.0f, 1.6f, 1.2f}, {5.0f, 1.6f, 1.2f}));   // The open doorway.
}
