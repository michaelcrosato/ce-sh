#include "lc_test.h"

#include "cpu_raycast.h"
#include "core/error.h"
#include "scene/builtin_scenes.h"
#include "scene/primitives.h"
#include "scene/rooms.h"
#include "scene/scene.h"

using lc::math::Mat4;
using lc::math::Vec3;

LC_TEST(scene_box_is_closed_and_outward_facing) {
    const lc::MeshData box = lc::MakeBox("box", {1, 2, 3});
    LC_CHECK_EQ(box.positions.size(), std::size_t{24});
    LC_CHECK_EQ(box.indices.size(), std::size_t{36});
    LC_CHECK(lc::ValidateMesh(box).empty());
    for (std::uint32_t t = 0; t < box.TriangleCount(); ++t) {
        const Vec3 p0 = box.positions[box.indices[t * 3 + 0]];
        const Vec3 p1 = box.positions[box.indices[t * 3 + 1]];
        const Vec3 p2 = box.positions[box.indices[t * 3 + 2]];
        const Vec3 n = lc::TriangleNormal(p0, p1, p2);
        const Vec3 centroid = (p0 + p1 + p2) / 3.0f;
        LC_CHECK(lc::math::Dot(n, centroid) > 0.0f);  // Outward.
        LC_CHECK(std::fabs(p0.x) <= 1.0f + 1e-6f && std::fabs(p0.y) <= 2.0f + 1e-6f && std::fabs(p0.z) <= 3.0f + 1e-6f);
    }
}

LC_TEST(scene_quad_faces_up) {
    const lc::MeshData quad = lc::MakeQuadXZ("q", 2, 3);
    LC_CHECK(lc::ValidateMesh(quad).empty());
    for (std::uint32_t t = 0; t < quad.TriangleCount(); ++t) {
        const Vec3 n = lc::TriangleNormal(quad.positions[quad.indices[t * 3]], quad.positions[quad.indices[t * 3 + 1]],
                                          quad.positions[quad.indices[t * 3 + 2]]);
        LC_CHECK(n.y > 0.0f);
        LC_CHECK_NEAR(n.x, 0, 1e-6);
        LC_CHECK_NEAR(n.z, 0, 1e-6);
    }
}

LC_TEST(scene_mesh_validation_reports_problems) {
    lc::MeshData bad = lc::MakeTriangle("bad", {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
    bad.indices[2] = 9;
    LC_CHECK(!lc::ValidateMesh(bad).empty());

    lc::MeshData degenerate = lc::MakeTriangle("flat", {0, 0, 0}, {1, 0, 0}, {2, 0, 0});
    LC_CHECK(!lc::ValidateMesh(degenerate).empty());

    lc::MeshData odd = lc::MakeTriangle("odd", {0, 0, 0}, {1, 0, 0}, {0, 1, 0});
    odd.indices.push_back(0);
    LC_CHECK(!lc::ValidateMesh(odd).empty());

    lc::Scene scene;
    bool threw = false;
    try {
        scene.AddMesh(bad);
    } catch (const lc::Error&) {
        threw = true;
    }
    LC_CHECK(threw);
}

LC_TEST(scene_stable_ids_and_transform_history) {
    lc::Scene scene;
    const lc::MeshId mesh = scene.AddMesh(lc::MakeBox("b", {1, 1, 1}));
    const lc::InstanceId a = scene.AddInstance("a", mesh, Mat4::Identity());
    const lc::InstanceId b = scene.AddInstance("b", mesh, Mat4::Translation({1, 0, 0}));
    const lc::InstanceId c = scene.AddInstance("c", mesh, Mat4::Translation({2, 0, 0}));
    LC_CHECK_EQ(a.value, 1u);
    LC_CHECK_EQ(b.value, 2u);
    LC_CHECK_EQ(c.value, 3u);

    scene.SetTransform(b, Mat4::Translation({5, 0, 0}));
    const lc::Instance* inst = scene.FindInstance(b);
    LC_REQUIRE(inst != nullptr);
    LC_CHECK_EQ(inst->transformRevision, 1u);
    LC_CHECK_NEAR(inst->objectToWorld.m[0][3], 5, 0);
    LC_CHECK_NEAR(inst->prevObjectToWorld.m[0][3], 1, 0);  // Unchanged until the frame is committed.
    scene.CommitRenderedFrame();
    LC_CHECK_NEAR(scene.FindInstance(b)->prevObjectToWorld.m[0][3], 5, 0);
    LC_CHECK_EQ(scene.TotalTriangles(), 36u);
}

LC_TEST(scene_builtin_hit_expectations_agree_with_cpu_raycast) {
    for (const std::string& name : lc::BuiltinSceneNames()) {
        const auto desc = lc::BuildBuiltinScene(name);
        LC_REQUIRE(desc.has_value());
        LC_CHECK(!desc->expectations.empty() || !desc->radianceExpectations.empty());
        const float aspect = 1280.0f / 720.0f;
        for (const lc::HitExpectation& e : desc->expectations) {
            float u = e.u;
            float v = e.v;
            if (e.worldPoint) {
                const auto uv = desc->camera.ProjectToImage(aspect, *e.worldPoint);
                if (!uv) {
                    lc::test::Fail(__FILE__, __LINE__, name + ": expectation point behind the camera for '" + e.description + "'");
                    continue;
                }
                u = uv->x;
                v = uv->y;
            }
            const Vec3 dir = desc->camera.RayDirection(aspect, u, v);
            const lc::test::CpuHit hit = lc::test::RaycastThroughMirrors(desc->scene, desc->camera.position, dir);
            if (e.expectedStableId == 0) {
                if (hit.hit) lc::test::Fail(__FILE__, __LINE__, name + ": expected a miss for '" + e.description + "'");
            } else {
                if (!hit.hit || hit.stableId != e.expectedStableId) {
                    lc::test::Fail(__FILE__, __LINE__, name + ": wrong hit for '" + e.description + "' (got id " +
                                                            std::to_string(hit.hit ? hit.stableId : 0) + ")");
                }
                if (hit.hit && !e.worldPoint && hit.frontFace != e.expectFrontFace) {
                    lc::test::Fail(__FILE__, __LINE__, name + ": facing mismatch for '" + e.description + "'");
                }
            }
        }
        // Every radiance expectation point must be visible from the camera and hit a surface.
        for (const lc::RadianceExpectation& r : desc->radianceExpectations) {
            if (r.kind == lc::RadianceExpectation::Kind::ZeroImage) continue;
            const auto uv = desc->camera.ProjectToImage(aspect, r.point);
            if (!uv || uv->x < 0.0f || uv->x > 1.0f || uv->y < 0.0f || uv->y > 1.0f) {
                lc::test::Fail(__FILE__, __LINE__, name + ": radiance patch off screen for '" + r.description + "'");
                continue;
            }
            const lc::test::CpuHit hit = lc::test::RaycastScene(desc->scene, desc->camera.position,
                                                                desc->camera.RayDirection(aspect, uv->x, uv->y), 0.0f, 1000.0f);
            if (!hit.hit) {
                lc::test::Fail(__FILE__, __LINE__, name + ": radiance patch ray misses for '" + r.description + "'");
            } else if (lc::math::Length(hit.position - r.point) > 0.05f) {
                lc::test::Fail(__FILE__, __LINE__, name + ": radiance patch point is occluded for '" + r.description + "' (hit id " +
                                                        std::to_string(hit.stableId) + " at distance " + std::to_string(hit.t) + ")");
            }
        }
    }
    LC_CHECK(!lc::BuildBuiltinScene("no_such_scene").has_value());
}

LC_TEST(scene_camera_ray_directions_follow_the_image_axes) {
    lc::Camera cam;
    cam.position = {0, 0, 0};
    const float aspect = 16.0f / 9.0f;
    const Vec3 centre = cam.RayDirection(aspect, 0.5f, 0.5f);
    LC_CHECK(lc::math::NearlyEqual(centre, {0, 0, -1}, 1e-6f));
    const Vec3 right = cam.RayDirection(aspect, 1.0f, 0.5f);
    LC_CHECK(right.x > 0.0f);
    LC_CHECK_NEAR(right.x, -right.z, 1e-5);  // 90 degree horizontal FOV: the edge ray is at 45 degrees.
    const Vec3 top = cam.RayDirection(aspect, 0.5f, 0.0f);
    LC_CHECK(top.y > 0.0f);

    // Projection is the inverse of ray generation.
    const Vec3 p = cam.position + cam.RayDirection(aspect, 0.3f, 0.7f) * 5.0f;
    const auto uv = cam.ProjectToImage(aspect, p);
    LC_REQUIRE(uv.has_value());
    LC_CHECK_NEAR(uv->x, 0.3f, 1e-5);
    LC_CHECK_NEAR(uv->y, 0.7f, 1e-5);
    LC_CHECK(!cam.ProjectToImage(aspect, {0, 0, 5}).has_value());  // Behind the camera.

    lc::Camera aimed;
    aimed.position = {1, 2, 3};
    aimed.LookAt({1, 2, -7});
    LC_CHECK(lc::math::NearlyEqual(aimed.Forward(), {0, 0, -1}, 1e-6f));
    aimed.LookAt({-4, 2, 3});
    LC_CHECK(lc::math::NearlyEqual(aimed.Forward(), {-1, 0, 0}, 1e-6f));
    aimed.LookAt({1, 7, 3 - 5});
    LC_CHECK(aimed.Forward().y > 0.7f);
}

LC_TEST(scene_room_slabs_enclose_the_volume_without_gaps) {
    lc::Scene scene;
    const lc::RoomSpec spec{{-2, 0, -2}, {2, 2.8f, 2}, 0.15f};
    lc::AddRoom(scene, "room", spec, {});
    // Rays from the centre in the six axis directions hit the inner faces at the expected distances.
    const Vec3 centre{0, 1.4f, 0};
    const Vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const float expected[6] = {2, 2, 1.4f, 1.4f, 2, 2};
    for (int i = 0; i < 6; ++i) {
        const lc::test::CpuHit hit = lc::test::RaycastScene(scene, centre, dirs[i], 0, 100);
        LC_CHECK(hit.hit);
        LC_CHECK_NEAR(hit.t, expected[i], 1e-5);
        LC_CHECK(hit.frontFace);  // Inner faces face inward.
    }
    // A diagonal ray toward a corner also hits a wall (corners are solid).
    const lc::test::CpuHit corner = lc::test::RaycastScene(scene, centre, lc::math::Normalize({1, 0.5f, 1}), 0, 100);
    LC_CHECK(corner.hit);
    // From outside, a ray toward the room hits the outer face 0.15 m before the inner volume.
    const lc::test::CpuHit outside = lc::test::RaycastScene(scene, {5, 1.4f, 0}, {-1, 0, 0}, 0, 100);
    LC_CHECK(outside.hit);
    LC_CHECK_NEAR(outside.t, 5 - 2.15f, 1e-5);
}

LC_TEST(scene_closed_door_blocks_the_doorway_and_open_door_clears_it) {
    lc::Scene scene;
    const lc::RoomSpec spec{{-2, 0, -2}, {2, 2.8f, 2}, 0.15f};
    const lc::DoorwaySpec doorway{0.0f, 0.9f, 2.1f};
    lc::AddPosZWallWithDoorway(scene, "room", spec, doorway, 0);
    const lc::DoorHandle door = lc::AddDoor(scene, "door", spec, doorway, lc::DoorSpec{}, 0);
    // Rays from outside through the opening, at several heights and offsets, are blocked when closed.
    const float xs[] = {-0.44f, 0.0f, 0.44f};
    const float ys[] = {0.05f, 1.0f, 2.05f};
    for (const float x : xs) {
        for (const float y : ys) {
            const lc::test::CpuHit hit = lc::test::RaycastScene(scene, {x, y, 4.0f}, {0, 0, -1}, 0, 100);
            LC_CHECK(hit.hit);
            if (hit.hit) LC_CHECK_EQ(hit.stableId, door.id.value);
        }
    }
    // Just above the door: the lintel blocks. Beside it: the side pieces block.
    LC_CHECK(lc::test::RaycastScene(scene, {0, 2.5f, 4.0f}, {0, 0, -1}, 0, 100).hit);
    LC_CHECK(lc::test::RaycastScene(scene, {1.0f, 1.0f, 4.0f}, {0, 0, -1}, 0, 100).hit);
    // Open the door: the opening is clear, and the door leaf now stands inside the room along -Z.
    scene.SetTransform(door.id, lc::DoorTransform(door, lc::math::kPi * 0.5f));
    const lc::test::CpuHit clear = lc::test::RaycastScene(scene, {0.2f, 1.0f, 4.0f}, {0, 0, -1}, 0, 100);
    LC_CHECK(!clear.hit);
    const lc::test::CpuHit leaf = lc::test::RaycastScene(scene, {-2.0f, 1.0f, 1.5f}, {1, 0, 0}, 0, 100);
    LC_CHECK(leaf.hit);
    if (leaf.hit) {
        LC_CHECK_EQ(leaf.stableId, door.id.value);
        // The open leaf's thickness (0.04 m) is centred on the hinge; its -X face is 0.02 m before it.
        LC_CHECK_NEAR(leaf.position.x, door.hinge.x - 0.02f, 1e-3f);
        LC_CHECK(leaf.position.z < 2.0f && leaf.position.z > 1.1f);  // Swung into the room along -Z.
    }
}

LC_TEST(scene_rectangle_emitter_faces_the_requested_direction) {
    lc::Scene scene;
    lc::Material lamp;
    lamp.name = "lamp";
    lamp.type = lc::MaterialType::Emitter;
    lamp.radiance = {1, 1, 1};
    const std::uint32_t m = scene.AddMaterial(lamp);
    const Vec3 facings[6] = {{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}};
    for (const Vec3& f : facings) {
        lc::Scene s;
        const std::uint32_t mm = s.AddMaterial(lamp);
        lc::AddRectangleEmitter(s, "e", 1.0f, 0.5f, {0, 0, 0}, f, mm);
        const lc::MeshData& mesh = s.Meshes()[0].data;
        const lc::Instance& inst = s.Instances()[0];
        const Vec3 p0 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[0]]);
        const Vec3 p1 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[1]]);
        const Vec3 p2 = inst.objectToWorld.TransformPoint(mesh.positions[mesh.indices[2]]);
        const Vec3 n = lc::math::Normalize(lc::TriangleNormal(p0, p1, p2));
        LC_CHECK(lc::math::NearlyEqual(n, f, 1e-5f));
    }
    (void)m;
}
