// RGB path tracer (spec §12): one camera path per pixel per dispatch, at most maxHits surface hits
// including the camera hit and mirror hits, next-event estimation at every non-delta surface,
// BSDF sampling for the continuation, multiple importance sampling (power heuristic, solid-angle
// measure) between the two, emission counted once, delta reflection handled separately, black
// environment, no Russian roulette. Every ray type traverses the same TLAS (spec H04).
//
// Outputs: accumulation sums (u4, u5) for progressive reference and raw modes, the running mean as
// linear radiance (u1), the display image after fixed exposure and sRGB encoding (u0), and the
// identity of the first non-mirror surface (u2) for the mirror tests.
#include "shared/layouts.hlsli"
#include "shared/math.hlsli"
#include "shared/sampling.hlsli"
#include "shared/materials.hlsli"
#include "trace/ray_interface.hlsli"

ConstantBuffer<FrameConstants> gFrame : register(b0);
ConstantBuffer<IntegratorConstants> gIntegrator : register(b1);
RaytracingAccelerationStructure gScene : register(t0);
StructuredBuffer<InstanceRecord> gInstances : register(t1);
StructuredBuffer<MeshRecord> gMeshes : register(t2);
StructuredBuffer<float3> gPositions : register(t3);
StructuredBuffer<uint> gIndices : register(t4);
StructuredBuffer<MaterialRecord> gMaterials : register(t5);
StructuredBuffer<EmitterRecord> gEmitters : register(t6);
StructuredBuffer<EmitterTriangle> gEmitterTriangles : register(t7);
RWTexture2D<float4> gDisplay : register(u0);
RWTexture2D<float4> gLinear : register(u1);
RWTexture2D<uint4> gHitInfo : register(u2);
RWTexture2D<float4> gAccum : register(u4);
RWTexture2D<float4> gAccumSq : register(u5);
RWStructuredBuffer<uint> gStats : register(u6);

// ---------------------------------------------------------------------------------------------
// Geometry helpers

struct TriangleWorld {
    float3 p0;
    float3 p1;
    float3 p2;
    float3 geometricNormal;  // Unit, from the winding (unflipped).
};

TriangleWorld LoadTriangle(uint instanceIndex, uint primitiveIndex) {
    const InstanceRecord inst = gInstances[instanceIndex];
    const MeshRecord mesh = gMeshes[inst.meshIndex];
    const uint base = mesh.firstIndex + primitiveIndex * 3u;
    TriangleWorld t;
    t.p0 = TransformPoint(inst.objectToWorldRow, gPositions[mesh.firstVertex + gIndices[base + 0u]]);
    t.p1 = TransformPoint(inst.objectToWorldRow, gPositions[mesh.firstVertex + gIndices[base + 1u]]);
    t.p2 = TransformPoint(inst.objectToWorldRow, gPositions[mesh.firstVertex + gIndices[base + 2u]]);
    t.geometricNormal = normalize(cross(t.p1 - t.p0, t.p2 - t.p0));
    return t;
}

struct Surface {
    float3 position;
    float3 geometricNormal;  // Unflipped.
    float3 normal;           // Flipped toward the incoming ray (two-sided shading).
    MaterialRecord material;
    uint instanceIndex;
    uint stableId;
    uint primitiveIndex;
    uint emitterIndex;
};

Surface ResolveSurface(ClosestHit hit, float3 rayDirection) {
    const TriangleWorld tri = LoadTriangle(hit.instanceIndex, hit.primitiveIndex);
    const InstanceRecord inst = gInstances[hit.instanceIndex];
    Surface s;
    const float b1 = hit.barycentrics.x;
    const float b2 = hit.barycentrics.y;
    s.position = tri.p0 * (1.0 - b1 - b2) + tri.p1 * b1 + tri.p2 * b2;
    s.geometricNormal = tri.geometricNormal;
    s.normal = dot(tri.geometricNormal, rayDirection) < 0.0 ? tri.geometricNormal : -tri.geometricNormal;
    s.material = gMaterials[inst.materialIndex];
    s.instanceIndex = hit.instanceIndex;
    s.stableId = inst.stableId;
    s.primitiveIndex = hit.primitiveIndex;
    s.emitterIndex = inst.emitterIndex;
    return s;
}

// ---------------------------------------------------------------------------------------------
// Emitter sampling

struct LightSample {
    float3 position;
    float3 normal;    // Emitting side.
    float3 radiance;
    float pdfArea;    // Probability density per unit area over all emitters: selectionPdf / area.
    bool valid;
};

LightSample SampleEmitters(float uSelect, float uTriangle, float2 uPoint) {
    LightSample ls = (LightSample)0;
    const uint count = gIntegrator.emitterCount;
    if (count == 0u) {
        return ls;
    }
    uint e = 0u;
    for (; e + 1u < count; ++e) {
        if (uSelect < gEmitters[e].selectionCdf) {
            break;
        }
    }
    const EmitterRecord em = gEmitters[e];
    uint t = em.firstTriangle;
    const uint last = em.firstTriangle + em.triangleCount - 1u;
    for (; t < last; ++t) {
        if (uTriangle < gEmitterTriangles[t].cdf) {
            break;
        }
    }
    const EmitterTriangle et = gEmitterTriangles[t];
    const TriangleWorld tri = LoadTriangle(et.instanceIndex, et.primitiveIndex);
    const float3 b = UniformTriangleBarycentrics(uPoint);
    ls.position = tri.p0 * b.x + tri.p1 * b.y + tri.p2 * b.z;
    ls.normal = tri.geometricNormal;
    ls.radiance = em.radiance;
    ls.pdfArea = em.selectionPdf / em.area;
    ls.valid = em.area > 0.0 && em.selectionPdf > 0.0;
    return ls;
}

float EmitterPdfArea(uint emitterIndex) {
    const EmitterRecord em = gEmitters[emitterIndex];
    return em.area > 0.0 ? em.selectionPdf / em.area : 0.0;
}

float PowerHeuristic(float a, float b) {
    const float a2 = a * a;
    const float b2 = b * b;
    return a2 / (a2 + b2);
}

// True when the finite segment between two offset surface points is unoccluded.
bool Unoccluded(float3 from, float3 fromNormal, float3 to, float3 toNormal) {
    const float3 origin = OffsetRay(from, fromNormal);
    const float3 target = OffsetRay(to, toNormal);
    const float3 segment = target - origin;
    return TraceVisibility(gScene, origin, segment, 0.0, 1.0 - 1e-4);
}

// ---------------------------------------------------------------------------------------------
// Camera

float3 CameraRayDirection(uint2 pixel, float2 jitter) {
    const float2 uv = (float2(pixel) + jitter) * gFrame.invRenderSize;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float3 dirView = float3(ndc.x * gFrame.tanHalfFovY * gFrame.aspectRatio, ndc.y * gFrame.tanHalfFovY, -1.0);
    return normalize(mul((float3x3)gFrame.viewToWorld, dirView));
}

// ---------------------------------------------------------------------------------------------
// Integrator

struct PathResult {
    float3 radiance;
    uint firstSolidId;      // Stable id of the first non-mirror surface, or LC_MISS_ID.
    uint firstSolidPrim;
    uint mirrorBounces;     // Mirror hits before the first solid surface.
    uint flags;
};

PathResult TracePath(uint2 pixel, SampleKey key) {
    PathResult result;
    result.radiance = 0.0;
    result.firstSolidId = LC_MISS_ID;
    result.firstSolidPrim = 0u;
    result.mirrorBounces = 0u;
    result.flags = 0u;

    const bool jitter = (gIntegrator.flags & LC_INTEGRATOR_FLAG_JITTER) != 0u;
    const float2 pixelJitter = jitter ? Rand2(key, LC_DIM_CAMERA) : float2(0.5, 0.5);
    float3 origin = gFrame.cameraPosition;
    float3 direction = CameraRayDirection(pixel, pixelJitter);
    float tMin = gFrame.rayTMin;

    float3 throughput = 1.0;
    float3 radiance = 0.0;
    bool previousDelta = false;   // The previous vertex could not run next-event estimation.
    float previousPdfBsdf = 0.0;  // Solid-angle pdf of the direction that produced this hit.
    const uint strategy = gIntegrator.strategy;
    const uint maxHits = gIntegrator.maxHits;
    uint lastMirrorId = LC_MISS_ID;
    bool solidFound = false;

    for (uint hitIndex = 0u; hitIndex < maxHits; ++hitIndex) {
        const ClosestHit hit = TraceClosest(gScene, origin, direction, tMin, gFrame.rayTMax);
        if (!hit.hit) {
            break;  // Black environment.
        }
        const Surface s = ResolveSurface(hit, direction);
        const uint bounceDim = LC_DIM_BOUNCE_BASE + hitIndex * LC_DIM_PER_BOUNCE;

        // Emission found by the camera ray, a delta bounce, or a BSDF-sampled continuation.
        if (IsActiveEmitter(s.material)) {
            const float3 le = EmittedRadiance(s.material, s.geometricNormal, -direction);
            float weight = 0.0;
            if (hitIndex == 0u || previousDelta) {
                weight = 1.0;  // No competing strategy could have produced this contribution.
            } else if (strategy == LC_STRATEGY_BSDF) {
                weight = 1.0;
            } else if (strategy == LC_STRATEGY_MIS && s.emitterIndex != LC_NO_EMITTER) {
                const float3 toLight = s.position - origin;
                const float dist2 = dot(toLight, toLight);
                const float cosLight = dot(s.geometricNormal, -direction);
                const float pdfLight = cosLight > 0.0 ? EmitterPdfArea(s.emitterIndex) * dist2 / cosLight : 0.0;
                weight = PowerHeuristic(previousPdfBsdf, pdfLight);
            }
            radiance += throughput * le * weight;
        }

        if (!IsDeltaMaterial(s.material) && !solidFound) {
            solidFound = true;
            result.firstSolidId = s.stableId;
            result.firstSolidPrim = s.primitiveIndex;
            result.mirrorBounces = hitIndex;
            const float facingCosine = dot(s.geometricNormal, direction);
            if (hit.frontFace) result.flags |= LC_HIT_FLAG_FRONT_FACE;
            if (facingCosine < 0.0) result.flags |= LC_HIT_FLAG_GEOMETRIC_FACING;
            if (abs(facingCosine) < LC_GRAZING_COSINE) result.flags |= LC_HIT_FLAG_GRAZING;
        }

        const bool lastVertex = hitIndex + 1u >= maxHits;

        if (IsDeltaMaterial(s.material)) {
            lastMirrorId = s.stableId;
            if (lastVertex) {
                break;
            }
            direction = reflect(direction, s.normal);
            origin = OffsetRay(s.position, s.normal);
            tMin = 0.0;
            throughput *= s.material.reflectance;
            previousDelta = true;
            previousPdfBsdf = 0.0;
            continue;
        }

        // Diffuse (or emitter surface as a reflector): next-event estimation.
        const float3 brdf = DiffuseBrdf(s.material);
        if (strategy != LC_STRATEGY_BSDF && gIntegrator.emitterCount > 0u) {
            const LightSample ls = SampleEmitters(Rand(key, bounceDim + LC_DIM_EMITTER_SELECT),
                                                  Rand(key, bounceDim + LC_DIM_EMITTER_TRIANGLE),
                                                  Rand2(key, bounceDim + LC_DIM_EMITTER_POINT));
            if (ls.valid) {
                const float3 toLight = ls.position - s.position;
                const float dist2 = dot(toLight, toLight);
                const float dist = sqrt(dist2);
                const float3 wi = toLight / dist;
                const float cosSurface = dot(s.normal, wi);
                const float cosLight = dot(ls.normal, -wi);  // One-sided emission.
                if (cosSurface > 0.0 && cosLight > 0.0 && dist2 > 0.0) {
                    const float pdfLight = ls.pdfArea * dist2 / cosLight;  // Solid-angle measure.
                    if (pdfLight <= 0.0) {
                        InterlockedAdd(gStats[LC_STATS_ZERO_PDF], 1u);
                    } else if (Unoccluded(s.position, s.normal, ls.position, ls.normal)) {
                        float weight = 1.0;
                        if (strategy == LC_STRATEGY_MIS && !lastVertex) {
                            const float pdfBsdf = cosSurface / LC_PI;
                            weight = PowerHeuristic(pdfLight, pdfBsdf);
                        }
                        radiance += throughput * brdf * ls.radiance * cosSurface / pdfLight * weight;
                    }
                }
            }
        }

        if (lastVertex) {
            break;
        }

        // Continue with a cosine-weighted BSDF sample: throughput *= brdf * cos / pdf = reflectance.
        float pdfBsdf = 0.0;
        const float3 next = CosineSampleHemisphere(Rand2(key, bounceDim + LC_DIM_BSDF), s.normal, pdfBsdf);
        if (pdfBsdf <= 0.0) {
            InterlockedAdd(gStats[LC_STATS_ZERO_PDF], 1u);
            break;
        }
        throughput *= brdf * max(dot(s.normal, next), 0.0) / pdfBsdf;
        if (all(throughput <= 0.0)) {
            break;
        }
        direction = next;
        origin = OffsetRay(s.position, s.normal);
        tMin = 0.0;
        previousDelta = false;
        previousPdfBsdf = pdfBsdf;
    }

    if (!solidFound) {
        result.firstSolidId = lastMirrorId;  // Path left the scene, possibly through a mirror.
    }
    result.radiance = radiance;
    return result;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.renderSize.x || dtid.y >= gFrame.renderSize.y) {
        return;
    }
    const uint2 pixel = dtid.xy;
    const SampleKey key = MakeSampleKey(pixel, gFrame.sampleIndex, gFrame.seed);
    const PathResult path = TracePath(pixel, key);

    float3 sample = path.radiance;
    if (any(isnan(sample))) {
        InterlockedAdd(gStats[LC_STATS_NAN], 1u);
        sample = 0.0;
    }
    if (any(isinf(sample))) {
        InterlockedAdd(gStats[LC_STATS_INF], 1u);
        sample = 0.0;
    }
    if (any(sample < 0.0)) {
        InterlockedAdd(gStats[LC_STATS_NEGATIVE], 1u);
        sample = max(sample, 0.0);
    }

    const bool reset = (gIntegrator.flags & LC_INTEGRATOR_FLAG_RESET) != 0u;
    float4 sum = reset ? float4(0.0, 0.0, 0.0, 0.0) : gAccum[pixel];
    float4 sumSq = reset ? float4(0.0, 0.0, 0.0, 0.0) : gAccumSq[pixel];
    sum.xyz += sample;
    sumSq.xyz += sample * sample;
    const float count = float(gFrame.sampleIndex + 1u);
    sum.w = count;
    sumSq.w = count;
    gAccum[pixel] = sum;
    gAccumSq[pixel] = sumSq;

    const float3 mean = sum.xyz / count;
    gLinear[pixel] = float4(mean, count);
    gDisplay[pixel] = float4(LinearToSrgb(saturate(mean * gIntegrator.exposure)), 1.0);
    gHitInfo[pixel] = uint4(path.firstSolidId, path.firstSolidPrim, path.flags, path.mirrorBounces);
}
