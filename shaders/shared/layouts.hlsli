// Shared GPU data contracts. The C++ mirror is src/render/gpu_layouts.h. Both sides are checked:
// static_asserts on the CPU and the layout_probe shader on the GPU (test T02).
#ifndef LC_LAYOUTS_HLSLI
#define LC_LAYOUTS_HLSLI

// Matrices are stored row-major in memory: element (row, col) lives at [row * 4 + col], which is
// exactly the C++ float m[4][4] layout. The math uses column vectors:
//     world = mul(objectToWorld, float4(local, 1.0))
#pragma pack_matrix(row_major)

static const uint LC_VIEW_NORMALS = 0;
static const uint LC_VIEW_INSTANCE_IDS = 1;
static const uint LC_VIEW_DEPTH = 2;
static const uint LC_VIEW_BARYCENTRICS = 3;
static const uint LC_VIEW_FRONT_FACE = 4;
static const uint LC_VIEW_PRIMITIVE_IDS = 5;

// Hit-info texel x component when the camera ray leaves the scene.
static const uint LC_MISS_ID = 0xFFFFFFFFu;

// Hit-info texel z component bits.
static const uint LC_HIT_FLAG_FRONT_FACE = 1u;        // RayQuery reported a front-facing triangle.
static const uint LC_HIT_FLAG_GEOMETRIC_FACING = 2u;  // Geometric normal faces the ray origin.
static const uint LC_HIT_FLAG_GRAZING = 4u;           // |dot(normal, direction)| below LC_GRAZING_COSINE: facing is ill-defined.
static const float LC_GRAZING_COSINE = 1e-3f;

// 256 bytes. Bound as a root constant buffer (b0).
struct FrameConstants {
    float4x4 viewToWorld;   // Camera-local -> world. Camera looks along local -Z, +X right, +Y up.
    float4x4 worldToView;   // Inverse of viewToWorld.
    float4x4 viewToClip;    // Right-handed perspective, D3D clip depth in [0, 1] (near -> 0).
    float3 cameraPosition;  // World-space camera position in metres.
    float tanHalfFovY;      // tan(vertical field of view / 2).
    uint2 renderSize;       // Internal trace resolution in pixels.
    float2 invRenderSize;   // 1 / renderSize.
    uint frameIndex;        // Rendered frames since start (display counter).
    uint sampleIndex;       // Accumulated path samples for the current image (reset on history invalidation).
    uint viewMode;          // LC_VIEW_*.
    uint instanceCount;     // Number of InstanceRecords.
    float rayTMin;          // Camera ray start distance in metres.
    float rayTMax;          // Camera ray end distance in metres.
    float aspectRatio;      // renderSize.x / renderSize.y.
    uint seed;              // Deterministic sampling seed.
};

// 112 bytes per instance. StructuredBuffer (t1), indexed by TLAS instance index.
struct InstanceRecord {
    float4 objectToWorldRow[3];      // Rows 0..2 of the 4x4 object-to-world matrix (translation in .w).
    float4 prevObjectToWorldRow[3];  // Same for the previously rendered image.
    uint meshIndex;                  // Index into MeshRecords.
    uint materialIndex;              // Index into the material table (M2).
    uint stableId;                   // Scene instance id; equals the TLAS InstanceID.
    uint transformRevision;          // Incremented on every transform change.
};

// 16 bytes per mesh. StructuredBuffer (t2).
struct MeshRecord {
    uint firstVertex;  // Offset into the global position buffer, in float3 elements.
    uint firstIndex;   // Offset into the global index buffer, in uint elements.
    uint vertexCount;
    uint indexCount;
};

#endif  // LC_LAYOUTS_HLSLI
