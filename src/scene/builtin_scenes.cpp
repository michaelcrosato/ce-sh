#include "scene/builtin_scenes.h"

#include "scene/primitives.h"

namespace lc {

using math::Mat4;
using math::Vec3;

namespace {

// One asymmetric triangle facing the camera. Proves device, BLAS, TLAS, ray generation, and hit ids.
SceneDescription BuildRtTriangle() {
    SceneDescription d;
    d.name = "rt_triangle";
    // Counter-clockwise seen from +Z, so the geometric normal is +Z (toward the camera).
    const MeshId tri = d.scene.AddMesh(MakeTriangle("triangle", {-1.0f, 0.6f, 0.0f}, {1.2f, 0.4f, 0.0f}, {0.3f, 2.6f, 0.0f}));
    d.scene.AddInstance("triangle", tri, Mat4::Identity());  // Stable id 1.
    d.camera.position = {0.0f, 1.6f, 4.0f};
    d.camera.yawRadians = 0.0f;
    d.camera.pitchRadians = 0.0f;
    d.expectations = {
        {0.5f, 0.5f, 1, true, "image centre hits the triangle front face"},
        {0.02f, 0.02f, 0, true, "top-left corner misses"},
        {0.98f, 0.02f, 0, true, "top-right corner misses"},
        {0.02f, 0.98f, 0, true, "bottom-left corner misses"},
        {0.98f, 0.98f, 0, true, "bottom-right corner misses"},
    };
    return d;
}

// Floor plus two different boxes left and right: an asymmetric scene for handedness and hit ids (T02).
SceneDescription BuildRtBoxes() {
    SceneDescription d;
    d.name = "rt_boxes";
    const MeshId floor = d.scene.AddMesh(MakeQuadXZ("floor", 3.0f, 3.0f));
    const MeshId smallBox = d.scene.AddMesh(MakeBox("small_box", {0.4f, 0.4f, 0.4f}));
    const MeshId tallBox = d.scene.AddMesh(MakeBox("tall_box", {0.4f, 0.9f, 0.4f}));
    d.scene.AddInstance("floor", floor, Mat4::Identity());                              // Stable id 1.
    d.scene.AddInstance("left_small_box", smallBox, Mat4::Translation({-1.2f, 0.4f, 0.0f}));  // Stable id 2.
    d.scene.AddInstance("right_tall_box", tallBox, Mat4::Translation({1.2f, 0.9f, 0.0f}));    // Stable id 3.
    d.camera.position = {0.0f, 1.2f, 4.0f};
    d.camera.yawRadians = 0.0f;
    d.camera.pitchRadians = 0.0f;
    d.expectations = {
        {0.34f, 0.69f, 2, true, "left of centre hits the small box (+X is screen right)"},
        {0.66f, 0.57f, 3, true, "right of centre hits the tall box"},
        {0.50f, 0.90f, 1, true, "below centre hits the floor"},
        {0.50f, 0.05f, 0, true, "above the horizon misses (no ceiling)"},
        {0.02f, 0.02f, 0, true, "top-left corner misses"},
    };
    return d;
}

}  // namespace

std::vector<std::string> BuiltinSceneNames() { return {"rt_triangle", "rt_boxes"}; }

std::optional<SceneDescription> BuildBuiltinScene(std::string_view name) {
    if (name == "rt_triangle") return BuildRtTriangle();
    if (name == "rt_boxes") return BuildRtBoxes();
    return std::nullopt;
}

}  // namespace lc
