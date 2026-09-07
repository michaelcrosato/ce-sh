// The six-room demo level (spec §5, §6): the measured layout is enclosed, the doors swing free, the
// budgets hold, the mirror aims through its door at the hall, the machine's route stays inside the
// halls, and every socket and marker stands on a floor inside a room.
#include "lc_test.h"

#include "cpu_raycast.h"
#include "game/collision.h"
#include "game/world.h"
#include "scene/scene_file.h"
#include "scene/two_room_level.h"

#include <cmath>
#include <set>

namespace {

using lc::math::Vec3;

lc::TwoRoomLevel LoadSixRoom() {
    const lc::SceneFileResult r = lc::LoadSceneFile("scenes/six_room.json", lc::AssetRoot());
    for (const std::string& e : r.errors) std::printf("    error: %s\n", e.c_str());
    LC_REQUIRE(r.level.has_value());
    return *r.level;
}

struct Room {
    const char* name;
    Vec3 min;  // Inner extents on the floor.
    Vec3 max;
};

// The measured layout of docs/superpowers/plans/2026-09-07-m6-six-room-demo.md.
const Room kRooms[] = {
    {"security", {0.0f, 0.0f, 12.55f}, {4.0f, 2.8f, 16.55f}},      {"equipment", {4.15f, 0.0f, 12.55f}, {8.15f, 2.8f, 17.55f}},
    {"inspection", {8.3f, 0.0f, 12.55f}, {12.3f, 2.8f, 16.55f}},   {"hall_a", {0.0f, 0.0f, 10.0f}, {16.4f, 2.8f, 12.4f}},
    {"exit_room", {0.0f, 0.0f, 5.85f}, {4.0f, 2.8f, 9.85f}},       {"vestibule", {1.0f, 0.0f, 3.3f}, {3.4f, 2.8f, 5.7f}},
    {"hall_b", {14.0f, 0.0f, 0.0f}, {16.4f, 2.8f, 10.0f}},         {"plant", {16.55f, 0.0f, 3.0f}, {22.55f, 2.8f, 9.0f}},
    {"switch_room", {14.0f, 0.0f, -5.15f}, {18.0f, 2.8f, -0.15f}},
};

// 62 directions spread over the sphere (a Fibonacci lattice).
std::vector<Vec3> SphereDirections() {
    std::vector<Vec3> dirs;
    const int n = 62;
    const float golden = 2.399963f;
    for (int i = 0; i < n; ++i) {
        const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / n;
        const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        const float phi = golden * static_cast<float>(i);
        dirs.push_back({r * std::cos(phi), y, r * std::sin(phi)});
    }
    return dirs;
}

}  // namespace

LC_TEST(six_room_loads_with_the_expected_entities) {
    const lc::TwoRoomLevel level = LoadSixRoom();
    const lc::Scene& s = level.description.scene;
    LC_CHECK_EQ(level.description.name, std::string("six_room"));
    LC_CHECK_EQ(level.doors.size(), std::size_t{7});
    LC_CHECK_EQ(level.items.size(), std::size_t{2});
    LC_CHECK_EQ(level.fans.size(), std::size_t{1});
    LC_CHECK_EQ(level.steps.size(), std::size_t{7});
    LC_CHECK_EQ(level.circuits.size(), std::size_t{7});
    LC_CHECK_EQ(level.sockets.size(), std::size_t{4});
    LC_CHECK_EQ(level.threatPath.size(), std::size_t{3});
    bool lockedExit = false;
    for (const lc::LevelDoor& d : level.doors) {
        if (d.id == "door_exit") lockedExit = d.locked && d.opensWithCircuit == "exit";
    }
    LC_CHECK(lockedExit);
    // Budgets (spec §6): placed objects, triangle instances, and the emitter count.
    LC_CHECK(s.Instances().size() < 500u);
    std::size_t triangles = 0;
    for (const lc::Instance& inst : s.Instances()) triangles += s.Meshes()[inst.mesh.value].data.TriangleCount();
    LC_CHECK(triangles < 100000u);
    std::printf("    six_room: %zu instances, %zu triangle instances, %zu meshes\n", s.Instances().size(), triangles, s.Meshes().size());
    std::size_t activeFixtures = 0;
    std::size_t fixtures = 0;
    for (const lc::Instance& inst : s.Instances()) {
        const lc::Material& m = s.Materials()[inst.materialIndex];
        if (m.type != lc::MaterialType::Emitter) continue;
        ++fixtures;
        if (m.emitterOn) ++activeFixtures;
    }
    LC_CHECK_EQ(fixtures, std::size_t{10});
    LC_CHECK(activeFixtures <= 8u);  // Spec §5: at most eight active emitter fixtures including the lamp.
    LC_CHECK_EQ(activeFixtures, std::size_t{8});
    // The walkable area of the measured layout stays within the spec's 160-220 square metres.
    float area = 0.0f;
    for (const Room& r : kRooms) area += (r.max.x - r.min.x) * (r.max.z - r.min.z);
    LC_CHECK(area >= 160.0f && area <= 220.0f);
    std::printf("    six_room: walkable area %.1f m^2\n", area);
}

LC_TEST(six_room_rooms_are_enclosed_and_doors_swing_free) {
    const lc::TwoRoomLevel level = LoadSixRoom();
    const lc::Scene& s = level.description.scene;
    // From the centre of every space, every direction meets a surface within 30 m: nothing escapes
    // to the black outside (the doors are closed at load).
    const std::vector<Vec3> dirs = SphereDirections();
    for (const Room& r : kRooms) {
        const Vec3 centre{(r.min.x + r.max.x) * 0.5f, 1.4f, (r.min.z + r.max.z) * 0.5f};
        int escaped = 0;
        for (const Vec3& d : dirs) {
            const lc::test::CpuHit h = lc::test::RaycastScene(s, centre, d, 0.0f, 30.0f);
            if (!h.hit) ++escaped;
        }
        if (escaped != 0) std::printf("    %s: %d of %zu rays escape\n", r.name, escaped, dirs.size());
        LC_CHECK_EQ(escaped, 0);
    }
    // Door leaves: at 0, 45, and 90 degrees the leaf's free edge touches no wall or furniture. The
    // solids exclude the leaves themselves.
    lc::game::CollisionWorld solids;
    std::vector<lc::InstanceId> staticIds;
    std::set<std::uint32_t> leafIds;
    for (const lc::LevelDoor& d : level.doors) leafIds.insert(d.handle.id.value);
    for (const lc::InstanceId id : level.colliders) {
        if (!leafIds.contains(id.value)) staticIds.push_back(id);
    }
    solids.Build(s, staticIds);
    for (const lc::LevelDoor& d : level.doors) {
        for (const float angle : {0.0f, 0.7854f, 1.5708f}) {
            const lc::math::Mat4 m = lc::DoorTransform(d.handle, angle);
            const lc::Instance* inst = s.FindInstance(d.handle.id);
            LC_REQUIRE(inst != nullptr);
            const lc::MeshData& mesh = s.Meshes()[inst->mesh.value].data;
            int touching = 0;
            for (const Vec3& p : mesh.positions) {
                const Vec3 w = m.TransformPoint(p);
                if (w.y < 0.05f || w.y > 2.05f) continue;  // Away from the floor and the lintel overlap.
                if (solids.SphereOverlaps(w, 0.01f)) ++touching;
            }
            if (touching != 0) std::printf("    %s at %.2f rad: %d leaf corners inside solids\n", d.id.c_str(), angle, touching);
            LC_CHECK_EQ(touching, 0);
        }
    }
}

LC_TEST(six_room_machine_routes_along_the_halls_around_the_corner) {
    lc::game::World world(LoadSixRoom(), lc::game::ThreatBehaviour::Hunt);
    const lc::game::CollisionWorld& solids = world.Colliders();
    lc::game::Threat& machine = world.GetThreat();
    // A clear line: the route is the target alone.
    const std::vector<Vec3> straight = machine.PlanRoute({15.2f, 0.0f, 8.0f}, {15.2f, 0.0f, 2.0f}, &solids);
    LC_CHECK_EQ(straight.size(), std::size_t{1});
    // From inside the Plant room's doorway (its door open) to Hall A around the Hall B corner: the route
    // leaves through the door line, follows Hall B to the corner vertex, then turns into Hall A.
    for (lc::game::Door& d : world.Doors()) {
        if (d.Id() == "door_plant") d.Restore(lc::game::DoorState::Open, lc::game::Door::kOpenAngle);
    }
    world.Tick(lc::game::InputFrame{}, 1.0f / 60.0f);  // Writes the door's collision transform.
    const Vec3 from{16.9f, 0.0f, 6.0f};  // Just inside the Plant room, in the doorway line.
    const Vec3 to{8.0f, 0.0f, 11.2f};
    LC_CHECK(!solids.SegmentClear(from + Vec3{0.0f, 0.6f, 0.0f}, to + Vec3{0.0f, 0.6f, 0.0f}));
    const std::vector<Vec3> route = machine.PlanRoute(from, to, &solids);
    LC_REQUIRE(route.size() >= 3u);
    LC_CHECK_NEAR(route[0].x, 15.2f, 1e-4f);  // Onto Hall B's line.
    LC_CHECK_NEAR(route[0].z, 6.0f, 1e-4f);
    bool corner = false;
    for (const Vec3& p : route) corner = corner || (std::fabs(p.x - 15.2f) < 1e-4f && std::fabs(p.z - 11.2f) < 1e-4f);
    LC_CHECK(corner);
    LC_CHECK_NEAR(route.back().x, to.x, 1e-6f);
    LC_CHECK_NEAR(route.back().z, to.z, 1e-6f);
    // Every leg is clear at body height.
    Vec3 previous = from;
    for (const Vec3& p : route) {
        LC_CHECK(solids.SegmentClear(previous + Vec3{0.0f, 0.6f, 0.0f}, p + Vec3{0.0f, 0.6f, 0.0f}));
        previous = p;
    }
    // Staged in the doorway, a return reaches the patrol line and resumes the round within a few
    // seconds instead of pushing against the corner.
    machine.Teleport(from);
    machine.BeginReturn();
    int ticks = 0;
    while (machine.State() != lc::game::ThreatState::Patrol && ticks < 60 * 20) {
        world.Tick(lc::game::InputFrame{}, 1.0f / 60.0f);
        ++ticks;
    }
    LC_CHECK(machine.State() == lc::game::ThreatState::Patrol);
    LC_CHECK(ticks < 60 * 10);
    const Vec3 at = machine.Current().position;
    LC_CHECK_NEAR(at.x, 15.2f, 0.05f);  // Back on Hall B's line.
    std::printf("    six_room: the return from the Plant doorway took %d ticks\n", ticks);
}

LC_TEST(six_room_mirror_route_sockets_and_markers_are_placed_correctly) {
    const lc::TwoRoomLevel level = LoadSixRoom();
    const lc::Scene& s = level.description.scene;
    // The mirror's normal bisects the check camera and the hall aim point (yaw only: the plane stays
    // vertical, so the mirror sits below eye height for the reflected ray to descend onto the machine's
    // body), and the reflected ray from the check camera reaches it through the inspection door's opening.
    const Vec3 centre{10.3f, 1.3f, 16.53f};
    const Vec3 eye = level.mirrorCheckCamera.position + Vec3{0.0f, 1.6f, 0.0f};
    const Vec3 aim = level.threatCheckPosition + Vec3{0.0f, 0.9f, 0.0f};
    const Vec3 expectedNormal = lc::math::Normalize(lc::math::Normalize(eye - centre) + lc::math::Normalize(aim - centre));
    LC_CHECK_NEAR(lc::math::Length(level.mirrorNormal + expectedNormal), 0.0f, 1e-5f);
    lc::game::World world(level, lc::game::ThreatBehaviour::Patrol);
    // Open the inspection door and park the machine at the hall aim point (8.8 m along its route); the
    // world writes both transforms.
    for (lc::game::Door& d : world.Doors()) {
        if (d.Id() == "door_inspection") {
            d.Restore(lc::game::DoorState::Open, lc::game::Door::kOpenAngle);
        }
    }
    world.GetThreat().SetDistance(level.threatCheckPosition.x - level.threatPath[0].x);
    world.WriteRenderScene(world.GetScene(), 1.0f);
    const Vec3 toMirror = lc::math::Normalize(level.mirrorCheckPoint - eye);
    const lc::test::CpuHit h = lc::test::RaycastThroughMirrors(world.GetScene(), eye, toMirror);
    LC_CHECK(h.hit);
    if (h.stableId != level.threatBody.value) {
        const lc::Instance* what = world.GetScene().FindInstance(lc::InstanceId{h.stableId});
        const lc::test::CpuHit direct = lc::test::RaycastScene(world.GetScene(), level.mirrorCheckPoint, lc::math::Normalize(aim - level.mirrorCheckPoint), 0.02f, 20.0f);
        const lc::Instance* blocker = world.GetScene().FindInstance(lc::InstanceId{direct.stableId});
        std::printf("    mirror ray ends on '%s' (t %.2f); the straight line from the mirror to the aim point meets '%s' (t %.2f)\n",
                    what != nullptr ? what->name.c_str() : "nothing", h.t, blocker != nullptr ? blocker->name.c_str() : "nothing", direct.t);
    }
    LC_CHECK_EQ(h.stableId, level.threatBody.value);  // The parked machine at the hall aim point.
    // The machine's route stays clear of solids with its capsule radius.
    lc::game::CollisionWorld solids;
    solids.Build(s, level.colliders);
    for (std::size_t i = 0; i + 1 < level.threatPath.size(); ++i) {
        for (int k = 0; k <= 20; ++k) {
            const Vec3 p = lc::math::Lerp(level.threatPath[i], level.threatPath[i + 1], static_cast<float>(k) / 20.0f);
            LC_CHECK(!solids.CapsuleOverlaps(p, lc::game::Threat::kCapsule));
        }
    }
    // Sockets and markers stand over the floor (or over the item resting in the socket, or the shelf that
    // is the socket's support), inside a room, not inside solids.
    std::set<std::uint32_t> supports{level.hallFloorId.value};
    for (const lc::LevelItem& item : level.items) {
        supports.insert(item.body.value);
        supports.insert(item.face.value);
    }
    for (const lc::Instance& inst : s.Instances()) {
        if (inst.name == "lamp_shelf") supports.insert(inst.id.value);
    }
    auto grounded = [&](Vec3 p, const char* what) {
        const lc::test::CpuHit down = lc::test::RaycastScene(s, p + Vec3{0.0f, 0.5f, 0.0f}, {0.0f, -1.0f, 0.0f}, 0.0f, 5.0f);
        const bool ok = down.hit && supports.contains(down.stableId);
        if (!ok) std::printf("    %s at (%.2f, %.2f, %.2f) is not over the floor\n", what, p.x, p.y, p.z);
        LC_CHECK(ok);
        bool inside = false;
        for (const Room& r : kRooms) inside = inside || (p.x > r.min.x && p.x < r.max.x && p.z > r.min.z && p.z < r.max.z);
        LC_CHECK(inside);
    };
    for (const lc::SocketSpec& sock : level.sockets) grounded({sock.position.x, 0.0f, sock.position.z}, sock.name.c_str());
    grounded(level.playerStart.position, "player_start");
    grounded(level.mirrorCheckCamera.position, "mirror_check_camera");
    for (const lc::ObjectiveStep& step : level.steps) {
        if (step.kind == "reach") grounded(step.markerPosition, step.id.c_str());
    }
    LC_CHECK(!solids.CapsuleOverlaps(level.playerStart.position, lc::game::Player::kCapsule));
}
