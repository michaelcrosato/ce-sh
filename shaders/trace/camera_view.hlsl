// M1 camera-ray pass: one hardware ray per pixel through the scene TLAS, writing diagnostic views.
// This is a DIAGNOSTIC pass. Its colours are not lighting and never reach the production image.
#include "shared/layouts.hlsli"
#include "shared/math.hlsli"
#include "trace/ray_interface.hlsli"

ConstantBuffer<FrameConstants> gFrame : register(b0);
RaytracingAccelerationStructure gScene : register(t0);
StructuredBuffer<InstanceRecord> gInstances : register(t1);
StructuredBuffer<MeshRecord> gMeshes : register(t2);
StructuredBuffer<float3> gPositions : register(t3);
StructuredBuffer<uint> gIndices : register(t4);
RWTexture2D<float4> gDisplay : register(u0);  // R8G8B8A8_UNORM, copied to the swap chain.
RWTexture2D<float4> gLinear : register(u1);   // R32G32B32A32_FLOAT, rgb = view colour, a = hit distance.
RWTexture2D<uint4> gHitInfo : register(u2);   // x = stable id or LC_MISS_ID, y = primitive, z = flags, w = instance index.

// Pinhole camera. Pixel (0,0) is the top-left; +Y in NDC points up, matching the world +Y-up frame.
float3 CameraRayDirection(uint2 pixel) {
    const float2 uv = (float2(pixel) + 0.5) * gFrame.invRenderSize;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float3 dirView = float3(ndc.x * gFrame.tanHalfFovY * gFrame.aspectRatio,
                                  ndc.y * gFrame.tanHalfFovY,
                                  -1.0);
    return normalize(mul((float3x3)gFrame.viewToWorld, dirView));
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.renderSize.x || dtid.y >= gFrame.renderSize.y) {
        return;
    }
    const uint2 pixel = dtid.xy;
    const float3 origin = gFrame.cameraPosition;
    const float3 dir = CameraRayDirection(pixel);

    const ClosestHit hit = TraceClosest(gScene, origin, dir, gFrame.rayTMin, gFrame.rayTMax);

    float3 color = 0.0;
    uint4 info = uint4(LC_MISS_ID, 0u, 0u, 0u);
    float distance = 0.0;

    if (hit.hit) {
        const InstanceRecord inst = gInstances[hit.instanceIndex];
        const MeshRecord mesh = gMeshes[inst.meshIndex];
        const uint base = mesh.firstIndex + hit.primitiveIndex * 3u;
        const uint i0 = gIndices[base + 0u];
        const uint i1 = gIndices[base + 1u];
        const uint i2 = gIndices[base + 2u];
        const float3 p0 = TransformPoint(inst.objectToWorldRow, gPositions[mesh.firstVertex + i0]);
        const float3 p1 = TransformPoint(inst.objectToWorldRow, gPositions[mesh.firstVertex + i1]);
        const float3 p2 = TransformPoint(inst.objectToWorldRow, gPositions[mesh.firstVertex + i2]);

        // Geometric normal from the world-space winding (counter-clockwise = outward).
        const float3 n = normalize(cross(p1 - p0, p2 - p0));
        const bool geometricFacing = dot(n, dir) < 0.0;

        uint flags = 0u;
        if (hit.frontFace) flags |= LC_HIT_FLAG_FRONT_FACE;
        if (geometricFacing) flags |= LC_HIT_FLAG_GEOMETRIC_FACING;
        info = uint4(inst.stableId, hit.primitiveIndex, flags, hit.instanceIndex);
        distance = hit.t;

        switch (gFrame.viewMode) {
            case LC_VIEW_INSTANCE_IDS:
                color = HashColor(inst.stableId);
                break;
            case LC_VIEW_DEPTH:
                color = saturate(hit.t / 20.0).xxx;  // 0 m = black, 20 m = white.
                break;
            case LC_VIEW_BARYCENTRICS:
                color = float3(1.0 - hit.barycentrics.x - hit.barycentrics.y, hit.barycentrics.x, hit.barycentrics.y);
                break;
            case LC_VIEW_FRONT_FACE:
                // Green: front face and normal faces the ray. Red: back face and normal faces away.
                // Magenta: the RayQuery facing flag and the geometric normal disagree (winding bug).
                if (hit.frontFace == geometricFacing) {
                    color = hit.frontFace ? float3(0.1, 0.9, 0.1) : float3(0.9, 0.1, 0.1);
                } else {
                    color = float3(1.0, 0.0, 1.0);
                }
                break;
            case LC_VIEW_PRIMITIVE_IDS:
                color = HashColor(hit.primitiveIndex * 7919u + inst.stableId);
                break;
            case LC_VIEW_NORMALS:
            default:
                color = n * 0.5 + 0.5;
                break;
        }
    }

    gDisplay[pixel] = float4(color, 1.0);
    gLinear[pixel] = float4(color, distance);
    gHitInfo[pixel] = info;
}
