// T02 layout probe: copies selected fields of the shared GPU records into a buffer so the CPU can
// confirm that C++ and HLSL agree on offsets, matrix element order, and array packing.
// The CPU side (Renderer::RunLayoutProbe in src/render/renderer.cpp) fills every record with known values first.
#include "shared/layouts.hlsli"

ConstantBuffer<FrameConstants> gFrame : register(b0);
StructuredBuffer<InstanceRecord> gInstances : register(t1);
StructuredBuffer<MeshRecord> gMeshes : register(t2);
StructuredBuffer<float3> gPositions : register(t3);
StructuredBuffer<uint> gIndices : register(t4);
RWStructuredBuffer<uint> gProbe : register(u3);

static const uint LC_PROBE_SENTINEL = 0xC0FFEEu;

[numthreads(1, 1, 1)]
void main() {
    uint i = 0;
    // Matrix element (row, col) indexing: [row][col]. Row 0, column 3 is the x translation.
    gProbe[i++] = asuint(gFrame.viewToWorld[0][3]);
    gProbe[i++] = asuint(gFrame.viewToWorld[1][3]);
    gProbe[i++] = asuint(gFrame.viewToWorld[2][3]);
    gProbe[i++] = asuint(gFrame.viewToWorld[3][3]);
    gProbe[i++] = asuint(gFrame.worldToView[1][2]);
    gProbe[i++] = asuint(gFrame.viewToClip[2][3]);
    gProbe[i++] = asuint(gFrame.cameraPosition.z);
    gProbe[i++] = asuint(gFrame.tanHalfFovY);
    gProbe[i++] = gFrame.renderSize.x;
    gProbe[i++] = gFrame.renderSize.y;
    gProbe[i++] = gFrame.frameIndex;
    gProbe[i++] = gFrame.viewMode;
    gProbe[i++] = gFrame.instanceCount;
    gProbe[i++] = asuint(gFrame.rayTMax);
    gProbe[i++] = asuint(gFrame.aspectRatio);
    gProbe[i++] = gFrame.seed;
    gProbe[i++] = asuint(gInstances[1].objectToWorldRow[0].w);
    gProbe[i++] = asuint(gInstances[1].objectToWorldRow[2].w);
    gProbe[i++] = asuint(gInstances[1].prevObjectToWorldRow[1].y);
    gProbe[i++] = gInstances[1].meshIndex;
    gProbe[i++] = gInstances[1].materialIndex;
    gProbe[i++] = gInstances[1].stableId;
    gProbe[i++] = gInstances[1].emitterIndex;
    gProbe[i++] = gMeshes[1].firstVertex;
    gProbe[i++] = gMeshes[1].firstIndex;
    gProbe[i++] = gMeshes[1].vertexCount;
    gProbe[i++] = gMeshes[1].indexCount;
    gProbe[i++] = asuint(gPositions[2].y);
    gProbe[i++] = gIndices[4];
    gProbe[i++] = LC_PROBE_SENTINEL;
}
