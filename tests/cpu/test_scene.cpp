#include "lc_test.h"

#include "cpu_raycast.h"
#include "core/error.h"
#include "scene/builtin_scenes.h"
#include "scene/primitives.h"
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
        // Every vertex touches the box surface.
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

LC_TEST(scene_builtin_expectations_agree_with_cpu_raycast) {
    for (const std::string& name : lc::BuiltinSceneNames()) {
        const auto desc = lc::BuildBuiltinScene(name);
        LC_REQUIRE(desc.has_value());
        LC_CHECK(!desc->expectations.empty());
        const float aspect = 1280.0f / 720.0f;
        for (const lc::HitExpectation& e : desc->expectations) {
            const Vec3 dir = desc->camera.RayDirection(aspect, e.u, e.v);
            const lc::test::CpuHit hit = lc::test::RaycastScene(desc->scene, desc->camera.position, dir, 0.0f, 1000.0f);
            if (e.expectedStableId == 0) {
                if (hit.hit) lc::test::Fail(__FILE__, __LINE__, name + ": expected a miss for '" + e.description + "'");
            } else {
                if (!hit.hit || hit.stableId != e.expectedStableId) {
                    lc::test::Fail(__FILE__, __LINE__, name + ": wrong hit for '" + e.description + "' (got id " +
                                                            std::to_string(hit.hit ? hit.stableId : 0) + ")");
                }
                if (hit.hit && hit.frontFace != e.expectFrontFace) {
                    lc::test::Fail(__FILE__, __LINE__, name + ": facing mismatch for '" + e.description + "'");
                }
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
}
