// Frame orchestration: diagnostic camera-ray pass (M1), the path tracer in raw and progressive
// reference modes (M2), and the denoised real-time path (M4: guided trace, NRD REBLUR, compose,
// optional resampling to the presented size). Explicit pass list, two frames in flight, named GPU
// timers, documented full waits for readback only.
#pragma once

#include "core/image_write.h"
#include "core/math/mat.h"
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

class NrdDenoiser;

// The three independently selectable image paths of spec §13 plus the M1 diagnostic pass.
enum class RenderMode { Diagnostic, Raw, Reference, Denoised };
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

// Denoised mode (docs/RENDERING.md, "History handling"). Bounded history: 30 frames = 0.5 s at 60 Hz.
struct DenoiserSettings {
    std::uint32_t historyFrames = 30;      // nrd::ReblurSettings::maxAccumulatedFrameNum.
    std::uint32_t fastHistoryFrames = 6;   // nrd::ReblurSettings::maxFastAccumulatedFrameNum.
    float disocclusionThreshold = 0.01f;   // nrd::CommonSettings::disocclusionThreshold (relative depth).
    float diffusePrepassRadius = 0.0f;     // nrd::ReblurSettings::diffusePrepassBlurRadius (pixels); 0 = no pre-accumulation blur.
    float specularPrepassRadius = 50.0f;   // nrd::ReblurSettings::specularPrepassBlurRadius (pixels).
    bool prepassOnlyForSpecularMotion = true;  // nrd::ReblurSettings::usePrepassOnlyForSpecularMotionEstimation.
    float maxBlurRadius = 30.0f;           // nrd::ReblurSettings::maxBlurRadius (pixels, shrinks with accumulation).
    bool antiFirefly = true;
    bool validationOverlay = false;        // NRD draws its validation layer; shown by --view validation.
    bool resetOnSourceChange = false;      // Explicit history reset when the material revision changes (circuit change).
};

struct CaptureImages {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t sampleCount = 0;          // Samples per pixel behind `linear` (1 for diagnostics; frames since reset in denoised mode).
    ImageRgba8 display;                     // As shown on screen (diagnostic colours or exposed sRGB, overlays included).
    ImageRgbF32 linear;                     // Diagnostic colour, the mean scene-linear radiance, or the denoised radiance.
    ImageRgbF32 standardError;              // Per-pixel standard error of the mean (zero when unknown or denoised).
    std::vector<gpu::HitInfoTexel> hitInfo; // width * height texels, top-down.
    // Denoised mode only (empty otherwise).
    ImageRgbF32 rawMean;                    // Mean of the recomposed raw samples since the last history reset.
    ImageRgbF32 rawStandardError;           // Its per-pixel standard error.
    std::vector<float> motion;              // 3 floats per pixel: world-space motion of the guide surface (metres, prev - current).
    std::vector<float> historyLength;       // 2 floats per pixel: diffuse and specular NRD history length in frames.
    std::vector<float> viewZ;               // 1 float per pixel: view-space z of the guide surface.
    std::uint32_t framesSinceReset = 0;

    const gpu::HitInfoTexel& HitAt(std::uint32_t x, std::uint32_t y) const { return hitInfo[static_cast<std::size_t>(y) * width + x]; }
    const float* MotionAt(std::uint32_t x, std::uint32_t y) const { return motion.data() + (static_cast<std::size_t>(y) * width + x) * 3; }
};

struct PatchStats {
    double mean[3] = {};
    double standardError[3] = {};  // Of the patch mean, assuming independent pixels.
    std::uint32_t pixels = 0;
};

// Mean and standard error over the (2 * halfSize + 1)^2 patch centred at (x, y), clamped to the image.
PatchStats ComputePatchStats(const CaptureImages& images, std::uint32_t x, std::uint32_t y, std::uint32_t halfSize);
// The same over an arbitrary image with an optional standard-error image.
PatchStats ComputePatchStats(const ImageRgbF32& image, const ImageRgbF32* standardError, std::uint32_t x, std::uint32_t y, std::uint32_t halfSize);

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

    // Uploads the scene (setup-time GPU wait). Invalidates history.
    void SetScene(const Scene& scene);

    void SetMode(RenderMode mode);
    RenderMode Mode() const { return mode_; }
    void SetIntegrator(const IntegratorSettings& settings);
    const IntegratorSettings& Integrator() const { return settings_; }
    void SetDenoiser(const DenoiserSettings& settings);
    const DenoiserSettings& Denoiser() const { return denoiser_; }
    // Samples accumulated per pixel so far (reference mode) or frames since the last history reset
    // (denoised mode); reset on incompatible changes.
    std::uint32_t SampleIndex() const { return sampleIndex_; }
    void ResetAccumulation() { accumulationKey_ = 0; }
    // Explicit history invalidation for the next frame: scene reload, restart, teleport, tests.
    void ResetHistory() { historyResetPending_ = true; }
    std::uint64_t HistoryResetCount() const { return historyResets_; }

    // Recreates the internal (trace) outputs (waits for the GPU). Never zero-sized.
    void Resize(std::uint32_t width, std::uint32_t height);
    // Presented size when it differs from the internal size: the display image is resampled by a
    // simple bilinear pass (spec §13 "simple diagnostic scale path"). 0x0 restores native presentation.
    void SetOutputSize(std::uint32_t width, std::uint32_t height);
    std::uint32_t OutputWidth() const { return outputWidth_; }
    std::uint32_t OutputHeight() const { return outputHeight_; }

    void BeginFrame();
    void RecordTrace(const RenderSnapshot& snapshot);
    // Copies (or resamples) the display image into a swap-chain buffer; skipped with a log message
    // when the sizes match neither the internal nor the output size (the caller resizes next frame).
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
    // Denoiser statistics (zero / empty until the first denoised frame).
    std::uint64_t DenoiserPoolBytes() const;
    std::uint32_t DenoiserDispatchCount() const;
    static std::string DenoiserVersion();
    // Content hashes of the application's compiled shaders (name, FNV-1a of the DXIL) for reports.
    std::vector<std::pair<std::string, std::uint64_t>> ShaderHashes() const;

private:
    struct FrameContext {
        gfx::ComPtr<ID3D12CommandAllocator> allocator;
        std::unique_ptr<gfx::UploadArena> arena;
        std::uint64_t fenceValue = 0;
    };

    void CreateOutputs(std::uint32_t width, std::uint32_t height);
    void CreateOutputDisplay();
    void CreateRootSignature();
    void EnsureDenoiser();
    void AbandonFrame() noexcept;
    gpu::FrameConstants BuildFrameConstants(const RenderSnapshot& snapshot, std::uint32_t sampleIndex, std::uint32_t seed) const;
    gpu::GuideConstants BuildGuideConstants(const RenderSnapshot& snapshot, bool reset, float jitterX, float jitterY) const;
    std::uint64_t AccumulationKey(const RenderSnapshot& snapshot) const;
    void BindCommon(ID3D12GraphicsCommandList4* list, D3D12_GPU_VIRTUAL_ADDRESS constants, D3D12_GPU_VIRTUAL_ADDRESS integrator,
                    D3D12_GPU_VIRTUAL_ADDRESS guide, D3D12_GPU_VIRTUAL_ADDRESS tlas, D3D12_GPU_VIRTUAL_ADDRESS instances,
                    D3D12_GPU_VIRTUAL_ADDRESS meshes, D3D12_GPU_VIRTUAL_ADDRESS positions, D3D12_GPU_VIRTUAL_ADDRESS indices,
                    D3D12_GPU_VIRTUAL_ADDRESS materials, D3D12_GPU_VIRTUAL_ADDRESS emitters, D3D12_GPU_VIRTUAL_ADDRESS emitterTriangles);
    void BindScene(gfx::UploadArena& arena, const gpu::FrameConstants& frame, const gpu::IntegratorConstants& integrator,
                   const gpu::GuideConstants& guide);
    void RecordDiagnostic(const RenderSnapshot& snapshot, gfx::UploadArena& arena);
    void RecordPathTrace(const RenderSnapshot& snapshot, gfx::UploadArena& arena);
    void RecordDenoised(const RenderSnapshot& snapshot, gfx::UploadArena& arena);
    void ClearStats();

    gfx::Device& device_;
    gfx::GraphicsQueue& queue_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t outputWidth_ = 0;
    std::uint32_t outputHeight_ = 0;

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
    // Denoised mode guides, signals, and outputs (docs/RENDERING.md contract table).
    gfx::GpuTexture2D motion_;
    gfx::GpuTexture2D normalRoughness_;
    gfx::GpuTexture2D viewZ_;
    gfx::GpuTexture2D diffIn_;
    gfx::GpuTexture2D specIn_;
    gfx::GpuTexture2D diffFactor_;
    gfx::GpuTexture2D specFactor_;
    gfx::GpuTexture2D emission_;
    gfx::GpuTexture2D direct_;
    gfx::GpuTexture2D diffOut_;
    gfx::GpuTexture2D specOut_;
    gfx::GpuTexture2D validation_;
    gfx::GpuTexture2D displayOut_;  // Presented size; only when it differs from the internal size.
    gfx::GpuBuffer probeBuffer_;
    gfx::GpuBuffer statsBuffer_;
    gfx::GpuBuffer statsZero_;

    gfx::ComPtr<ID3D12RootSignature> rootSignature_;
    std::unique_ptr<gfx::ComputePipeline> cameraView_;
    std::unique_ptr<gfx::ComputePipeline> pathTrace_;
    std::unique_ptr<gfx::ComputePipeline> pathTraceGuided_;
    std::unique_ptr<gfx::ComputePipeline> compose_;
    std::unique_ptr<gfx::ComputePipeline> upscale_;
    std::unique_ptr<gfx::ComputePipeline> layoutProbe_;
    std::unique_ptr<NrdDenoiser> nrd_;

    std::unique_ptr<SceneGpu> sceneGpu_;
    gfx::TimestampQueries timers_;
    std::uint32_t frameTimer_ = gfx::TimestampQueries::kInvalidTimer;
    std::vector<gfx::TimerResult> lastTimings_;
    bool warnedCopyMismatch_ = false;

    RenderMode mode_ = RenderMode::Raw;
    IntegratorSettings settings_;
    DenoiserSettings denoiser_;
    std::uint32_t sampleIndex_ = 0;
    std::uint64_t accumulationKey_ = 0;
    std::uint32_t lastDispatchCount_ = 0;
    std::uint64_t tlasRebuilds_ = 0;

    // Temporal state of the denoised path.
    bool historyResetPending_ = true;
    bool denoiserFreshlyCreated_ = false;
    std::uint64_t historyResets_ = 0;
    std::uint32_t nrdFrameIndex_ = 0;
    bool havePrevCamera_ = false;
    math::Mat4 prevWorldToView_ = math::Mat4::Identity();
    math::Mat4 prevViewToClip_ = math::Mat4::Identity();
    float prevJitter_[2] = {0.0f, 0.0f};
    bool haveMaterialRevision_ = false;
    std::uint32_t lastMaterialRevision_ = 0;
};

}  // namespace lc
