// CPU mirrors of the GPU records in shaders/shared/layouts.hlsli. Byte layouts are pinned by the
// static_asserts below and confirmed on the GPU by the layout probe (test T02).
#pragma once

#include <cstddef>
#include <cstdint>

namespace lc::gpu {

struct alignas(16) Float4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

// Row-major storage: m[row][col]. Column-vector math on both sides.
struct alignas(16) Float4x4 {
    float m[4][4] = {};
};

// Bound as root constant buffer b0. 256 bytes; the upload arena aligns it to 256 bytes.
struct alignas(16) FrameConstants {
    Float4x4 viewToWorld;
    Float4x4 worldToView;
    Float4x4 viewToClip;
    float cameraPosition[3] = {};
    float tanHalfFovY = 0.0f;
    std::uint32_t renderSize[2] = {};
    float invRenderSize[2] = {};
    std::uint32_t frameIndex = 0;
    std::uint32_t sampleIndex = 0;
    std::uint32_t viewMode = 0;
    std::uint32_t instanceCount = 0;
    float rayTMin = 0.0f;
    float rayTMax = 0.0f;
    float aspectRatio = 0.0f;
    std::uint32_t seed = 0;
};
static_assert(sizeof(FrameConstants) == 256, "FrameConstants must match layouts.hlsli");
static_assert(offsetof(FrameConstants, worldToView) == 64);
static_assert(offsetof(FrameConstants, viewToClip) == 128);
static_assert(offsetof(FrameConstants, cameraPosition) == 192);
static_assert(offsetof(FrameConstants, tanHalfFovY) == 204);
static_assert(offsetof(FrameConstants, renderSize) == 208);
static_assert(offsetof(FrameConstants, invRenderSize) == 216);
static_assert(offsetof(FrameConstants, frameIndex) == 224);
static_assert(offsetof(FrameConstants, rayTMin) == 240);
static_assert(offsetof(FrameConstants, seed) == 252);

// Root constant buffer b1 for the path tracer.
struct alignas(16) IntegratorConstants {
    std::uint32_t maxHits = 4;
    std::uint32_t strategy = 0;
    std::uint32_t emitterCount = 0;
    std::uint32_t flags = 0;
    float exposure = 1.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
    float pad2 = 0.0f;
};
static_assert(sizeof(IntegratorConstants) == 32, "IntegratorConstants must match layouts.hlsli");

// Root constant buffer b2 for the guided trace, compose, and upscale passes (denoised mode).
struct alignas(16) GuideConstants {
    float jitterPixels[2] = {};
    std::uint32_t outputSize[2] = {};
    float hitDistParams[3] = {3.0f, 0.1f, 20.0f};  // nrd::ReblurHitDistanceParameters defaults (metres).
    float denoisingRange = 500.0f;
    std::uint32_t flags = 0;
    std::uint32_t historyFrames = 30;
    float exposure = 1.0f;
    std::uint32_t viewMode = 0;
};
static_assert(sizeof(GuideConstants) == 48, "GuideConstants must match layouts.hlsli");
static_assert(offsetof(GuideConstants, hitDistParams) == 16);
static_assert(offsetof(GuideConstants, flags) == 32);
static_assert(offsetof(GuideConstants, viewMode) == 44);

inline constexpr std::uint32_t kGuideFlagGlobalJitter = 1u;
inline constexpr std::uint32_t kGuideFlagReset = 2u;
inline constexpr std::uint32_t kGuideFlagOverlay = 4u;
inline constexpr std::uint32_t kGuideFlagValidation = 8u;
inline constexpr std::uint32_t kMaterialIdDiffuse = 0u;
inline constexpr std::uint32_t kMaterialIdConductor = 1u;

inline constexpr std::uint32_t kStrategyMis = 0;
inline constexpr std::uint32_t kStrategyLight = 1;
inline constexpr std::uint32_t kStrategyBsdf = 2;
inline constexpr std::uint32_t kIntegratorFlagReset = 1u;
inline constexpr std::uint32_t kIntegratorFlagJitter = 2u;

// StructuredBuffer t1, one per TLAS instance (same order as the instance descriptors).
struct InstanceRecord {
    Float4 objectToWorldRow[3];
    Float4 prevObjectToWorldRow[3];
    std::uint32_t meshIndex = 0;
    std::uint32_t materialIndex = 0;
    std::uint32_t stableId = 0;
    std::uint32_t emitterIndex = 0xFFFFFFFFu;
};
static_assert(sizeof(InstanceRecord) == 112, "InstanceRecord must match layouts.hlsli");
static_assert(offsetof(InstanceRecord, prevObjectToWorldRow) == 48);
static_assert(offsetof(InstanceRecord, meshIndex) == 96);

// StructuredBuffer t2, one per mesh.
struct MeshRecord {
    std::uint32_t firstVertex = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
};
static_assert(sizeof(MeshRecord) == 16, "MeshRecord must match layouts.hlsli");

// StructuredBuffer t5, one per scene material.
struct MaterialRecord {
    std::uint32_t type = 0;
    std::uint32_t flags = 0;
    float roughness = 0.0f;
    float pad1 = 0.0f;
    float reflectance[3] = {};
    float pad2 = 0.0f;
    float radiance[3] = {};
    float pad3 = 0.0f;
};
static_assert(sizeof(MaterialRecord) == 48, "MaterialRecord must match layouts.hlsli");
static_assert(offsetof(MaterialRecord, reflectance) == 16);
static_assert(offsetof(MaterialRecord, radiance) == 32);

inline constexpr std::uint32_t kMaterialFlagEmitterOn = 1u;
inline constexpr std::uint32_t kNoEmitter = 0xFFFFFFFFu;

// StructuredBuffer t7, one per emitting triangle.
struct EmitterTriangle {
    std::uint32_t instanceIndex = 0;
    std::uint32_t primitiveIndex = 0;
    float area = 0.0f;
    float cdf = 0.0f;
};
static_assert(sizeof(EmitterTriangle) == 16, "EmitterTriangle must match layouts.hlsli");

// StructuredBuffer t6, one per active emitter.
struct EmitterRecord {
    std::uint32_t instanceIndex = 0;
    std::uint32_t firstTriangle = 0;
    std::uint32_t triangleCount = 0;
    std::uint32_t materialIndex = 0;
    float area = 0.0f;
    float selectionPdf = 0.0f;
    float selectionCdf = 0.0f;
    float pad = 0.0f;
    float radiance[3] = {};
    float pad2 = 0.0f;
};
static_assert(sizeof(EmitterRecord) == 48, "EmitterRecord must match layouts.hlsli");
static_assert(offsetof(EmitterRecord, radiance) == 32);

// RWTexture2D<uint4> u2 texel written by camera_view.hlsl and path_trace.hlsl.
struct HitInfoTexel {
    std::uint32_t stableId = 0;        // kMissId when the ray left the scene.
    std::uint32_t primitiveIndex = 0;
    std::uint32_t flags = 0;           // kHitFlag*.
    std::uint32_t instanceIndex = 0;   // camera_view: instance index; path_trace: mirror bounces before the surface.
};
static_assert(sizeof(HitInfoTexel) == 16);

inline constexpr std::uint32_t kMissId = 0xFFFFFFFFu;
inline constexpr std::uint32_t kHitFlagFrontFace = 1u;
inline constexpr std::uint32_t kHitFlagGeometricFacing = 2u;
inline constexpr std::uint32_t kHitFlagGrazing = 4u;

// Stats buffer (RWStructuredBuffer<uint> u6).
inline constexpr std::uint32_t kStatsNan = 0;
inline constexpr std::uint32_t kStatsInf = 1;
inline constexpr std::uint32_t kStatsNegative = 2;
inline constexpr std::uint32_t kStatsZeroPdf = 3;
inline constexpr std::uint32_t kStatsCount = 8;

// layout_probe.hlsl output size and trailing sentinel.
inline constexpr std::uint32_t kLayoutProbeCount = 35;
inline constexpr std::uint32_t kLayoutProbeSentinel = 0xC0FFEEu;

}  // namespace lc::gpu
