// Frame orchestration for the M1 camera-ray diagnostic pass. Explicit pass list, two frames in
// flight, named GPU timers, and documented full waits for readback only.
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

struct CaptureImages {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ImageRgba8 display;                     // Diagnostic view as shown on screen.
    ImageRgbF32 linear;                     // Same colours as float; alpha (hit distance) dropped.
    std::vector<gpu::HitInfoTexel> hitInfo; // width * height texels, top-down.

    const gpu::HitInfoTexel& HitAt(std::uint32_t x, std::uint32_t y) const { return hitInfo[static_cast<std::size_t>(y) * width + x]; }
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

    // Recreates the output textures (waits for the GPU). Never zero-sized.
    void Resize(std::uint32_t width, std::uint32_t height);

    void BeginFrame();
    void RecordTrace(const RenderSnapshot& snapshot);
    // Copies the display texture into a swap-chain buffer of identical size; skipped with a log
    // message when the sizes differ (the caller resizes on the next frame).
    void RecordCopyToBackBuffer(ID3D12Resource* backBuffer, std::uint32_t backBufferWidth, std::uint32_t backBufferHeight);
    // Closes, executes, and signals. Returns the fence value of this frame.
    std::uint64_t EndFrame();

    // Full GPU wait, then copies the three outputs to the CPU.
    CaptureImages Readback();

    // Runs layout_probe.hlsl with known input data and compares every probed field (test T02).
    std::vector<LayoutProbeEntry> RunLayoutProbe();

    // After a full GPU wait, reads the timings of the most recently submitted frame (useful when a
    // run is too short for the normal two-frame delay).
    void CollectFinalTimings();

    const std::vector<gfx::TimerResult>& LastTimings() const { return lastTimings_; }
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
    gpu::FrameConstants BuildFrameConstants(const RenderSnapshot& snapshot) const;
    void BindCommon(ID3D12GraphicsCommandList4* list, D3D12_GPU_VIRTUAL_ADDRESS constants, D3D12_GPU_VIRTUAL_ADDRESS tlas,
                    D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS meshes, D3D12_GPU_VIRTUAL_ADDRESS positions,
                    D3D12_GPU_VIRTUAL_ADDRESS indices);

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
    gfx::GpuBuffer probeBuffer_;

    gfx::ComPtr<ID3D12RootSignature> rootSignature_;
    std::unique_ptr<gfx::ComputePipeline> cameraView_;
    std::unique_ptr<gfx::ComputePipeline> layoutProbe_;

    std::unique_ptr<SceneGpu> sceneGpu_;
    gfx::TimestampQueries timers_;
    std::uint32_t frameTimer_ = gfx::TimestampQueries::kInvalidTimer;
    std::vector<gfx::TimerResult> lastTimings_;
    bool warnedCopyMismatch_ = false;
};

}  // namespace lc
