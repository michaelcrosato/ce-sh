// Frame orchestration: diagnostic camera-ray pass (M1) and the path tracer in raw and progressive
// reference modes (M2). Explicit pass list, two frames in flight, named GPU timers, documented full
// waits for readback only.
#pragma once

#include "core/image_write.h"
#include "graphics/d3d12/compute_pipeline.h"
#include "graphics/d3d12/descriptor_heap.h"
#include "graphics/d3d12/gpu_buffer.h"
#include "graphics/d3d12/gpu_texture.h"
#include "graphics/d3d12/timestamp_queries.h"
#include "graphics/d3d12/upload_arena.h"
#include "render/gpu_layouts.h"
#include "render/render_snapshot.h"
#include "render/scene_gpu.h"

#include <memory>
#include <string>
#include <vector>

namespace lc {

enum class RenderMode { Diagnostic, Raw, Reference };
enum class Strategy : std::uint32_t { Mis = 0, Light = 1, Bsdf = 2 };

const char* RenderModeName(RenderMode mode);
const char* StrategyName(Strategy strategy);

struct IntegratorSettings {
    std::uint32_t maxHits = 4;        // Surface hits per path including the camera hit and mirror hits.
    Strategy strategy = Strategy::Mis;
    float exposure = 1.0f;            // Display only.
    std::uint32_t seed = 0;
    bool jitter = true;
    std::uint32_t samplesPerFrame = 4;  // Reference mode dispatches per frame (bounded GPU work).
    std::uint32_t targetSamples = 0;    // Reference mode stops adding samples at this count (0 = never).
};

struct CaptureImages {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t sampleCount = 0;          // Samples per pixel behind `linear` (1 for diagnostics).
    ImageRgba8 display;                     // As shown on screen (diagnostic colours or exposed sRGB).
    ImageRgbF32 linear;                     // Diagnostic colour, or the mean scene-linear radiance.
    ImageRgbF32 standardError;              // Per-pixel standard error of the mean (zero when unknown).
    std::vector<gpu::HitInfoTexel> hitInfo; // width * height texels, top-down.

    const gpu::HitInfoTexel& HitAt(std::uint32_t x, std::uint32_t y) const { return hitInfo[static_cast<std::size_t>(y) * width + x]; }
};

struct PatchStats {
    double mean[3] = {};
    double standardError[3] = {};  // Of the patch mean, assuming independent pixels.
    std::uint32_t pixels = 0;
};

// Mean and standard error over the (2 * halfSize + 1)^2 patch centred at (x, y), clamped to the image.
PatchStats ComputePatchStats(const CaptureImages& images, std::uint32_t x, std::uint32_t y, std::uint32_t halfSize);

struct StatsCounters {
    std::uint32_t nan = 0;
    std::uint32_t inf = 0;
    std::uint32_t negative = 0;
    std::uint32_t zeroPdf = 0;
    std::uint32_t Total() const { return nan + inf + negative + zeroPdf; }
};

struct LayoutProbeEntry {
    std::string name;
    std::uint32_t expected = 0;
    std::uint32_t actual = 0;
    bool ok = false;
};

class Renderer {
public:
    Renderer(gfx::Device& device, gfx::GraphicsQueue& queue, std::uint32_t width, std::uint32_t height);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Uploads the scene (setup-time GPU wait).
    void SetScene(const Scene& scene);

    void SetMode(RenderMode mode);
    RenderMode Mode() const { return mode_; }
    void SetIntegrator(const IntegratorSettings& settings);
    const IntegratorSettings& Integrator() const { return settings_; }
    // Samples accumulated per pixel so far (reference mode); reset on incompatible changes.
    std::uint32_t SampleIndex() const { return sampleIndex_; }
    void ResetAccumulation() { accumulationKey_ = 0; }

    // Recreates the output textures (waits for the GPU). Never zero-sized.
    void Resize(std::uint32_t width, std::uint32_t height);

    void BeginFrame();
    void RecordTrace(const RenderSnapshot& snapshot);
    // Copies the display texture into a swap-chain buffer of identical size; skipped with a log
    // message when the sizes differ (the caller resizes on the next frame).
    void RecordCopyToBackBuffer(ID3D12Resource* backBuffer, std::uint32_t backBufferWidth, std::uint32_t backBufferHeight);
    // Closes, executes, and signals. Returns the fence value of this frame.
    std::uint64_t EndFrame();

    // Full GPU wait, then copies the outputs to the CPU.
    CaptureImages Readback();
    StatsCounters ReadStats();

    // Runs layout_probe.hlsl with known input data and compares every probed field (test T02).
    std::vector<LayoutProbeEntry> RunLayoutProbe();

    // After a full GPU wait, reads the timings of the most recently submitted frame.
    void CollectFinalTimings();

    const std::vector<gfx::TimerResult>& LastTimings() const { return lastTimings_; }
    // Frames in which the TLAS was rebuilt (first frame plus every frame with a transform change).
    std::uint64_t TlasRebuildCount() const { return tlasRebuilds_; }
    std::uint32_t Width() const { return width_; }
    std::uint32_t Height() const { return height_; }
    std::uint64_t FrameCounter() const { return frameCounter_; }
    const SceneGpu* SceneData() const { return sceneGpu_.get(); }

private:
    struct FrameContext {
        gfx::ComPtr<ID3D12CommandAllocator> allocator;
        std::unique_ptr<gfx::UploadArena> arena;
        std::uint64_t fenceValue = 0;
    };

    void CreateOutputs(std::uint32_t width, std::uint32_t height);
    void CreateRootSignature();
    void AbandonFrame() noexcept;
    gpu::FrameConstants BuildFrameConstants(const RenderSnapshot& snapshot, std::uint32_t sampleIndex, std::uint32_t seed) const;
    std::uint64_t AccumulationKey(const RenderSnapshot& snapshot) const;
    void BindCommon(ID3D12GraphicsCommandList4* list, D3D12_GPU_VIRTUAL_ADDRESS constants, D3D12_GPU_VIRTUAL_ADDRESS integrator,
                    D3D12_GPU_VIRTUAL_ADDRESS tlas, D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS meshes,
                    D3D12_GPU_VIRTUAL_ADDRESS positions, D3D12_GPU_VIRTUAL_ADDRESS indices, D3D12_GPU_VIRTUAL_ADDRESS materials,
                    D3D12_GPU_VIRTUAL_ADDRESS emitters, D3D12_GPU_VIRTUAL_ADDRESS emitterTriangles);
    void RecordDiagnostic(const RenderSnapshot& snapshot, gfx::UploadArena& arena);
    void RecordPathTrace(const RenderSnapshot& snapshot, gfx::UploadArena& arena);

    gfx::Device& device_;
    gfx::GraphicsQueue& queue_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;

    FrameContext frames_[gfx::kFramesInFlight];
    std::uint32_t frameSlot_ = 0;
    std::uint64_t frameCounter_ = 0;
    bool frameOpen_ = false;
    gfx::ComPtr<ID3D12GraphicsCommandList4> list_;
    gfx::ComPtr<ID3D12CommandAllocator> utilityAllocator_;
    gfx::ComPtr<ID3D12GraphicsCommandList4> utilityList_;

    gfx::DescriptorHeap heap_;
    std::uint32_t uavTable_ = 0;
    gfx::GpuTexture2D display_;
    gfx::GpuTexture2D linear_;
    gfx::GpuTexture2D hitInfo_;
    gfx::GpuTexture2D accum_;
    gfx::GpuTexture2D accumSq_;
    gfx::GpuBuffer probeBuffer_;
    gfx::GpuBuffer statsBuffer_;
    gfx::GpuBuffer statsZero_;

    gfx::ComPtr<ID3D12RootSignature> rootSignature_;
    std::unique_ptr<gfx::ComputePipeline> cameraView_;
    std::unique_ptr<gfx::ComputePipeline> pathTrace_;
    std::unique_ptr<gfx::ComputePipeline> layoutProbe_;

    std::unique_ptr<SceneGpu> sceneGpu_;
    gfx::TimestampQueries timers_;
    std::uint32_t frameTimer_ = gfx::TimestampQueries::kInvalidTimer;
    std::vector<gfx::TimerResult> lastTimings_;
    bool warnedCopyMismatch_ = false;

    RenderMode mode_ = RenderMode::Raw;
    IntegratorSettings settings_;
    std::uint32_t sampleIndex_ = 0;
    std::uint64_t accumulationKey_ = 0;
    std::uint32_t lastDispatchCount_ = 0;
    std::uint64_t tlasRebuilds_ = 0;
};

}  // namespace lc
