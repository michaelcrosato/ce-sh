// Locally generated low-polygon kit. Every primitive uses outward counter-clockwise faces and flat
// (per-face) vertices so geometric normals are exact.
#pragma once

#include "scene/mesh.h"

namespace lc {

MeshData MakeTriangle(std::string name, math::Vec3 p0, math::Vec3 p1, math::Vec3 p2);

// Quad in the XZ plane centred at the origin, facing +Y, spanning [-halfX, halfX] x [-halfZ, halfZ].
MeshData MakeQuadXZ(std::string name, float halfX, float halfZ);

// Axis-aligned box centred at the origin: 24 vertices (4 per face), 12 triangles.
MeshData MakeBox(std::string name, math::Vec3 halfExtents);

}  // namespace lc
