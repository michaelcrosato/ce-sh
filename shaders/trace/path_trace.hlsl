// RGB path tracer (spec §12): one camera path per pixel per dispatch, at most maxHits surface hits
// including the camera hit and mirror hits, next-event estimation at every non-delta surface,
// BSDF sampling for the continuation, multiple importance sampling (power heuristic, solid-angle
// measure) between the two, emission counted once, delta reflection handled separately, black
// environment, no Russian roulette. Every ray type traverses the same TLAS (spec H04).
//
// Outputs: accumulation sums (u4, u5) for progressive reference and raw modes, the running mean as
// linear radiance (u1), the display image after fixed exposure and sRGB encoding (u0), and the
// identity of the first non-mirror surface (u2) for the mirror tests.
//
// Compiled twice. With LC_GUIDES=1 (path_trace_guided.cso, denoised mode) the same integrator
// additionally follows the delta mirror chain to the first non-mirror surface (NRD's Primary
// Surface Replacement), writes that virtual surface's guides (normal + roughness, view Z, world
// motion), splits the sampled radiance into a demodulated diffuse or specular signal with the
// continuation hit distance, and records the noise-free emission separately; the display and
// accumulation outputs are then produced by post/compose.hlsl. The estimator is identical in both
// variants: the guided sections only observe the path.
#include "shared/layouts.hlsli"
#include "shared/math.hlsli"
#include "shared/sampling.hlsli"
#include "shared/materials.hlsli"
#include "trace/ray_interface.hlsli"
#if LC_GUIDES
#include "NRD.hlsli"
#endif

ConstantBuffer<FrameConstants> gFrame : register(b0);
ConstantBuffer<IntegratorConstants> gIntegrator : register(b1);
#if LC_GUIDES
ConstantBuffer<GuideConstants> gGuide : register(b2);
#endif
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
#if LC_GUIDES
RWTexture2D<float4> gMotion : register(u7);           // IN_MV: world-space motion of the virtual surface, metres.
RWTexture2D<float4> gNormalRoughness : register(u8);  // IN_NORMAL_ROUGHNESS: R10G10B10A2 via NRD_FrontEnd_PackNormalAndRoughness.
RWTexture2D<float> gViewZ : register(u9);             // IN_VIEWZ: view-space z of the virtual surface (negative in front).
RWTexture2D<float4> gDiffIn : register(u10);          // IN_DIFF_RADIANCE_HITDIST.
RWTexture2D<float4> gSpecIn : register(u11);          // IN_SPEC_RADIANCE_HITDIST.
RWTexture2D<float4> gDiffFactor : register(u12);      // Demodulation factor (rgb), w = material id.
RWTexture2D<float4> gSpecFactor : register(u13);      // Demodulation factor (rgb), w = roughness.
RWTexture2D<float4> gEmission : register(u14);        // Radiance seen at or before the PSR vertex (noise-free), w = 1 when a surface was hit.
RWTexture2D<float4> gDirect : register(u15);          // Direct light at the PSR vertex (this sample), w = unfolded distance.
#endif

// Mirrors before the PSR vertex are bounded by maxHits - 1 (the last hit must be the PSR).
static const uint LC_MAX_MIRROR_CHAIN = 3;

// ---------------------------------------------------------------------------------------------
// Geometry helpers

struct TriangleWorld {
    float3 p0;
    float3 p1;
    float3 p2;
    float3 geometricNormal;  // Unit, from the winding (unflipped).
    float scale;             // Largest absolute vertex coordinate: error bound for offsets.
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
    t.scale = TriangleScale(t.p0, t.p1, t.p2);
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
    float scale;             // Triangle coordinate magnitude for ray offsets.
#if LC_GUIDES
    float3 objectPoint;      // Hit point in object space, for the motion of rigid instances.
#endif
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
    s.scale = tri.scale;
#if LC_GUIDES
    {
        const MeshRecord mesh = gMeshes[inst.meshIndex];
        const uint base = mesh.firstIndex + hit.primitiveIndex * 3u;
        const float3 o0 = gPositions[mesh.firstVertex + gIndices[base + 0u]];
        const float3 o1 = gPositions[mesh.firstVertex + gIndices[base + 1u]];
        const float3 o2 = gPositions[mesh.firstVertex + gIndices[base + 2u]];
        s.objectPoint = o0 * (1.0 - b1 - b2) + o1 * b1 + o2 * b2;
    }
#endif
    return s;
}

// ---------------------------------------------------------------------------------------------
// Emitter sampling

struct LightSample {
    float3 position;
    float3 normal;    // Emitting side.
    float3 radiance;
    float pdfArea;    // Probability density per unit area over all emitters: selectionPdf / area.
    float scale;      // Emitting triangle coordinate magnitude for the shadow-ray endpoint offset.
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
    ls.scale = tri.scale;
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
bool Unoccluded(float3 from, float3 fromNormal, float fromScale, float3 to, float3 toNormal, float toScale) {
    const float3 origin = OffsetRayTri(from, fromNormal, fromScale);
    const float3 target = OffsetRayTri(to, toNormal, toScale);
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
#if LC_GUIDES
    float3 primaryDirection;
    bool psrFound;                      // A non-mirror surface was reached (directly or through mirrors).
    float3 psrNormal;                   // World-space shading normal at that vertex, facing the incoming ray.
    float3 psrObjectPoint;              // Object-space hit point (rigid motion from the instance transforms).
    uint psrInstance;
    MaterialRecord psrMaterial;
    float unfolded;                     // Camera -> PSR distance along the unfolded path (sum of segment lengths).
    float3 chainThroughput;             // Product of the mirror reflectances before the PSR vertex.
    float3 mirrorNormals[LC_MAX_MIRROR_CHAIN];
    uint mirrorCount;
    float3 emission;                    // Radiance found at or before the PSR vertex: deterministic, not denoised.
    float3 signal;                      // Radiance gathered at and after the PSR vertex: the lobe signal before demodulation.
    float3 direct;                      // The next-event term at the PSR vertex only (diagnostic and hit-distance mixing).
    float continuationHitDist;          // Distance from the PSR vertex to the BSDF-sampled continuation's hit.
    float directHitDist;                // Distance from the PSR vertex to its unoccluded light sample (0 = none).
#endif
};

PathResult TracePath(uint2 pixel, SampleKey key) {
    PathResult result = (PathResult)0;
    result.firstSolidId = LC_MISS_ID;

    const bool jitter = (gIntegrator.flags & LC_INTEGRATOR_FLAG_JITTER) != 0u;
    float2 pixelJitter = jitter ? Rand2(key, LC_DIM_CAMERA) : float2(0.5, 0.5);
#if LC_GUIDES
    if ((gGuide.flags & LC_GUIDE_FLAG_GLOBAL_JITTER) != 0u) {
        pixelJitter = 0.5 + gGuide.jitterPixels;  // The offset NRD is told about (CommonSettings::cameraJitter).
    }
#endif
    float3 origin = gFrame.cameraPosition;
    float3 direction = CameraRayDirection(pixel, pixelJitter);
    float tMin = gFrame.rayTMin;
#if LC_GUIDES
    result.primaryDirection = direction;
    result.chainThroughput = 1.0;
#endif

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
#if LC_GUIDES
        if (result.psrFound && hitIndex == result.mirrorBounces + 1u) {
            result.continuationHitDist = hit.hit ? hit.t : LC_MISS_HIT_DISTANCE;  // "As is" for the first bounce after the PSR.
        }
#endif
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
            const float3 contribution = throughput * le * weight;
            radiance += contribution;
#if LC_GUIDES
            if (!solidFound) {
                result.emission += contribution;  // Seen directly or through mirrors: deterministic.
            } else {
                result.signal += contribution;
            }
#endif
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
#if LC_GUIDES
            result.psrFound = true;
            result.psrNormal = s.normal;
            result.psrObjectPoint = s.objectPoint;
            result.psrInstance = s.instanceIndex;
            result.psrMaterial = s.material;
            result.unfolded += hit.t;
            result.chainThroughput = throughput;  // Mirror reflectances so far (1 when seen directly).
#endif
        }

        const bool lastVertex = hitIndex + 1u >= maxHits;

        if (IsDeltaMaterial(s.material)) {
            lastMirrorId = s.stableId;
#if LC_GUIDES
            if (!solidFound) {
                result.unfolded += hit.t;
                if (result.mirrorCount < LC_MAX_MIRROR_CHAIN) {
                    result.mirrorNormals[result.mirrorCount] = s.normal;
                }
                result.mirrorCount += 1u;
            }
#endif
            if (lastVertex) {
                break;
            }
            direction = reflect(direction, s.normal);
            origin = OffsetRayTri(s.position, s.normal, s.scale);
            tMin = 0.0;
            throughput *= s.material.reflectance;
            previousDelta = true;
            previousPdfBsdf = 0.0;
            continue;
        }

        // Non-delta surface (diffuse, emitter surface, rough conductor): next-event estimation.
        const float3 wo = -direction;
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
                    } else if (Unoccluded(s.position, s.normal, s.scale, ls.position, ls.normal, ls.scale)) {
                        const float3 f = EvalBsdf(s.material, s.normal, wo, wi);
                        float weight = 1.0;
                        if (strategy == LC_STRATEGY_MIS && !lastVertex) {
                            weight = PowerHeuristic(pdfLight, PdfBsdf(s.material, s.normal, wo, wi));
                        }
                        const float3 contribution = throughput * f * ls.radiance * cosSurface / pdfLight * weight;
                        radiance += contribution;
#if LC_GUIDES
                        result.signal += contribution;
                        if (hitIndex == result.mirrorBounces) {
                            result.direct += contribution;
                            result.directHitDist = dist;
                        }
#endif
                    }
                }
            }
        }

        if (lastVertex) {
            break;
        }

        // Continue with a BSDF sample: throughput *= f * cos / pdf.
        const BsdfSample bs = SampleBsdf(s.material, s.normal, wo, Rand2(key, bounceDim + LC_DIM_BSDF));
        if (!bs.valid) {
            if (!IsRoughConductor(s.material)) {
                InterlockedAdd(gStats[LC_STATS_ZERO_PDF], 1u);  // A diffuse sample can only fail through a zero pdf.
            }
            break;  // Conductor samples below the surface are energy loss by design (documented).
        }
        throughput *= bs.weight;
        if (all(throughput <= 0.0)) {
            break;
        }
        direction = bs.direction;
        origin = OffsetRayTri(s.position, s.normal, s.scale);
        tMin = 0.0;
        previousDelta = false;
        previousPdfBsdf = bs.pdf;
    }

    if (!solidFound) {
        result.firstSolidId = lastMirrorId;  // Path left the scene, possibly through a mirror.
    }
    result.radiance = radiance;
    return result;
}

#if LC_GUIDES
// Guides for the first non-mirror surface as if the mirrors did not exist (NRD README,
// "Interaction with Primary Surface Replacements"): the virtual position lies on the primary ray
// at the unfolded distance, the virtual normal and motion are reflected through every mirror plane
// in reverse order. Mirror planes are taken as static between frames (docs/RENDERING.md).
void WriteGuides(uint2 pixel, PathResult path) {
    if (!path.psrFound) {
        gMotion[pixel] = 0.0;
        gNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0, 0.0, 1.0), 1.0, 0.0);
        gViewZ[pixel] = -2.0 * gGuide.denoisingRange;  // Beyond the denoising range: NRD skips the pixel.
        gDiffIn[pixel] = 0.0;
        gSpecIn[pixel] = 0.0;
        gDiffFactor[pixel] = float4(1.0, 1.0, 1.0, 0.0);
        gSpecFactor[pixel] = float4(1.0, 1.0, 1.0, 1.0);
        gEmission[pixel] = float4(path.emission, 0.0);
        gDirect[pixel] = 0.0;
        return;
    }

    const float3 virtualPosition = gFrame.cameraPosition + path.primaryDirection * path.unfolded;
    const float viewZ = mul(gFrame.worldToView, float4(virtualPosition, 1.0)).z;

    const InstanceRecord inst = gInstances[path.psrInstance];
    float3 motion = TransformPoint(inst.prevObjectToWorldRow, path.psrObjectPoint) - TransformPoint(inst.objectToWorldRow, path.psrObjectPoint);
    float3 n = path.psrNormal;
    const uint chain = min(path.mirrorCount, LC_MAX_MIRROR_CHAIN);
    for (int i = int(chain) - 1; i >= 0; --i) {  // Last mirror first.
        n = reflect(n, path.mirrorNormals[i]);
        motion = reflect(motion, path.mirrorNormals[i]);
    }

    const bool conductor = IsRoughConductor(path.psrMaterial);
    const float roughness = conductor ? clamp(path.psrMaterial.roughness, 0.02, 1.0) : 1.0;  // NRD: "isDiffuse ? 1.0 : roughness".
    const float materialId = conductor ? float(LC_MATERIAL_ID_CONDUCTOR) : float(LC_MATERIAL_ID_DIFFUSE);
    const float3 albedo = conductor ? 0.0 : path.chainThroughput * path.psrMaterial.reflectance;
    const float3 f0 = conductor ? path.chainThroughput * path.psrMaterial.reflectance : 0.0;
    float3 diffFactor;
    float3 specFactor;
    NRD_MaterialFactors(n, -path.primaryDirection, albedo, f0, roughness, diffFactor, specFactor);

    // Diffuse hit distance: the continuation ray's distance, pulled toward the distance of the light
    // sample where direct light dominates (NRD README, "Combined denoising of direct and indirect
    // lighting"): the denoiser then blurs less across sharp direct-light gradients and shadows. The
    // specular lobe keeps the in-lobe continuation distance only.
    float hitDist = path.continuationHitDist;
    if (!conductor && path.directHitDist > 0.0) {
        if (hitDist >= LC_MISS_HIT_DISTANCE) {
            hitDist = path.directHitDist;  // The continuation left the scene (black environment): it carries no radiance.
        } else {
            const float directLum = Luminance709(path.direct);
            const float indirectLum = max(Luminance709(path.signal) - directLum, 0.0);
            const float directWeight = min(directLum / (directLum + indirectLum + 1e-6), 0.5);
            hitDist = lerp(hitDist, path.directHitDist, directWeight);
        }
    }
    const float normHitDist = REBLUR_FrontEnd_GetNormHitDist(hitDist, viewZ, gGuide.hitDistParams, roughness);
    float4 diff = 0.0;
    float4 spec = 0.0;
    if (conductor) {
        spec = REBLUR_FrontEnd_PackRadianceAndNormHitDist(path.signal / specFactor, normHitDist, true);
    } else {
        diff = REBLUR_FrontEnd_PackRadianceAndNormHitDist(path.signal / diffFactor, normHitDist, true);
    }

    gMotion[pixel] = float4(motion, 1.0);
    gNormalRoughness[pixel] = NRD_FrontEnd_PackNormalAndRoughness(n, roughness, materialId);
    gViewZ[pixel] = viewZ;
    gDiffIn[pixel] = diff;
    gSpecIn[pixel] = spec;
    gDiffFactor[pixel] = float4(diffFactor, materialId);
    gSpecFactor[pixel] = float4(specFactor, roughness);
    gEmission[pixel] = float4(path.emission, 1.0);
    gDirect[pixel] = float4(path.direct, path.unfolded);
}
#endif

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

#if LC_GUIDES
    WriteGuides(pixel, path);
#else
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
#endif
    gHitInfo[pixel] = uint4(path.firstSolidId, path.firstSolidPrim, path.flags, path.mirrorBounces);
}
