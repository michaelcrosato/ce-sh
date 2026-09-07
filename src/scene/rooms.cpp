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

InstanceId AddSlab(Scene& scene, const std::string& name, Vec3 min, Vec3 max, std::uint32_t material) {
    const Vec3 half = (max - min) * 0.5f;
    const Vec3 centre = (max + min) * 0.5f;
    return AddBox(scene, name, half, centre, material);
}

void AddWallWithOpening(Scene& scene, const std::string& p, Vec3 min, Vec3 max, bool axisX, const WallOpening& o, std::uint32_t material) {
    const float lo = axisX ? min.x : min.z;
    const float hi = axisX ? max.x : max.z;
    const float a0 = o.centre - o.width * 0.5f;
    const float a1 = o.centre + o.width * 0.5f;
    if (a0 <= lo || a1 >= hi || min.y + o.height >= max.y) {
        throw Error("wall opening does not fit inside the wall");
    }
    auto piece = [&](const std::string& suffix, float from, float to, float y0, float y1) {
        Vec3 pmin = min;
        Vec3 pmax = max;
        if (axisX) {
            pmin.x = from;
            pmax.x = to;
        } else {
            pmin.z = from;
            pmax.z = to;
        }
        pmin.y = y0;
        pmax.y = y1;
        AddSlab(scene, p + suffix, pmin, pmax, material);
    };
    piece("_side_a", lo, a0, min.y, max.y);
    piece("_side_b", a1, hi, min.y, max.y);
    piece("_lintel", a0, a1, min.y + o.height, max.y);
}

DoorHandle AddDoorLeaf(Scene& scene, const std::string& name, const DoorLeafSpec& s, std::uint32_t material) {
    // Local leaf box: x along widthDir, y up, z along thicknessDir; built axis-aligned then oriented.
    const Vec3 w = math::Normalize(s.widthDir);
    const Vec3 t = math::Normalize(s.thicknessDir);
    const float halfW = (s.width + 2.0f * s.overlap) * 0.5f;
    const float halfH = (s.height + s.overlap) * 0.5f;
    const float halfT = s.thickness * 0.5f;
    const Vec3 centre = s.hingeBase + w * (s.width * 0.5f) + Vec3{0.0f, halfH, 0.0f} + t * (s.inset + halfT);
    // Orientation: yaw so that local +X maps to widthDir. The leaf box is symmetric about its
    // thickness axis, so local +Z may map to either +thicknessDir or -thicknessDir; the world-space
    // centre above already places the slab inside the wall along thicknessDir.
    if (std::fabs(math::Dot(w, t)) > 1e-3f || std::fabs(w.y) > 1e-3f || std::fabs(t.y) > 1e-3f) {
        throw Error("AddDoorLeaf: widthDir and thicknessDir must be perpendicular horizontal axes");
    }
    const float yaw = std::atan2(w.z, w.x) * -1.0f;  // RotationY(yaw) maps +X to (cos yaw, 0, -sin yaw).
    const Mat4 orientation = Mat4::RotationY(yaw);
    DoorHandle h;
    h.hinge = s.hingeBase + t * (s.inset + halfT);
    h.closedTransform = Mat4::Translation(centre) * orientation;
    h.centre = centre;
    h.width = s.width;
    const MeshId mesh = scene.AddMesh(MakeBox(name, {halfW, halfH, halfT}));
    h.id = scene.AddInstance(name, mesh, h.closedTransform, material);
    return h;
}

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
    h.centre = centre;
    h.width = d.doorWidth;
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
