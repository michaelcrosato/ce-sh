// D3D12 backend for the NVIDIA Real-Time Denoisers (NRD) API. NRD makes no graphics calls: it
// describes pipelines, samplers, texture pools, and per-frame compute dispatches with their
// resource bindings and constants (NRD README, "API"). This class creates the D3D12 objects once,
// then records the dispatches every frame with the state transitions they require.
//
// One instance = one REBLUR_DIFFUSE_SPECULAR denoiser at a fixed resolution; NRD has no resize,
// so the renderer recreates the object when the internal resolution changes.
#pragma once

#include "graphics/d3d12/compute_pipeline.h"
#include "graphics/d3d12/descriptor_heap.h"
#include "graphics/d3d12/gpu_texture.h"
#include "graphics/d3d12/upload_arena.h"

#include "NRD.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lc::gfx {
class Device;
}

namespace lc {

// Application-owned textures bound to NRD's input and output slots. All must be UAV-capable
// textures of the denoiser's resolution in the UNORDERED_ACCESS state when Record is called; they
// are returned to that state afterwards. `validation` may be null unless the overlay is enabled.
struct NrdInputs {
    gfx::GpuTexture2D* motion = nullptr;           // IN_MV
    gfx::GpuTexture2D* normalRoughness = nullptr;  // IN_NORMAL_ROUGHNESS
    gfx::GpuTexture2D* viewZ = nullptr;            // IN_VIEWZ
    gfx::GpuTexture2D* diffIn = nullptr;           // IN_DIFF_RADIANCE_HITDIST
    gfx::GpuTexture2D* specIn = nullptr;           // IN_SPEC_RADIANCE_HITDIST
    gfx::GpuTexture2D* diffOut = nullptr;          // OUT_DIFF_RADIANCE_HITDIST
    gfx::GpuTexture2D* specOut = nullptr;          // OUT_SPEC_RADIANCE_HITDIST
    gfx::GpuTexture2D* validation = nullptr;       // OUT_VALIDATION (optional)
};

class NrdDenoiser {
public:
    NrdDenoiser(gfx::Device& device, std::uint32_t width, std::uint32_t height);
    ~NrdDenoiser();
    NrdDenoiser(const NrdDenoiser&) = delete;
    NrdDenoiser& operator=(const NrdDenoiser&) = delete;

    // Per frame, before Record. Throws lc::Error when NRD rejects the settings.
    void SetCommonSettings(const nrd::CommonSettings& settings);
    // At least once (and whenever they change).
    void SetReblurSettings(const nrd::ReblurSettings& settings);

    // Records every dispatch NRD asks for. Constants go into the frame's upload arena, descriptors
    // into this frame slot's slice of the denoiser's own shader-visible heap. The caller must
    // rebind its descriptor heap and root signature afterwards.
    void Record(ID3D12GraphicsCommandList4* list, gfx::UploadArena& arena, std::uint32_t frameSlot, const NrdInputs& inputs);

    std::uint32_t Width() const { return width_; }
    std::uint32_t Height() const { return height_; }
    std::uint64_t PoolBytes() const { return poolBytes_; }
    std::uint32_t PoolTextureCount() const { return static_cast<std::uint32_t>(pool_.size()); }
    std::uint32_t PipelineCount() const { return static_cast<std::uint32_t>(pipelines_.size()); }
    std::uint32_t LastDispatchCount() const { return lastDispatchCount_; }

    // "major.minor.build" of the linked library.
    static std::string VersionText();

private:
    gfx::GpuTexture2D* Resolve(const nrd::ResourceDesc& resource, const NrdInputs& inputs) const;
    void CreateRootSignature();
    void CreatePipelines();
    void CreatePools();
    void CreateDescriptorHeap();

    gfx::Device& device_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    nrd::Instance* instance_ = nullptr;
    const nrd::InstanceDesc* desc_ = nullptr;
    nrd::Identifier identifier_ = 1;
    gfx::ComPtr<ID3D12RootSignature> rootSignature_;
    std::vector<std::unique_ptr<gfx::ComputePipeline>> pipelines_;
    std::vector<gfx::GpuTexture2D> pool_;  // Permanent pool first, then the transient pool.
    std::unique_ptr<gfx::DescriptorHeap> heap_;
    std::uint32_t texturesPerSet_ = 0;
    std::uint32_t storagesPerSet_ = 0;
    std::uint32_t descriptorsPerSet_ = 0;
    std::uint32_t setsPerFrame_ = 0;
    std::uint64_t poolBytes_ = 0;
    std::uint32_t lastDispatchCount_ = 0;
};

// DXGI equivalent of an NRD texture format; throws lc::Error for formats D3D12 cannot use as
// UAV textures (the RGB32 family).
DXGI_FORMAT NrdFormatToDxgi(nrd::Format format);

}  // namespace lc
