// The only two ray queries the renderer performs. Both use inline RayQuery (DXR Tier 1.1) on the
// single scene acceleration structure. Camera, reflection, indirect, and source-visibility rays
// all go through these functions, so every path sees the same world.
#ifndef LC_RAY_INTERFACE_HLSLI
#define LC_RAY_INTERFACE_HLSLI

struct ClosestHit {
    bool hit;
    float t;                // Ray parameter of the hit; hit point = origin + t * direction.
    uint instanceIndex;     // Index into the TLAS instance array (== InstanceRecord index).
    uint instanceId;        // InstanceID from the instance descriptor (== scene stable id).
    uint primitiveIndex;    // Triangle index within the mesh.
    float2 barycentrics;    // Weights of vertices 1 and 2; vertex 0 weight = 1 - x - y.
    bool frontFace;         // DXR default rule: dot(cross(p1 - p0, p2 - p0), direction) < 0 (no instance flag).
    float3x4 objectToWorld; // Instance transform at trace time.
};

// Nearest accepted surface along the finite segment [tMin, tMax]. All geometry is opaque; the
// candidate loop is still written out so a future non-opaque path cannot silently take a
// first-hit shortcut. Never uses RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH.
ClosestHit TraceClosest(RaytracingAccelerationStructure scene, float3 origin, float3 direction,
                        float tMin, float tMax) {
    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = tMin;
    ray.Direction = direction;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    q.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed()) {
        // With RAY_FLAG_FORCE_OPAQUE and triangle-only geometry no candidate reaches this loop.
    }

    ClosestHit h = (ClosestHit)0;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        h.hit = true;
        h.t = q.CommittedRayT();
        h.instanceIndex = q.CommittedInstanceIndex();
        h.instanceId = q.CommittedInstanceID();
        h.primitiveIndex = q.CommittedPrimitiveIndex();
        h.barycentrics = q.CommittedTriangleBarycentrics();
        h.frontFace = q.CommittedTriangleFrontFace();
        h.objectToWorld = q.CommittedObjectToWorld3x4();
    }
    return h;
}

// True when the finite segment [tMin, tMax] reaches its end without touching any surface. This
// query may stop at the first blocker because only the yes/no answer is needed.
bool TraceVisibility(RaytracingAccelerationStructure scene, float3 origin, float3 direction,
                     float tMin, float tMax) {
    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = tMin;
    ray.Direction = direction;
    ray.TMax = tMax;

    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed()) {
    }
    return q.CommittedStatus() == COMMITTED_NOTHING;
}

#endif  // LC_RAY_INTERFACE_HLSLI
