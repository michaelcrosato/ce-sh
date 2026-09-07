// Triangle mesh data as authored or generated. Positions are in metres in object space; triangles
// are counter-clockwise when seen from outside in the right-handed, +Y-up world.
#pragma once

#include "core/math/vec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lc {

struct MeshData {
    std::string name;
    std::vector<math::Vec3> positions;
    std::vector<std::uint32_t> indices;  // Three per triangle.

    std::uint32_t TriangleCount() const { return static_cast<std::uint32_t>(indices.size() / 3); }
};

inline constexpr std::uint32_t kMaxMeshTriangles = 1'000'000;
inline constexpr float kMinTriangleArea = 1e-10f;  // Square metres; smaller triangles are degenerate.

// Returns an empty list when the mesh is usable; otherwise one message per problem.
std::vector<std::string> ValidateMesh(const MeshData& mesh);

// Geometric (unnormalized) normal of a triangle: Cross(p1 - p0, p2 - p0).
math::Vec3 TriangleNormal(math::Vec3 p0, math::Vec3 p1, math::Vec3 p2);

}  // namespace lc
