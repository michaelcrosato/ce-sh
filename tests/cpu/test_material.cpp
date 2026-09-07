#include "lc_test.h"

#include "core/error.h"
#include "scene/material.h"
#include "scene/primitives.h"
#include "scene/scene.h"

LC_TEST(material_validation_ranges) {
    lc::Material ok;
    ok.name = "ok";
    LC_CHECK(lc::ValidateMaterial(ok).empty());

    lc::Material gain = ok;
    gain.reflectance = {1.2f, 0.5f, 0.5f};
    LC_CHECK(!lc::ValidateMaterial(gain).empty());

    lc::Material negative = ok;
    negative.reflectance = {-0.1f, 0.5f, 0.5f};
    LC_CHECK(!lc::ValidateMaterial(negative).empty());

    lc::Material glowingDiffuse = ok;
    glowingDiffuse.radiance = {1, 1, 1};
    LC_CHECK(!lc::ValidateMaterial(glowingDiffuse).empty());

    lc::Material emitter;
    emitter.name = "lamp";
    emitter.type = lc::MaterialType::Emitter;
    emitter.radiance = {4, 4, 4};
    emitter.reflectance = {0, 0, 0};
    LC_CHECK(lc::ValidateMaterial(emitter).empty());
    LC_CHECK(emitter.IsActiveEmitter());

    lc::Material darkEmitter = emitter;
    darkEmitter.radiance = {0, 0, 0};
    LC_CHECK(!lc::ValidateMaterial(darkEmitter).empty());
    darkEmitter.emitterOn = false;
    LC_CHECK(lc::ValidateMaterial(darkEmitter).empty());
    LC_CHECK(!darkEmitter.IsActiveEmitter());

    lc::Material mirror;
    mirror.type = lc::MaterialType::Mirror;
    mirror.reflectance = {1, 1, 1};
    LC_CHECK(lc::ValidateMaterial(mirror).empty());

    lc::Material conductor;
    conductor.type = lc::MaterialType::RoughConductor;
    conductor.reflectance = {0.9f, 0.7f, 0.5f};
    conductor.roughness = 0.3f;
    LC_CHECK(lc::ValidateMaterial(conductor).empty());
    conductor.roughness = 0.0f;
    LC_CHECK(!lc::ValidateMaterial(conductor).empty());
    conductor.roughness = 1.5f;
    LC_CHECK(!lc::ValidateMaterial(conductor).empty());
    LC_CHECK_EQ(std::string(lc::MaterialTypeName(lc::MaterialType::RoughConductor)), std::string("rough_conductor"));
}

LC_TEST(material_luminance_weights) {
    LC_CHECK_NEAR(lc::Luminance({1, 1, 1}), 1.0f, 1e-6);
    LC_CHECK_NEAR(lc::Luminance({0, 1, 0}), 0.7152f, 1e-6);
}

LC_TEST(scene_materials_default_and_validation) {
    lc::Scene scene;
    LC_REQUIRE(scene.Materials().size() == 1);
    LC_CHECK(scene.Materials()[0].type == lc::MaterialType::Diffuse);
    const lc::MeshId mesh = scene.AddMesh(lc::MakeBox("b", {1, 1, 1}));
    scene.AddInstance("default", mesh, lc::math::Mat4::Identity());  // Material 0 implied.

    lc::Material red;
    red.name = "red";
    red.reflectance = {0.8f, 0.1f, 0.1f};
    const std::uint32_t redIndex = scene.AddMaterial(red);
    LC_CHECK_EQ(redIndex, 1u);
    scene.AddInstance("red_box", mesh, lc::math::Mat4::Identity(), redIndex);

    bool threw = false;
    try {
        scene.AddInstance("bad", mesh, lc::math::Mat4::Identity(), 7);
    } catch (const lc::Error&) {
        threw = true;
    }
    LC_CHECK(threw);

    lc::Material bad;
    bad.reflectance = {2, 2, 2};
    threw = false;
    try {
        scene.AddMaterial(bad);
    } catch (const lc::Error&) {
        threw = true;
    }
    LC_CHECK(threw);

    lc::Material lamp;
    lamp.name = "lamp";
    lamp.type = lc::MaterialType::Emitter;
    lamp.radiance = {2, 2, 2};
    const std::uint32_t lampIndex = scene.AddMaterial(lamp);
    const std::uint32_t revision = scene.MaterialRevision();
    scene.SetEmitterOn(lampIndex, false);
    LC_CHECK_EQ(scene.MaterialRevision(), revision + 1);
    LC_CHECK(!scene.Materials()[lampIndex].emitterOn);
    scene.SetEmitterOn(lampIndex, false);  // No change, no revision bump.
    LC_CHECK_EQ(scene.MaterialRevision(), revision + 1);
}
