#include "scene/rooms.h"

#include "core/error.h"
#include "scene/primitives.h"

#include <cmath>

namespace lc {

using math::Mat4;
using math::Vec3;

InstanceId AddBox(Scene& scene, const std::string& name, Vec3 halfExtents, Vec3 centre, std::uint32_t material) {
    const MeshId mesh = scene.AddMesh(MakeBox(name, halfExtents));
    return scene.AddInstance(name, mesh, Mat4::Translation(centre), material);
}

namespace {

InstanceId AddSlab(Scene& scene, const std::string& name, Vec3 min, Vec3 max, std::uint32_t material) {
    const Vec3 half = (max - min) * 0.5f;
    const Vec3 centre = (max + min) * 0.5f;
    return AddBox(scene, name, half, centre, material);
}

}  // namespace

RoomInstances AddRoom(Scene& scene, const std::string& p, const RoomSpec& s, const RoomMaterials& m) {
    const float t = s.wallThickness;
    const Vec3 lo = s.innerMin;
    const Vec3 hi = s.innerMax;
    RoomInstances r;
    r.floor = AddSlab(scene, p + "_floor", {lo.x - t, lo.y - t, lo.z - t}, {hi.x + t, lo.y, hi.z + t}, m.floor);
    r.ceiling = AddSlab(scene, p + "_ceiling", {lo.x - t, hi.y, lo.z - t}, {hi.x + t, hi.y + t, hi.z + t}, m.ceiling);
    r.wallNegX = AddSlab(scene, p + "_wall_neg_x", {lo.x - t, lo.y, lo.z - t}, {lo.x, hi.y, hi.z + t}, m.walls);
    r.wallPosX = AddSlab(scene, p + "_wall_pos_x", {hi.x, lo.y, lo.z - t}, {hi.x + t, hi.y, hi.z + t}, m.walls);
    r.wallNegZ = AddSlab(scene, p + "_wall_neg_z", {lo.x, lo.y, lo.z - t}, {hi.x, hi.y, lo.z}, m.walls);
    r.wallPosZ = AddSlab(scene, p + "_wall_pos_z", {lo.x, lo.y, hi.z}, {hi.x, hi.y, hi.z + t}, m.walls);
    return r;
}

void AddPosZWallWithDoorway(Scene& scene, const std::string& p, const RoomSpec& s, const DoorwaySpec& d, std::uint32_t material) {
    const float t = s.wallThickness;
    const Vec3 lo = s.innerMin;
    const Vec3 hi = s.innerMax;
    const float x0 = d.doorCentreX - d.doorWidth * 0.5f;
    const float x1 = d.doorCentreX + d.doorWidth * 0.5f;
    if (x0 <= lo.x || x1 >= hi.x || lo.y + d.doorHeight >= hi.y) {
        throw Error("doorway does not fit inside the wall");
    }
    AddSlab(scene, p + "_wall_pos_z_left", {lo.x, lo.y, hi.z}, {x0, hi.y, hi.z + t}, material);
    AddSlab(scene, p + "_wall_pos_z_right", {x1, lo.y, hi.z}, {hi.x, hi.y, hi.z + t}, material);
    AddSlab(scene, p + "_wall_pos_z_lintel", {x0, lo.y + d.doorHeight, hi.z}, {x1, hi.y, hi.z + t}, material);
}

DoorHandle AddDoor(Scene& scene, const std::string& name, const RoomSpec& s, const DoorwaySpec& d, const DoorSpec& door,
                   std::uint32_t material) {
    const float x0 = d.doorCentreX - d.doorWidth * 0.5f - door.overlap;
    const float x1 = d.doorCentreX + d.doorWidth * 0.5f + door.overlap;
    const float y0 = s.innerMin.y;
    const float y1 = s.innerMin.y + d.doorHeight + door.overlap;
    const float z0 = s.innerMax.z + door.inset;
    const float z1 = z0 + door.thickness;
    const Vec3 half{(x1 - x0) * 0.5f, (y1 - y0) * 0.5f, (z1 - z0) * 0.5f};
    const Vec3 centre{(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f};
    DoorHandle h;
    h.hinge = {x0, y0, (z0 + z1) * 0.5f};
    h.closedTransform = Mat4::Translation(centre);
    const MeshId mesh = scene.AddMesh(MakeBox(name, half));
    h.id = scene.AddInstance(name, mesh, h.closedTransform, material);
    return h;
}

Mat4 DoorTransform(const DoorHandle& door, float angleRadians) {
    return Mat4::Translation(door.hinge) * Mat4::RotationY(angleRadians) * Mat4::Translation(-door.hinge) * door.closedTransform;
}

Mat4 QuadFacing(Vec3 facing) {
    // MakeQuadXZ faces +Y. Rotations follow the right-hand rule (RotationX maps +Y toward +Z).
    if (facing.y > 0.5f) return Mat4::Identity();
    if (facing.y < -0.5f) return Mat4::RotationX(math::kPi);
    if (facing.z > 0.5f) return Mat4::RotationX(math::kPi * 0.5f);
    if (facing.z < -0.5f) return Mat4::RotationX(-math::kPi * 0.5f);
    if (facing.x > 0.5f) return Mat4::RotationZ(-math::kPi * 0.5f);
    if (facing.x < -0.5f) return Mat4::RotationZ(math::kPi * 0.5f);
    throw Error("QuadFacing: facing must be an axis direction");
}

InstanceId AddRectangleEmitter(Scene& scene, const std::string& name, float width, float height, Vec3 position, Vec3 facing,
                               std::uint32_t emitterMaterial) {
    const MeshId mesh = scene.AddMesh(MakeQuadXZ(name, width * 0.5f, height * 0.5f));
    return scene.AddInstance(name, mesh, Mat4::Translation(position) * QuadFacing(facing), emitterMaterial);
}

}  // namespace lc
