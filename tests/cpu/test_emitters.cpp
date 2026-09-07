#include "lc_test.h"

#include "scene/emitters.h"
#include "scene/primitives.h"
#include "scene/scene.h"

using lc::math::Mat4;

namespace {

std::uint32_t AddEmitterMaterial(lc::Scene& scene, const char* name, lc::math::Vec3 radiance, bool on = true) {
    lc::Material m;
    m.name = name;
    m.type = lc::MaterialType::Emitter;
    m.radiance = radiance;
    m.reflectance = {0, 0, 0};
    m.emitterOn = on;
    return scene.AddMaterial(m);
}

}  // namespace

LC_TEST(emitters_area_follows_instance_transform) {
    lc::Scene scene;
    const lc::MeshId quad = scene.AddMesh(lc::MakeQuadXZ("panel", 0.5f, 0.25f));  // 1 x 0.5 m = 0.5 m^2.
    const std::uint32_t lamp = AddEmitterMaterial(scene, "lamp", {4, 4, 4});
    scene.AddInstance("lamp_a", quad, Mat4::Translation({1, 2, 3}) * Mat4::RotationY(0.7f), lamp);
    scene.AddInstance("lamp_b", quad, Mat4::Translation({-3, 2, 0}) * Mat4::Scale({2, 2, 2}), lamp);

    const lc::EmitterTable table = lc::BuildEmitterTable(scene);
    LC_REQUIRE(table.emitters.size() == 2);
    LC_REQUIRE(table.triangles.size() == 4);
    LC_CHECK_NEAR(table.emitters[0].area, 0.5f, 1e-5);
    LC_CHECK_NEAR(table.emitters[1].area, 2.0f, 1e-5);  // Uniform scale 2 -> area x4.
    LC_CHECK_EQ(table.emitters[0].instanceIndex, 0u);
    LC_CHECK_EQ(table.emitters[1].instanceIndex, 1u);
    LC_CHECK_EQ(table.emitters[1].firstTriangle, 2u);
    LC_CHECK_EQ(table.emitters[1].triangleCount, 2u);
    LC_CHECK_NEAR(table.triangles[0].cdf, 0.5f, 1e-5);
    LC_CHECK_NEAR(table.triangles[1].cdf, 1.0f, 0.0f);
    LC_CHECK_NEAR(table.triangles[3].cdf, 1.0f, 0.0f);
    // Power-proportional selection: equal radiance, areas 0.5 and 2.0 -> 0.2 and 0.8.
    LC_CHECK_NEAR(table.emitters[0].selectionPdf, 0.2f, 1e-5);
    LC_CHECK_NEAR(table.emitters[1].selectionPdf, 0.8f, 1e-5);
    LC_CHECK_NEAR(table.emitters[0].selectionCdf, 0.2f, 1e-5);
    LC_CHECK_NEAR(table.emitters[1].selectionCdf, 1.0f, 0.0f);
    LC_CHECK_NEAR(table.totalPower, 4.0f * 2.5f, 1e-4);
}

LC_TEST(emitters_selection_by_radiance_and_off_sources_excluded) {
    lc::Scene scene;
    const lc::MeshId quad = scene.AddMesh(lc::MakeQuadXZ("panel", 0.5f, 0.5f));
    const std::uint32_t bright = AddEmitterMaterial(scene, "bright", {3, 3, 3});
    const std::uint32_t dim = AddEmitterMaterial(scene, "dim", {1, 1, 1});
    const std::uint32_t off = AddEmitterMaterial(scene, "off", {9, 9, 9}, false);
    scene.AddInstance("bright", quad, Mat4::Identity(), bright);
    scene.AddInstance("plain", quad, Mat4::Identity(), 0);
    scene.AddInstance("dim", quad, Mat4::Identity(), dim);
    scene.AddInstance("off", quad, Mat4::Identity(), off);

    const lc::EmitterTable table = lc::BuildEmitterTable(scene);
    LC_REQUIRE(table.emitters.size() == 2);
    LC_CHECK_EQ(table.emitters[0].instanceIndex, 0u);
    LC_CHECK_EQ(table.emitters[1].instanceIndex, 2u);
    LC_CHECK_NEAR(table.emitters[0].selectionPdf, 0.75f, 1e-6);
    LC_CHECK_NEAR(table.emitters[1].selectionPdf, 0.25f, 1e-6);
    LC_CHECK(table.emitters[0].selectionPdf > 0.0f && table.emitters[1].selectionPdf > 0.0f);

    lc::Scene empty;
    empty.AddInstance("plain", empty.AddMesh(lc::MakeBox("b", {1, 1, 1})), Mat4::Identity());
    LC_CHECK(lc::BuildEmitterTable(empty).emitters.empty());
}
