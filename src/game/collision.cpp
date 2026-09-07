#include "game/collision.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>

namespace lc::game {

using math::Vec3;

namespace {

// World (x, z) -> the box's local frame. RotationY(yaw) maps local (x, z) to (c x + s z, -s x + c z).
void ToLocal(const ColliderBox& b, float wx, float wz, float& lx, float& lz) {
    const float c = std::cos(b.yaw);
    const float s = std::sin(b.yaw);
    const float dx = wx - b.centre.x;
    const float dz = wz - b.centre.z;
    lx = c * dx - s * dz;
    lz = s * dx + c * dz;
}

void ToWorld(const ColliderBox& b, float lx, float lz, float& wx, float& wz) {
    const float c = std::cos(b.yaw);
    const float s = std::sin(b.yaw);
    wx = b.centre.x + c * lx + s * lz;
    wz = b.centre.z - s * lx + c * lz;
}

bool VerticalOverlap(const ColliderBox& b, float yMin, float yMax) {
    return (b.centre.y - b.half.y) < yMax && (b.centre.y + b.half.y) > yMin;
}

// Pushes a circle of `radius` at (px, pz) out of the box footprint. When the centre lies inside
// the footprint, the circle leaves through the face it came in by (the previous position decides),
// or through the nearest face when the previous position was inside as well. Returns true when it moved.
bool ResolveCircle(const ColliderBox& b, float prevX, float prevZ, float& px, float& pz, float radius) {
    float lx = 0.0f;
    float lz = 0.0f;
    ToLocal(b, px, pz, lx, lz);
    const float qx = std::clamp(lx, -b.half.x, b.half.x);
    const float qz = std::clamp(lz, -b.half.z, b.half.z);
    const float ex = lx - qx;
    const float ez = lz - qz;
    const float d2 = ex * ex + ez * ez;
    if (d2 >= radius * radius) {
        return false;
    }
    float nx = 0.0f;
    float nz = 0.0f;
    float push = 0.0f;
    if (d2 > 1e-12f) {
        const float d = std::sqrt(d2);
        nx = ex / d;
        nz = ez / d;
        push = radius - d;
    } else {
        float plx = 0.0f;
        float plz = 0.0f;
        ToLocal(b, prevX, prevZ, plx, plz);
        const float penX = b.half.x - std::fabs(lx);
        const float penZ = b.half.z - std::fabs(lz);
        if (std::fabs(plz) > b.half.z && std::fabs(plx) <= b.half.x) {
            nz = plz >= 0.0f ? 1.0f : -1.0f;  // Came in through a Z face.
            push = (nz > 0.0f ? b.half.z - lz : b.half.z + lz) + radius;
        } else if (std::fabs(plx) > b.half.x) {
            nx = plx >= 0.0f ? 1.0f : -1.0f;  // Came in through an X face.
            push = (nx > 0.0f ? b.half.x - lx : b.half.x + lx) + radius;
        } else if (penX < penZ) {
            nx = lx >= 0.0f ? 1.0f : -1.0f;
            push = penX + radius;
        } else {
            nz = lz >= 0.0f ? 1.0f : -1.0f;
            push = penZ + radius;
        }
    }
    lx += nx * push;
    lz += nz * push;
    ToWorld(b, lx, lz, px, pz);
    return true;
}

bool CircleOverlaps(const ColliderBox& b, float px, float pz, float radius) {
    float lx = 0.0f;
    float lz = 0.0f;
    ToLocal(b, px, pz, lx, lz);
    const float ex = lx - std::clamp(lx, -b.half.x, b.half.x);
    const float ez = lz - std::clamp(lz, -b.half.z, b.half.z);
    return ex * ex + ez * ez < radius * radius;
}

// Segment versus the box in its local frame (slab test on the open segment).
bool SegmentHitsBox(const ColliderBox& b, Vec3 a, Vec3 d) {
    const float c = std::cos(b.yaw);
    const float s = std::sin(b.yaw);
    const Vec3 ra = a - b.centre;
    const float ax = c * ra.x - s * ra.z;
    const float az = s * ra.x + c * ra.z;
    const float dx = c * d.x - s * d.z;
    const float dz = s * d.x + c * d.z;
    const float origin[3] = {ax, ra.y, az};
    const float dir[3] = {dx, d.y, dz};
    const float half[3] = {b.half.x, b.half.y, b.half.z};
    float tMin = 1e-4f;
    float tMax = 1.0f - 1e-4f;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dir[i]) < 1e-9f) {
            if (std::fabs(origin[i]) > half[i]) return false;
            continue;
        }
        float t0 = (-half[i] - origin[i]) / dir[i];
        float t1 = (half[i] - origin[i]) / dir[i];
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax) return false;
    }
    return true;
}

}  // namespace

bool CollisionWorld::BoxFromInstance(const Scene& scene, const Instance& inst, const math::Mat4& m, ColliderBox& out) {
    const std::vector<Vec3>& positions = scene.Meshes()[inst.mesh.value].data.positions;
    if (positions.empty()) return false;
    // Yaw-only rotation plus translation: row 1 is (0, 1, 0, ty) and column 1 is (0, 1, 0).
    const float e = 1e-4f;
    if (std::fabs(m.m[1][1] - 1.0f) > e || std::fabs(m.m[1][0]) > e || std::fabs(m.m[1][2]) > e || std::fabs(m.m[0][1]) > e || std::fabs(m.m[2][1]) > e) {
        return false;
    }
    Vec3 lo = positions[0];
    Vec3 hi = positions[0];
    for (const Vec3& p : positions) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    out.id = inst.id;
    out.half = (hi - lo) * 0.5f;
    out.centre = m.TransformPoint((lo + hi) * 0.5f);
    out.yaw = std::atan2(m.m[0][2], m.m[0][0]);
    return out.half.x > 0.0f && out.half.y > 0.0f && out.half.z > 0.0f;
}

void CollisionWorld::Build(const Scene& scene, const std::vector<InstanceId>& ids) {
    boxes_.clear();
    skipped_ = 0;
    for (const InstanceId id : ids) {
        const Instance* inst = scene.FindInstance(id);
        ColliderBox box;
        if (inst != nullptr && BoxFromInstance(scene, *inst, inst->objectToWorld, box)) {
            boxes_.push_back(box);
        } else {
            ++skipped_;
        }
    }
    if (skipped_ != 0) {
        log::Warn("collision: {} collider(s) skipped (not a box, or not a yaw-only placement)", skipped_);
    }
}

void CollisionWorld::SetTransform(const Scene& scene, InstanceId id, const math::Mat4& objectToWorld) {
    const Instance* inst = scene.FindInstance(id);
    if (inst == nullptr) return;
    for (ColliderBox& b : boxes_) {
        if (b.id.value == id.value) {
            ColliderBox updated;
            if (BoxFromInstance(scene, *inst, objectToWorld, updated)) b = updated;
            return;
        }
    }
}

Vec3 CollisionWorld::MoveCapsule(Vec3 feet, Vec3 delta, const Capsule& capsule) const {
    const float yMin = feet.y + capsule.skin;
    const float yMax = feet.y + capsule.height - capsule.skin;
    // Substeps no longer than a quarter radius keep the centre from jumping deep into a solid.
    const float length = std::sqrt(delta.x * delta.x + delta.z * delta.z);
    const int steps = std::max(1, static_cast<int>(std::ceil(length / (capsule.radius * 0.25f))));
    Vec3 p = feet;
    for (int step = 1; step <= steps; ++step) {
        const Vec3 prev = p;
        p.x = feet.x + delta.x * static_cast<float>(step) / static_cast<float>(steps);
        p.z = feet.z + delta.z * static_cast<float>(step) / static_cast<float>(steps);
        for (int iteration = 0; iteration < 4; ++iteration) {
            bool moved = false;
            for (const ColliderBox& b : boxes_) {
                if (!VerticalOverlap(b, yMin, yMax)) continue;
                moved = ResolveCircle(b, prev.x, prev.z, p.x, p.z, capsule.radius) || moved;
            }
            if (!moved) break;
        }
        // The resolved point becomes the origin of the next substep: the remaining delta is re-applied from it.
        feet.x = p.x - delta.x * static_cast<float>(step) / static_cast<float>(steps);
        feet.z = p.z - delta.z * static_cast<float>(step) / static_cast<float>(steps);
    }
    p.y = yMin - capsule.skin;
    return p;
}

bool CollisionWorld::CapsuleOverlaps(Vec3 feet, const Capsule& capsule) const {
    const float yMin = feet.y + capsule.skin;
    const float yMax = feet.y + capsule.height - capsule.skin;
    for (const ColliderBox& b : boxes_) {
        if (VerticalOverlap(b, yMin, yMax) && CircleOverlaps(b, feet.x, feet.z, capsule.radius)) return true;
    }
    return false;
}

bool CollisionWorld::SphereOverlaps(Vec3 centre, float radius) const {
    for (const ColliderBox& b : boxes_) {
        if (VerticalOverlap(b, centre.y - radius, centre.y + radius) && CircleOverlaps(b, centre.x, centre.z, radius)) return true;
    }
    return false;
}

Vec3 CollisionWorld::SweepSphere(Vec3 from, Vec3 to, float radius) const {
    constexpr int kSteps = 16;
    for (int i = kSteps; i >= 1; --i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSteps);
        const Vec3 p = math::Lerp(from, to, t);
        if (!SphereOverlaps(p, radius)) return p;
    }
    return from;
}

bool CollisionWorld::SegmentClear(Vec3 a, Vec3 b) const {
    const Vec3 d = b - a;
    for (const ColliderBox& box : boxes_) {
        if (SegmentHitsBox(box, a, d)) return false;
    }
    return true;
}

}  // namespace lc::game
