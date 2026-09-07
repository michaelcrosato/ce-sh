// Simple collision solids for the proof (spec §14): every collider is the world-space box of a
// kit part (its mesh's object-space bounds under the instance transform; kit parts rotate about
// +Y only), so the collision solid and the visible shell are the same box. The player is a
// vertical capsule, the carried lamp a sphere, lines of sight are segments. No dynamic physics,
// nothing derived from images.
#pragma once

#include "core/ids.h"
#include "core/math/mat.h"
#include "scene/scene.h"

#include <vector>

namespace lc::game {

struct ColliderBox {
    InstanceId id;
    math::Vec3 centre;  // World-space centre.
    math::Vec3 half;    // Half extents in the box's own frame.
    float yaw = 0.0f;   // Rotation about +Y.
};

// Vertical capsule with its feet at the position: the horizontal footprint is a circle of the
// radius; the vertical extent is [feet + skin, feet + height - skin] so a floor slab touching the
// feet and a ceiling touching the top do not count as overlaps.
struct Capsule {
    float radius = 0.3f;
    float height = 1.7f;
    float skin = 0.05f;
};

class CollisionWorld {
public:
    // Colliders from the scene's instances with the listed ids. Instances whose mesh is not a box
    // or whose transform is not a yaw rotation plus translation are skipped (Skipped() counts them).
    void Build(const Scene& scene, const std::vector<InstanceId>& ids);
    // Re-derives one collider's box from an explicit transform (the door leaf follows its state).
    void SetTransform(const Scene& scene, InstanceId id, const math::Mat4& objectToWorld);

    // Moves the capsule horizontally by delta, sliding along solids. Returns the resolved feet position.
    math::Vec3 MoveCapsule(math::Vec3 feet, math::Vec3 delta, const Capsule& capsule) const;
    bool CapsuleOverlaps(math::Vec3 feet, const Capsule& capsule) const;

    // Sphere placement along a segment: the point closest to `to` (moving back toward `from`)
    // where a sphere of the radius is free of solids; `from` itself when nothing along the way is free.
    math::Vec3 SweepSphere(math::Vec3 from, math::Vec3 to, float radius) const;
    bool SphereOverlaps(math::Vec3 centre, float radius) const;

    // True when the open segment between a and b meets no solid (line of sight, occlusion).
    bool SegmentClear(math::Vec3 a, math::Vec3 b) const;

    const std::vector<ColliderBox>& Boxes() const { return boxes_; }
    std::uint32_t Skipped() const { return skipped_; }
    // A box without a scene instance (tests).
    void AddBox(const ColliderBox& box) { boxes_.push_back(box); }

    // World-space yaw box of an instance's mesh bounds; false when the mesh has no vertices or the
    // transform is not a yaw rotation plus translation.
    static bool BoxFromInstance(const Scene& scene, const Instance& inst, const math::Mat4& objectToWorld, ColliderBox& out);

private:
    std::vector<ColliderBox> boxes_;
    std::uint32_t skipped_ = 0;
};

}  // namespace lc::game
