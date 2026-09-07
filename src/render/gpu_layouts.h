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

// StructuredBuffer t1, one per TLAS instance (same order as the instance descriptors).
struct InstanceRecord {
    Float4 objectToWorldRow[3];
    Float4 prevObjectToWorldRow[3];
    std::uint32_t meshIndex = 0;
    std::uint32_t materialIndex = 0;
    std::uint32_t stableId = 0;
    std::uint32_t transformRevision = 0;
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

// RWTexture2D<uint4> u2 texel written by camera_view.hlsl.
struct HitInfoTexel {
    std::uint32_t stableId = 0;        // kMissId when the ray left the scene.
    std::uint32_t primitiveIndex = 0;
    std::uint32_t flags = 0;           // kHitFlag*.
    std::uint32_t instanceIndex = 0;
};
static_assert(sizeof(HitInfoTexel) == 16);

inline constexpr std::uint32_t kMissId = 0xFFFFFFFFu;
inline constexpr std::uint32_t kHitFlagFrontFace = 1u;
inline constexpr std::uint32_t kHitFlagGeometricFacing = 2u;
inline constexpr std::uint32_t kHitFlagGrazing = 4u;

// layout_probe.hlsl output size and trailing sentinel.
inline constexpr std::uint32_t kLayoutProbeCount = 30;
inline constexpr std::uint32_t kLayoutProbeSentinel = 0xC0FFEEu;

}  // namespace lc::gpu
