#include "scene/primitives.h"

namespace lc {

using math::Vec3;

MeshData MakeTriangle(std::string name, Vec3 p0, Vec3 p1, Vec3 p2) {
    MeshData m;
    m.name = std::move(name);
    m.positions = {p0, p1, p2};
    m.indices = {0, 1, 2};
    return m;
}

MeshData MakeQuadXZ(std::string name, float halfX, float halfZ) {
    MeshData m;
    m.name = std::move(name);
    // Counter-clockwise seen from +Y: Cross(p1 - p0, p2 - p0) = +Y.
    m.positions = {
        {-halfX, 0.0f, halfZ},
        {halfX, 0.0f, halfZ},
        {halfX, 0.0f, -halfZ},
        {-halfX, 0.0f, -halfZ},
    };
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}

MeshData MakeBox(std::string name, Vec3 h) {
    MeshData m;
    m.name = std::move(name);
    m.positions.reserve(24);
    m.indices.reserve(36);

    struct Face {
        Vec3 n;  // Outward normal.
        Vec3 u;  // First tangent; Cross(u, v) == n.
        Vec3 v;  // Second tangent.
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},   // +X
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},   // -X
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},   // +Y
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},   // -Y
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},    // +Z
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},  // -Z
    };

    for (const Face& f : faces) {
        const std::uint32_t base = static_cast<std::uint32_t>(m.positions.size());
        // Corners in counter-clockwise order around the outward normal.
        const float su[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        const float sv[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        for (int c = 0; c < 4; ++c) {
            const Vec3 axisSum = f.n + f.u * su[c] + f.v * sv[c];  // Each axis appears exactly once.
            m.positions.push_back(axisSum * h);
        }
        m.indices.insert(m.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
    return m;
}

}  // namespace lc
