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
static const uint LC_NO_EMITTER = 0xFFFFFFFFu;

// Hit-info texel z component bits.
static const uint LC_HIT_FLAG_FRONT_FACE = 1u;        // RayQuery reported a front-facing triangle.
static const uint LC_HIT_FLAG_GEOMETRIC_FACING = 2u;  // Geometric normal faces the ray origin.
static const uint LC_HIT_FLAG_GRAZING = 4u;           // |dot(normal, direction)| below LC_GRAZING_COSINE: facing is ill-defined.
static const float LC_GRAZING_COSINE = 1e-3f;

// Integrator strategies (IntegratorConstants.strategy).
static const uint LC_STRATEGY_MIS = 0;    // Next-event estimation combined with BSDF sampling (production).
static const uint LC_STRATEGY_LIGHT = 1;  // Next-event estimation only.
static const uint LC_STRATEGY_BSDF = 2;   // BSDF sampling only.

// IntegratorConstants.flags bits.
static const uint LC_INTEGRATOR_FLAG_RESET = 1u;   // This dispatch overwrites the accumulation buffers.
static const uint LC_INTEGRATOR_FLAG_JITTER = 2u;  // Jitter the camera ray inside the pixel.

// Material types (MaterialRecord.type) and flags.
static const uint LC_MATERIAL_DIFFUSE = 0;
static const uint LC_MATERIAL_MIRROR = 1;
static const uint LC_MATERIAL_EMITTER = 2;
static const uint LC_MATERIAL_ROUGH_CONDUCTOR = 3;
static const uint LC_MATERIAL_FLAG_EMITTER_ON = 1u;

// Stats buffer slots (RWStructuredBuffer<uint>, LC_STATS_COUNT entries).
static const uint LC_STATS_NAN = 0;
static const uint LC_STATS_INF = 1;
static const uint LC_STATS_NEGATIVE = 2;
static const uint LC_STATS_ZERO_PDF = 3;
static const uint LC_STATS_COUNT = 8;

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
    uint sampleIndex;       // Samples already accumulated before this dispatch (0 = first sample).
    uint viewMode;          // LC_VIEW_*.
    uint instanceCount;     // Number of InstanceRecords.
    float rayTMin;          // Camera ray start distance in metres.
    float rayTMax;          // Camera ray end distance in metres.
    float aspectRatio;      // renderSize.x / renderSize.y.
    uint seed;              // Deterministic sampling seed.
};

// 32 bytes. Root constant buffer b1 for the path tracer.
struct IntegratorConstants {
    uint maxHits;       // Maximum surface hits per path, including the camera hit and mirror hits.
    uint strategy;      // LC_STRATEGY_*.
    uint emitterCount;  // Active emitters in gEmitters.
    uint flags;         // LC_INTEGRATOR_FLAG_*.
    float exposure;     // Display scale applied before sRGB encoding (does not touch radiance).
    float pad0;
    float pad1;
    float pad2;
};

// 112 bytes per instance. StructuredBuffer (t1), indexed by TLAS instance index.
struct InstanceRecord {
    float4 objectToWorldRow[3];      // Rows 0..2 of the 4x4 object-to-world matrix (translation in .w).
    float4 prevObjectToWorldRow[3];  // Same for the previously rendered image.
    uint meshIndex;                  // Index into MeshRecords.
    uint materialIndex;              // Index into MaterialRecords.
    uint stableId;                   // Scene instance id; equals the TLAS InstanceID.
    uint emitterIndex;               // Index into EmitterRecords, or LC_NO_EMITTER.
};

// 16 bytes per mesh. StructuredBuffer (t2).
struct MeshRecord {
    uint firstVertex;  // Offset into the global position buffer, in float3 elements.
    uint firstIndex;   // Offset into the global index buffer, in uint elements.
    uint vertexCount;
    uint indexCount;
};

// 48 bytes per material. StructuredBuffer (t5).
struct MaterialRecord {
    uint type;          // LC_MATERIAL_*.
    uint flags;         // LC_MATERIAL_FLAG_*.
    float roughness;    // RoughConductor: perceptual roughness in [0.02, 1]; GGX alpha = roughness^2.
    float pad1;
    float3 reflectance; // Diffuse albedo, mirror reflectance, emitter surface albedo, or conductor F0. [0, 1].
    float pad2;
    float3 radiance;    // Emitter front-side radiance (scene-linear, unitless radiance scale).
    float pad3;
};

// 16 bytes per emitting triangle. StructuredBuffer (t7).
struct EmitterTriangle {
    uint instanceIndex;
    uint primitiveIndex;
    float area;  // World-space area.
    float cdf;   // Cumulative area fraction within the emitter (last triangle = 1).
};

// 48 bytes per active emitter. StructuredBuffer (t6).
struct EmitterRecord {
    uint instanceIndex;
    uint firstTriangle;
    uint triangleCount;
    uint materialIndex;
    float area;          // Total world-space area.
    float selectionPdf;  // Exact probability of selecting this emitter.
    float selectionCdf;  // Cumulative selection probability (last emitter = 1).
    float pad;
    float3 radiance;
    float pad2;
};

#endif  // LC_LAYOUTS_HLSLI
