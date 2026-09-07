#include "scene/mesh.h"

#include <format>

namespace lc {

math::Vec3 TriangleNormal(math::Vec3 p0, math::Vec3 p1, math::Vec3 p2) {
    return math::Cross(p1 - p0, p2 - p0);
}

std::vector<std::string> ValidateMesh(const MeshData& mesh) {
    std::vector<std::string> problems;
    const std::string name = mesh.name.empty() ? "<unnamed>" : mesh.name;

    if (mesh.positions.empty()) {
        problems.push_back(std::format("mesh '{}' has no vertices", name));
    }
    if (mesh.indices.empty()) {
        problems.push_back(std::format("mesh '{}' has no indices", name));
    }
    if (mesh.indices.size() % 3 != 0) {
        problems.push_back(std::format("mesh '{}' index count {} is not a multiple of 3", name, mesh.indices.size()));
    }
    if (mesh.indices.size() / 3 > kMaxMeshTriangles) {
        problems.push_back(std::format("mesh '{}' has {} triangles, above the limit of {}", name,
                                       mesh.indices.size() / 3, kMaxMeshTriangles));
    }
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        if (!math::IsFinite(mesh.positions[i])) {
            problems.push_back(std::format("mesh '{}' vertex {} is not finite", name, i));
            break;
        }
    }
    for (std::size_t i = 0; i < mesh.indices.size(); ++i) {
        if (mesh.indices[i] >= mesh.positions.size()) {
            problems.push_back(std::format("mesh '{}' index {} references vertex {} but only {} exist", name, i,
                                           mesh.indices[i], mesh.positions.size()));
            return problems;  // Later checks would read out of range.
        }
    }
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const math::Vec3 n = TriangleNormal(mesh.positions[mesh.indices[t]], mesh.positions[mesh.indices[t + 1]],
                                            mesh.positions[mesh.indices[t + 2]]);
        if (0.5f * math::Length(n) < kMinTriangleArea) {
            problems.push_back(std::format("mesh '{}' triangle {} is degenerate (area below {})", name, t / 3,
                                           kMinTriangleArea));
        }
    }
    return problems;
}

}  // namespace lc
