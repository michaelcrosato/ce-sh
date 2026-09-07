#include "scene/builtin_scenes.h"

#include "core/math/radiometry.h"
#include "scene/primitives.h"
#include "scene/rooms.h"
#include "scene/two_room_level.h"

namespace lc {

using math::Mat4;
using math::Vec3;

namespace {

std::uint32_t AddDiffuse(Scene& s, const char* name, Vec3 reflectance) {
    Material m;
    m.name = name;
    m.type = MaterialType::Diffuse;
    m.reflectance = reflectance;
    return s.AddMaterial(m);
}

std::uint32_t AddEmitter(Scene& s, const char* name, Vec3 radiance, bool on = true) {
    Material m;
    m.name = name;
    m.type = MaterialType::Emitter;
    m.radiance = radiance;
    m.reflectance = {0.0f, 0.0f, 0.0f};
    m.emitterOn = on;
    return s.AddMaterial(m);
}

std::uint32_t AddMirror(Scene& s, const char* name, Vec3 reflectance) {
    Material m;
    m.name = name;
    m.type = MaterialType::Mirror;
    m.reflectance = reflectance;
    return s.AddMaterial(m);
}

RadianceExpectation Zero(const char* what) {
    RadianceExpectation e;
    e.kind = RadianceExpectation::Kind::ZeroImage;
    e.description = what;
    return e;
}

RadianceExpectation Positive(const char* what, Vec3 point, float minimum = 1e-3f) {
    RadianceExpectation e;
    e.kind = RadianceExpectation::Kind::PositivePatch;
    e.description = what;
    e.point = point;
    e.minimum = minimum;
    return e;
}

RadianceExpectation Analytic(const char* what, Vec3 point, float expected, float relTol) {
    RadianceExpectation e;
    e.kind = RadianceExpectation::Kind::AnalyticPatch;
    e.description = what;
    e.point = point;
    e.expected = expected;
    e.relativeTolerance = relTol;
    return e;
}

// ---------------------------------------------------------------------------------------------
// M1 diagnostic scenes.

SceneDescription BuildRtTriangle() {
    SceneDescription d;
    d.name = "rt_triangle";
    const MeshId tri = d.scene.AddMesh(MakeTriangle("triangle", {-1.0f, 0.6f, 0.0f}, {1.2f, 0.4f, 0.0f}, {0.3f, 2.6f, 0.0f}));
    d.scene.AddInstance("triangle", tri, Mat4::Identity());  // Stable id 1.
    d.camera.position = {0.0f, 1.6f, 4.0f};
    d.expectations = {
        {0.5f, 0.5f, std::nullopt, 1, true, "image centre hits the triangle front face"},
        {0.02f, 0.02f, std::nullopt, 0, true, "top-left corner misses"},
        {0.98f, 0.02f, std::nullopt, 0, true, "top-right corner misses"},
        {0.02f, 0.98f, std::nullopt, 0, true, "bottom-left corner misses"},
        {0.98f, 0.98f, std::nullopt, 0, true, "bottom-right corner misses"},
    };
    return d;
}

SceneDescription BuildRtBoxes() {
    SceneDescription d;
    d.name = "rt_boxes";
    const MeshId floor = d.scene.AddMesh(MakeQuadXZ("floor", 3.0f, 3.0f));
    const MeshId smallBox = d.scene.AddMesh(MakeBox("small_box", {0.4f, 0.4f, 0.4f}));
    const MeshId tallBox = d.scene.AddMesh(MakeBox("tall_box", {0.4f, 0.9f, 0.4f}));
    d.scene.AddInstance("floor", floor, Mat4::Identity());                                     // 1
    d.scene.AddInstance("left_small_box", smallBox, Mat4::Translation({-1.2f, 0.4f, 0.0f}));  // 2
    d.scene.AddInstance("right_tall_box", tallBox, Mat4::Translation({1.2f, 0.9f, 0.0f}));    // 3
    d.camera.position = {0.0f, 1.2f, 4.0f};
    d.expectations = {
        {0.34f, 0.69f, std::nullopt, 2, true, "left of centre hits the small box (+X is screen right)"},
        {0.66f, 0.57f, std::nullopt, 3, true, "right of centre hits the tall box"},
        {0.50f, 0.90f, std::nullopt, 1, true, "below centre hits the floor"},
        {0.50f, 0.05f, std::nullopt, 0, true, "above the horizon misses (no ceiling)"},
        {0.02f, 0.02f, std::nullopt, 0, true, "top-left corner misses"},
    };
    return d;
}

// ---------------------------------------------------------------------------------------------
// M2 lighting scenes. The standard room is 4 x 4 m with a 2.8 m ceiling and 0.15 m walls.

const RoomSpec kStandardRoom{{-2.0f, 0.0f, -2.0f}, {2.0f, 2.8f, 2.0f}, 0.15f};

// T03: sealed grey room, one emitter that is switched off. Everything must be black.
SceneDescription BuildT03DarkRoom() {
    SceneDescription d;
    d.name = "t03_dark_room";
    d.needsLighting = true;
    const std::uint32_t grey = AddDiffuse(d.scene, "grey", {0.5f, 0.5f, 0.5f});
    AddRoom(d.scene, "room", kStandardRoom, {grey, grey, grey});
    const std::uint32_t lampOff = AddEmitter(d.scene, "ceiling_panel_off", {8.0f, 8.0f, 8.0f}, false);
    AddRectangleEmitter(d.scene, "ceiling_panel", 1.0f, 1.0f, {0.0f, 2.79f, 0.0f}, {0.0f, -1.0f, 0.0f}, lampOff);
    AddBox(d.scene, "crate", {0.3f, 0.3f, 0.3f}, {0.8f, 0.3f, -0.5f}, grey);
    d.camera.position = {0.0f, 1.5f, 1.5f};
    d.camera.LookAt({0.0f, 1.0f, -2.0f});
    d.radianceExpectations = {Zero("no active source: raw radiance is zero everywhere")};
    return d;
}

// T04: sealed room with a strong emitter outside the +Z wall, facing it. Interior stays black.
SceneDescription BuildT04Sealed(bool open) {
    SceneDescription d;
    d.name = open ? "t04_open" : "t04_sealed";
    d.needsLighting = true;
    const std::uint32_t grey = AddDiffuse(d.scene, "grey", {0.6f, 0.6f, 0.6f});
    const RoomSpec room = kStandardRoom;
    RoomInstances r = AddRoom(d.scene, "room", room, {grey, grey, grey});
    (void)r;
    const DoorwaySpec doorway{0.0f, 0.9f, 2.1f};
    if (open) {
        // Replace the solid +Z wall (the last room instance) with a doorway: rebuild the scene
        // without it. Simplest: build the room pieces explicitly.
        SceneDescription fresh;
        fresh.name = d.name;
        fresh.needsLighting = true;
        const std::uint32_t g = AddDiffuse(fresh.scene, "grey", {0.6f, 0.6f, 0.6f});
        RoomSpec noPosZ = room;
        // Floor, ceiling, and three walls, then the doorway wall and the door.
        const float t = room.wallThickness;
        const Vec3 lo = room.innerMin;
        const Vec3 hi = room.innerMax;
        AddBox(fresh.scene, "room_floor", {(hi.x - lo.x) * 0.5f + t, t * 0.5f, (hi.z - lo.z) * 0.5f + t}, {0.0f, lo.y - t * 0.5f, 0.0f}, g);
        AddBox(fresh.scene, "room_ceiling", {(hi.x - lo.x) * 0.5f + t, t * 0.5f, (hi.z - lo.z) * 0.5f + t}, {0.0f, hi.y + t * 0.5f, 0.0f}, g);
        AddBox(fresh.scene, "room_wall_neg_x", {t * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f + t}, {lo.x - t * 0.5f, (lo.y + hi.y) * 0.5f, 0.0f}, g);
        AddBox(fresh.scene, "room_wall_pos_x", {t * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f + t}, {hi.x + t * 0.5f, (lo.y + hi.y) * 0.5f, 0.0f}, g);
        AddBox(fresh.scene, "room_wall_neg_z", {(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, t * 0.5f}, {0.0f, (lo.y + hi.y) * 0.5f, lo.z - t * 0.5f}, g);
        AddPosZWallWithDoorway(fresh.scene, "room", noPosZ, doorway, g);
        const DoorHandle door = AddDoor(fresh.scene, "door", noPosZ, doorway, DoorSpec{}, g);
        fresh.scene.SetTransform(door.id, DoorTransform(door, math::kPi * 0.5f));
        fresh.scene.CommitRenderedFrame();
        const std::uint32_t lamp = AddEmitter(fresh.scene, "outside_panel", {10.0f, 10.0f, 10.0f});
        AddRectangleEmitter(fresh.scene, "outside_panel", 2.0f, 2.0f, {0.0f, 1.4f, hi.z + t + 1.0f}, {0.0f, 0.0f, -1.0f}, lamp);
        fresh.camera.position = {0.0f, 2.2f, -1.5f};
        fresh.camera.LookAt({0.0f, 0.0f, 1.0f});
        fresh.radianceExpectations = {
            Positive("floor 1 m inside the open door is lit through the real opening", {0.0f, 0.0f, 1.0f}, 1e-3f),
        };
        fresh.statsPatches = {{"floor_inside_door", {0.0f, 0.0f, 1.0f}, 3}};
        return fresh;
    }
    const std::uint32_t lamp = AddEmitter(d.scene, "outside_panel", {10.0f, 10.0f, 10.0f});
    AddRectangleEmitter(d.scene, "outside_panel", 2.0f, 2.0f, {0.0f, 1.4f, room.innerMax.z + room.wallThickness + 1.0f}, {0.0f, 0.0f, -1.0f}, lamp);
    d.camera.position = {0.0f, 1.5f, -1.5f};
    d.camera.LookAt({0.0f, 1.4f, 2.0f});
    d.radianceExpectations = {Zero("a source outside the sealed room does not light its interior")};
    return d;
}

// T08/T09: an open floor under a rectangular emitter; the floor radiance has a closed form.
SceneDescription BuildT09RectLight(bool large) {
    SceneDescription d;
    d.name = large ? "t09_rect_light_large" : "t09_rect_light";
    d.needsLighting = true;
    const float width = large ? 2.0f : 1.0f;
    const float depth = large ? 1.0f : 0.5f;
    const float height = 1.5f;
    const float radiance = 4.0f;
    const float albedo = 0.5f;
    const std::uint32_t floorMaterial = AddDiffuse(d.scene, "floor_grey", {albedo, albedo, albedo});
    const MeshId floor = d.scene.AddMesh(MakeQuadXZ("floor", 5.0f, 5.0f));
    d.scene.AddInstance("floor", floor, Mat4::Identity(), floorMaterial);
    const std::uint32_t lamp = AddEmitter(d.scene, "panel", {radiance, radiance, radiance});
    AddRectangleEmitter(d.scene, "panel", width, depth, {0.0f, height, 0.0f}, {0.0f, -1.0f, 0.0f}, lamp);
    d.camera.position = {0.0f, 1.0f, 3.0f};
    d.camera.LookAt({0.0f, 0.0f, 0.0f});

    const float centreE = math::RectangleIrradianceBelowCentre(width, depth, height, radiance);
    const Vec3 off{width * 0.75f, 0.0f, 0.0f};  // Outside the rectangle's footprint along X.
    const float offE = math::RectangleIrradianceAtPoint(-width * 0.5f, width * 0.5f, -depth * 0.5f, depth * 0.5f, height, radiance, off.x, off.z);
    d.radianceExpectations = {
        Analytic("floor under the emitter centre matches the closed-form irradiance", {0.0f, 0.0f, 0.0f}, albedo * centreE / math::kPi, 0.02f),
        Analytic("floor beside the emitter matches the closed-form irradiance", off, albedo * offE / math::kPi, 0.03f),
    };
    d.statsPatches = {{"centre", {0.0f, 0.0f, 0.0f}, 2}, {"beside", off, 2}};
    return d;
}

// T07: white room whose -X wall is red and +X wall is white, lit by a ceiling panel. The floor next
// to the red wall receives red indirect light; the mirror-image patch next to the white wall does
// not. Both patches are lit directly by the same panel, so the ratio isolates the indirect colour.
SceneDescription BuildT07Bleed() {
    SceneDescription d;
    d.name = "t07_bleed";
    d.needsLighting = true;
    const std::uint32_t white = AddDiffuse(d.scene, "white", {0.8f, 0.8f, 0.8f});
    const std::uint32_t red = AddDiffuse(d.scene, "red", {0.8f, 0.1f, 0.1f});
    const RoomSpec room = kStandardRoom;
    const float t = room.wallThickness;
    const Vec3 lo = room.innerMin;
    const Vec3 hi = room.innerMax;
    AddBox(d.scene, "room_floor", {(hi.x - lo.x) * 0.5f + t, t * 0.5f, (hi.z - lo.z) * 0.5f + t}, {0.0f, lo.y - t * 0.5f, 0.0f}, white);
    AddBox(d.scene, "room_ceiling", {(hi.x - lo.x) * 0.5f + t, t * 0.5f, (hi.z - lo.z) * 0.5f + t}, {0.0f, hi.y + t * 0.5f, 0.0f}, white);
    AddBox(d.scene, "room_wall_neg_x_red", {t * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f + t}, {lo.x - t * 0.5f, (lo.y + hi.y) * 0.5f, 0.0f}, red);
    AddBox(d.scene, "room_wall_pos_x", {t * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f + t}, {hi.x + t * 0.5f, (lo.y + hi.y) * 0.5f, 0.0f}, white);
    AddBox(d.scene, "room_wall_neg_z", {(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, t * 0.5f}, {0.0f, (lo.y + hi.y) * 0.5f, lo.z - t * 0.5f}, white);
    AddBox(d.scene, "room_wall_pos_z", {(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, t * 0.5f}, {0.0f, (lo.y + hi.y) * 0.5f, hi.z + t * 0.5f}, white);
    AddBox(d.scene, "crate", {0.3f, 0.3f, 0.3f}, {0.0f, 0.3f, -1.0f}, white);
    const std::uint32_t lamp = AddEmitter(d.scene, "ceiling_panel", {8.0f, 8.0f, 8.0f});
    AddRectangleEmitter(d.scene, "ceiling_panel", 1.0f, 1.0f, {0.0f, 2.79f, 0.0f}, {0.0f, -1.0f, 0.0f}, lamp);
    d.camera.position = {0.0f, 1.6f, 1.8f};
    d.camera.LookAt({0.0f, 0.5f, -0.6f});
    const Vec3 nearRed{-1.85f, 0.0f, 0.0f};
    const Vec3 nearWhite{1.85f, 0.0f, 0.0f};
    RadianceExpectation ratio;
    ratio.kind = RadianceExpectation::Kind::RatioGreaterThan;
    ratio.description = "floor beside the red wall is redder than beside the white wall (indirect colour)";
    ratio.point = nearRed;
    ratio.otherPoint = nearWhite;
    ratio.ratioFactor = 1.2f;
    d.radianceExpectations = {ratio, Positive("floor beside the red wall is lit", nearRed), Positive("floor beside the white wall is lit", nearWhite)};
    d.statsPatches = {{"near_red", nearRed, 3}, {"near_white", nearWhite, 3}, {"floor_centre", {0.0f, 0.0f, 0.3f}, 3}};
    return d;
}

// T08 (strategy comparison): closed Cornell-style box with coloured side walls.
SceneDescription BuildT08Box() {
    SceneDescription d;
    d.name = "t08_box";
    d.needsLighting = true;
    const std::uint32_t grey = AddDiffuse(d.scene, "grey", {0.7f, 0.7f, 0.7f});
    const std::uint32_t red = AddDiffuse(d.scene, "red", {0.7f, 0.1f, 0.1f});
    const std::uint32_t green = AddDiffuse(d.scene, "green", {0.1f, 0.7f, 0.1f});
    const RoomSpec box{{-1.25f, 0.0f, -1.25f}, {1.25f, 2.5f, 1.25f}, 0.15f};
    const float t = box.wallThickness;
    const Vec3 lo = box.innerMin;
    const Vec3 hi = box.innerMax;
    AddBox(d.scene, "floor", {(hi.x - lo.x) * 0.5f + t, t * 0.5f, (hi.z - lo.z) * 0.5f + t}, {0.0f, lo.y - t * 0.5f, 0.0f}, grey);
    AddBox(d.scene, "ceiling", {(hi.x - lo.x) * 0.5f + t, t * 0.5f, (hi.z - lo.z) * 0.5f + t}, {0.0f, hi.y + t * 0.5f, 0.0f}, grey);
    AddBox(d.scene, "wall_neg_x", {t * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f + t}, {lo.x - t * 0.5f, (lo.y + hi.y) * 0.5f, 0.0f}, red);
    AddBox(d.scene, "wall_pos_x", {t * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f + t}, {hi.x + t * 0.5f, (lo.y + hi.y) * 0.5f, 0.0f}, green);
    AddBox(d.scene, "wall_neg_z", {(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, t * 0.5f}, {0.0f, (lo.y + hi.y) * 0.5f, lo.z - t * 0.5f}, grey);
    AddBox(d.scene, "wall_pos_z", {(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, t * 0.5f}, {0.0f, (lo.y + hi.y) * 0.5f, hi.z + t * 0.5f}, grey);
    const MeshId tallMesh = d.scene.AddMesh(MakeBox("tall_block", {0.3f, 0.6f, 0.3f}));
    d.scene.AddInstance("tall_block", tallMesh, Mat4::Translation({-0.4f, 0.6f, -0.35f}) * Mat4::RotationY(0.3f), grey);
    const MeshId shortMesh = d.scene.AddMesh(MakeBox("short_block", {0.3f, 0.3f, 0.3f}));
    d.scene.AddInstance("short_block", shortMesh, Mat4::Translation({0.45f, 0.3f, 0.4f}) * Mat4::RotationY(-0.25f), grey);
    const std::uint32_t lamp = AddEmitter(d.scene, "ceiling_panel", {12.0f, 12.0f, 12.0f});
    AddRectangleEmitter(d.scene, "ceiling_panel", 0.8f, 0.8f, {0.0f, hi.y - 0.01f, 0.0f}, {0.0f, -1.0f, 0.0f}, lamp);
    d.camera.position = {0.0f, 1.25f, 1.15f};
    d.camera.LookAt({0.0f, 0.6f, -1.25f});
    const Vec3 floorPoint{0.0f, 0.0f, -0.6f};
    const Vec3 redWall{lo.x, 1.2f, -0.6f};
    const Vec3 backWall{0.3f, 1.4f, lo.z};
    const Vec3 shortBlockTop{0.45f, 0.6f, 0.4f};
    d.radianceExpectations = {Positive("floor is lit", floorPoint), Positive("red wall is lit", redWall), Positive("back wall is lit", backWall)};
    d.statsPatches = {{"floor", floorPoint, 3}, {"red_wall", redWall, 3}, {"back_wall", backWall, 3}, {"short_block_top", shortBlockTop, 2}};
    return d;
}

// Mirror identity and energy: a flat unit mirror on the -Z wall shows a box that stands behind the
// camera, and reflects a small wall emitter behind the camera straight back (radiance == Le).
SceneDescription BuildMirrorBox() {
    SceneDescription d;
    d.name = "mirror_box";
    d.needsLighting = true;
    const std::uint32_t grey = AddDiffuse(d.scene, "grey", {0.5f, 0.5f, 0.5f});
    const std::uint32_t orange = AddDiffuse(d.scene, "orange", {0.8f, 0.4f, 0.1f});
    const std::uint32_t mirrorMaterial = AddMirror(d.scene, "mirror", {1.0f, 1.0f, 1.0f});
    // A 4 m wide, 7 m long room: the camera stands in the middle, the mirror is on the -Z wall,
    // and the box and the wall lamp are behind the camera near the +Z wall.
    const RoomSpec longRoom{{-2.0f, 0.0f, -2.0f}, {2.0f, 2.8f, 5.0f}, 0.15f};
    const RoomInstances room = AddRoom(d.scene, "room", longRoom, {grey, grey, grey});
    // Mirror slab on the -Z wall: 1 m wide, 2 m tall, 0.02 m thick, front face at z = -1.98.
    const InstanceId mirror = AddBox(d.scene, "mirror", {0.5f, 1.0f, 0.01f}, {0.0f, 1.5f, -1.99f}, mirrorMaterial);
    (void)mirror;
    const InstanceId hiddenBox = AddBox(d.scene, "hidden_box", {0.3f, 0.4f, 0.3f}, {0.8f, 0.4f, 3.5f}, orange);
    const std::uint32_t panel = AddEmitter(d.scene, "ceiling_panel", {6.0f, 6.0f, 6.0f});
    AddRectangleEmitter(d.scene, "ceiling_panel", 1.0f, 1.0f, {0.0f, 2.79f, 2.5f}, {0.0f, -1.0f, 0.0f}, panel);
    const float wallLampRadiance = 3.0f;
    const std::uint32_t wallLamp = AddEmitter(d.scene, "wall_lamp", {wallLampRadiance, wallLampRadiance, wallLampRadiance});
    AddRectangleEmitter(d.scene, "wall_lamp", 0.5f, 0.5f, {0.0f, 1.2f, 4.99f}, {0.0f, 0.0f, -1.0f}, wallLamp);
    d.camera.position = {0.0f, 1.2f, 3.0f};
    d.camera.yawRadians = 0.0f;
    d.camera.pitchRadians = 0.0f;

    // Mirror point whose reflection shows the hidden box's front face (z = 3.2): with the camera
    // 4.98 m from the mirror plane, the reflected ray reaches z = 3.2 at s = 1.0402 and
    // x = xm * (1 + s) = 0.8, y = 1.2 + (ym - 1.2) * (1 + s) = 0.4 (derivation in docs/TESTS.md).
    const Vec3 mirrorPointBox{0.392f, 0.808f, -1.98f};
    const Vec3 mirrorPointWall{-0.3f, 1.6f, -1.98f};
    HitExpectation seesBox;
    seesBox.worldPoint = mirrorPointBox;
    seesBox.expectedStableId = hiddenBox.value;
    seesBox.description = "mirror shows the box that stands behind the camera (first non-mirror hit)";
    HitExpectation seesWall;
    seesWall.worldPoint = mirrorPointWall;
    seesWall.expectedStableId = room.wallPosZ.value;
    seesWall.description = "mirror shows the +Z wall behind the camera";
    d.expectations = {seesBox, seesWall};
    d.radianceExpectations = {
        Analytic("mirror centre reflects the wall lamp: radiance equals the emitter radiance", {0.0f, 1.2f, -1.98f}, wallLampRadiance, 1e-3f),
        Positive("mirror image of the box is lit", mirrorPointBox),
    };
    d.statsPatches = {{"mirror_lamp", {0.0f, 1.2f, -1.98f}, 2}, {"mirror_box", mirrorPointBox, 2}};
    return d;
}

std::uint32_t AddConductor(Scene& s, const char* name, Vec3 f0, float roughness) {
    Material m;
    m.name = name;
    m.type = MaterialType::RoughConductor;
    m.reflectance = f0;
    m.roughness = roughness;
    return s.AddMaterial(m);
}

// T10 furnace: a huge F0 = 1 conductor floor under a huge uniform emitter (radiance 1) facing down.
// The reflected radiance at 45 degrees must equal the single-scattering GGX directional albedo,
// which the CPU integrates numerically from the same formulas (energy loss recorded, never gain).
SceneDescription BuildT10Furnace(float roughness, const char* name, bool normalIncidence) {
    SceneDescription d;
    d.name = name;
    d.needsLighting = true;
    const std::uint32_t metal = AddConductor(d.scene, "furnace_conductor", {1.0f, 1.0f, 1.0f}, roughness);
    const MeshId floor = d.scene.AddMesh(MakeQuadXZ("floor", 200.0f, 200.0f));
    d.scene.AddInstance("floor", floor, Mat4::Identity(), metal);
    const std::uint32_t sky = AddEmitter(d.scene, "uniform_sky", {1.0f, 1.0f, 1.0f});
    AddRectangleEmitter(d.scene, "uniform_sky", 400.0f, 400.0f, {0.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, sky);
    d.camera.position = {0.0f, 1.0f, 0.0f};
    const Vec3 target = normalIncidence ? Vec3{0.0f, 0.0f, 0.0f} : Vec3{0.0f, 0.0f, -1.0f};
    d.camera.LookAt(target);  // Straight down, or 45 degrees onto the floor.
    const float cosTheta = normalIncidence ? 1.0f : 0.70710678f;
    const float alpha = roughness * roughness;
    // alpha = 1 at normal incidence has the closed form 1 - ln 2 (D = 1/pi, Lambda = (sec - 1)/2).
    const float albedo = (normalIncidence && roughness == 1.0f) ? (1.0f - 0.69314718f) : math::GgxDirectionalAlbedo(cosTheta, alpha, 1024, 1024);
    d.radianceExpectations = {
        Analytic("conductor under uniform radiance 1 reflects the GGX directional albedo (no energy gain)", target, albedo, 0.02f),
    };
    d.statsPatches = {{"floor_patch", target, 3}};
    return d;
}

// Visual scene with conductors of several roughness values next to diffuse and mirror surfaces.
SceneDescription BuildMetalsRoom() {
    SceneDescription d;
    d.name = "metals_room";
    d.needsLighting = true;
    const std::uint32_t grey = AddDiffuse(d.scene, "grey", {0.6f, 0.6f, 0.6f});
    AddRoom(d.scene, "room", kStandardRoom, {grey, grey, grey});
    const float roughness[4] = {0.05f, 0.2f, 0.45f, 0.8f};
    for (int i = 0; i < 4; ++i) {
        const std::uint32_t metal = AddConductor(d.scene, "steel", {0.56f, 0.57f, 0.58f}, roughness[i]);
        AddBox(d.scene, "metal_box", {0.3f, 0.3f, 0.3f}, {-1.35f + 0.9f * static_cast<float>(i), 0.3f, -0.6f}, metal);
    }
    const std::uint32_t copper = AddConductor(d.scene, "copper", {0.95f, 0.64f, 0.54f}, 0.25f);
    AddBox(d.scene, "copper_slab", {1.2f, 0.05f, 0.5f}, {0.0f, 0.05f, 0.3f}, copper);
    const std::uint32_t lamp = AddEmitter(d.scene, "ceiling_panel", {10.0f, 10.0f, 10.0f});
    AddRectangleEmitter(d.scene, "ceiling_panel", 0.8f, 0.8f, {0.0f, 2.79f, 0.2f}, {0.0f, -1.0f, 0.0f}, lamp);
    d.camera.position = {0.0f, 1.7f, 1.9f};
    d.camera.LookAt({0.0f, 0.3f, -0.4f});
    d.radianceExpectations = {Positive("copper slab reflects the panel", {0.0f, 0.1f, 0.3f})};
    d.statsPatches = {{"copper", {0.0f, 0.1f, 0.3f}, 3}};
    return d;
}

}  // namespace

std::vector<std::string> BuiltinSceneNames() {
    return {"rt_triangle", "rt_boxes", "t03_dark_room", "t04_sealed", "t04_open", "t07_bleed", "t08_box",
            "t09_rect_light", "t09_rect_light_large", "mirror_box", "t10_furnace_r05", "t10_furnace_r35",
            "t10_furnace_r70", "t10_furnace_r100_normal", "metals_room", "two_room"};
}

std::optional<SceneDescription> BuildBuiltinScene(std::string_view name) {
    if (name == "rt_triangle") return BuildRtTriangle();
    if (name == "rt_boxes") return BuildRtBoxes();
    if (name == "t03_dark_room") return BuildT03DarkRoom();
    if (name == "t04_sealed") return BuildT04Sealed(false);
    if (name == "t04_open") return BuildT04Sealed(true);
    if (name == "t07_bleed") return BuildT07Bleed();
    if (name == "t08_box") return BuildT08Box();
    if (name == "t09_rect_light") return BuildT09RectLight(false);
    if (name == "t09_rect_light_large") return BuildT09RectLight(true);
    if (name == "mirror_box") return BuildMirrorBox();
    if (name == "t10_furnace_r05") return BuildT10Furnace(0.05f, "t10_furnace_r05", false);
    if (name == "t10_furnace_r35") return BuildT10Furnace(0.35f, "t10_furnace_r35", false);
    if (name == "t10_furnace_r70") return BuildT10Furnace(0.70f, "t10_furnace_r70", false);
    if (name == "t10_furnace_r100_normal") return BuildT10Furnace(1.0f, "t10_furnace_r100_normal", true);
    if (name == "metals_room") return BuildMetalsRoom();
    if (name == "two_room") return BuildTwoRoomLevel().description;
    return std::nullopt;
}

}  // namespace lc
