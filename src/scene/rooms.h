// Generated room kit: slabs with real thickness (spec §6: ~0.15 m walls, ~0.04 m doors), doorways
// with frames, hinged doors, and rectangular one-sided emitters. Every part is an opaque box or
// quad instance; nothing here is lighting.
#pragma once

#include "scene/scene.h"

#include <string>

namespace lc {

struct RoomMaterials {
    std::uint32_t floor = 0;
    std::uint32_t ceiling = 0;
    std::uint32_t walls = 0;
};

// The enclosed air volume is [innerMin, innerMax]; slabs of the given thickness surround it.
struct RoomSpec {
    math::Vec3 innerMin;
    math::Vec3 innerMax;
    float wallThickness = 0.15f;
};

struct RoomInstances {
    InstanceId floor, ceiling, wallNegX, wallPosX, wallNegZ, wallPosZ;
};

// Adds floor, ceiling, and four walls. Walls along X span the full depth so corners are solid.
RoomInstances AddRoom(Scene& scene, const std::string& namePrefix, const RoomSpec& spec, const RoomMaterials& materials);

// Adds a +Z wall with a doorway (two side pieces and a lintel) instead of a solid slab. The
// opening is doorWidth wide, doorHeight tall from the floor, centred at x = doorCentreX.
struct DoorwaySpec {
    float doorCentreX = 0.0f;
    float doorWidth = 0.9f;
    float doorHeight = 2.1f;
};
void AddPosZWallWithDoorway(Scene& scene, const std::string& namePrefix, const RoomSpec& spec, const DoorwaySpec& doorway,
                            std::uint32_t material);

// A 0.04 m door leaf that overlaps the frame by 0.01 m on both sides and at the top so a closed
// door leaves no light gap. Hinged on its -X edge; positive angles swing it into the room (-Z).
struct DoorSpec {
    float thickness = 0.04f;
    float overlap = 0.01f;
    float inset = 0.005f;  // Gap from the wall's inner face to the door's inner face (avoids coplanar faces).
};
struct DoorHandle {
    InstanceId id;
    math::Vec3 hinge;          // World-space hinge line base point (on the floor).
    math::Mat4 closedTransform;
    math::Vec3 centre;         // World-space centre of the closed leaf (interaction point).
    float width = 0.9f;
};
DoorHandle AddDoor(Scene& scene, const std::string& name, const RoomSpec& spec, const DoorwaySpec& doorway, const DoorSpec& door,
                   std::uint32_t material);

// General door leaf: the hinge line rises from hingeBase; the leaf extends `width` along widthDir
// (a horizontal unit axis) and `thickness` along thicknessDir (the wall's thickness direction,
// starting `inset` from the room-side face at hingeBase). Positive angles rotate about +Y.
struct DoorLeafSpec {
    math::Vec3 hingeBase;
    math::Vec3 widthDir{1.0f, 0.0f, 0.0f};
    math::Vec3 thicknessDir{0.0f, 0.0f, 1.0f};
    float width = 0.9f;
    float height = 2.1f;
    float thickness = 0.04f;
    float inset = 0.005f;
    float overlap = 0.01f;
};
DoorHandle AddDoorLeaf(Scene& scene, const std::string& name, const DoorLeafSpec& spec, std::uint32_t material);
math::Mat4 DoorTransform(const DoorHandle& door, float angleRadians);

// Axis-aligned slab from min to max corner (a box instance with its own mesh).
InstanceId AddSlab(Scene& scene, const std::string& name, math::Vec3 min, math::Vec3 max, std::uint32_t material);

// A wall along X or Z with a rectangular opening: two side pieces and a lintel. `axisX` selects a
// wall that runs along X (opening measured in x) versus along Z (opening measured in z).
struct WallOpening {
    float centre = 0.0f;   // Opening centre along the wall's running axis.
    float width = 0.9f;
    float height = 2.1f;   // From the wall's bottom.
};
void AddWallWithOpening(Scene& scene, const std::string& namePrefix, math::Vec3 min, math::Vec3 max, bool axisX, const WallOpening& opening,
                        std::uint32_t material);

// A rectangle emitter (two triangles) of the given size, centred at position, with its emitting
// side facing `facing` (one of +/-X, +/-Y, +/-Z). The mesh is named after the instance.
InstanceId AddRectangleEmitter(Scene& scene, const std::string& name, float width, float height, math::Vec3 position,
                               math::Vec3 facing, std::uint32_t emitterMaterial);

// Box instance helper: adds a unique mesh and one instance.
InstanceId AddBox(Scene& scene, const std::string& name, math::Vec3 halfExtents, math::Vec3 centre, std::uint32_t material);

// Orientation that maps the +Y face of a MakeQuadXZ quad to the requested axis direction.
math::Mat4 QuadFacing(math::Vec3 facing);

}  // namespace lc
