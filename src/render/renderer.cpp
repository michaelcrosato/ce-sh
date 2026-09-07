#include "render/renderer.h"

#include "core/error.h"
#include "core/log.h"
#include "core/math/camera_math.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/graphics_queue.h"
#include "platform/files.h"
#include "render/nrd_denoiser.h"
#include "render/nrd_settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>

namespace lc {

namespace {

// UAV table layout (root parameter 10). Slots 7 and up belong to the denoised path.
constexpr std::uint32_t kUavDisplay = 0;
constexpr std::uint32_t kUavLinear = 1;
constexpr std::uint32_t kUavHitInfo = 2;
constexpr std::uint32_t kUavProbe = 3;
constexpr std::uint32_t kUavAccum = 4;
constexpr std::uint32_t kUavAccumSq = 5;
constexpr std::uint32_t kUavStats = 6;
constexpr std::uint32_t kUavMotion = 7;
constexpr std::uint32_t kUavNormalRoughness = 8;
constexpr std::uint32_t kUavViewZ = 9;
constexpr std::uint32_t kUavDiffIn = 10;
constexpr std::uint32_t kUavSpecIn = 11;
constexpr std::uint32_t kUavDiffFactor = 12;
constexpr std::uint32_t kUavSpecFactor = 13;
constexpr std::uint32_t kUavEmission = 14;
constexpr std::uint32_t kUavDirect = 15;
constexpr std::uint32_t kUavDiffOut = 16;
constexpr std::uint32_t kUavSpecOut = 17;
constexpr std::uint32_t kUavValidation = 18;
constexpr std::uint32_t kUavDisplayOut = 19;
constexpr std::uint32_t kUavCount = 20;

// Root parameters.
constexpr std::uint32_t kRootFrame = 0;
constexpr std::uint32_t kRootIntegrator = 1;
constexpr std::uint32_t kRootTlas = 2;
constexpr std::uint32_t kRootInstances = 3;
constexpr std::uint32_t kRootMeshes = 4;
constexpr std::uint32_t kRootPositions = 5;
constexpr std::uint32_t kRootIndices = 6;
constexpr std::uint32_t kRootMaterials = 7;
constexpr std::uint32_t kRootEmitters = 8;
constexpr std::uint32_t kRootEmitterTriangles = 9;
constexpr std::uint32_t kRootUavTable = 10;
constexpr std::uint32_t kRootGuide = 11;
constexpr std::uint32_t kRootCount = 12;

constexpr std::uint64_t kArenaBytes = 8ull * 1024 * 1024;
constexpr std::uint32_t kMaxTimersPerFrame = 24;

gpu::Float4x4 ToGpu(const math::Mat4& m) {
    gpu::Float4x4 r;
    std::memcpy(r.m, m.m, sizeof(r.m));
    return r;
}

std::uint32_t AsUint(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

template <class T>
std::span<const std::uint8_t> AsBytes(const T& value) {
    return {reinterpret_cast<const std::uint8_t*>(&value), sizeof(T)};
}

std::uint64_t HashCombine(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t HashFloat(std::uint64_t h, float f) { return HashCombine(h, AsUint(f)); }

void CopyRows(const gfx::GpuBuffer& readback, const gfx::ReadbackPlan& plan, std::uint32_t height,
              const std::function<void(std::uint32_t, const std::uint8_t*)>& consume) {
    auto& rb = const_cast<gfx::GpuBuffer&>(readback);
    const auto* src = static_cast<const std::uint8_t*>(rb.Map());
    for (std::uint32_t y = 0; y < height; ++y) {
        consume(y, src + plan.footprint.Offset + static_cast<std::size_t>(y) * plan.footprint.Footprint.RowPitch);
    }
    rb.Unmap();
}

std::uint32_t Groups(std::uint32_t size) { return (size + 7) / 8; }

}  // namespace

const char* RenderModeName(RenderMode mode) {
    switch (mode) {
        case RenderMode::Diagnostic: return "diag";
        case RenderMode::Raw: return "raw";
        case RenderMode::Reference: return "reference";
        case RenderMode::Denoised: return "denoised";
    }
    return "unknown";
}

const char* StrategyName(Strategy strategy) {
    switch (strategy) {
        case Strategy::Mis: return "mis";
        case Strategy::Light: return "light";
        case Strategy::Bsdf: return "bsdf";
    }
    return "unknown";
}

PatchStats ComputePatchStats(const ImageRgbF32& image, const ImageRgbF32* standardError, std::uint32_t x, std::uint32_t y,
                             std::uint32_t halfSize) {
    PatchStats s;
    if (image.width == 0 || image.height == 0) {
        return s;
    }
    const std::uint32_t x0 = x >= halfSize ? x - halfSize : 0;
    const std::uint32_t y0 = y >= halfSize ? y - halfSize : 0;
    const std::uint32_t x1 = std::min(image.width - 1, x + halfSize);
    const std::uint32_t y1 = std::min(image.height - 1, y + halfSize);
    double sumSe2[3] = {};
    for (std::uint32_t py = y0; py <= y1; ++py) {
        for (std::uint32_t px = x0; px <= x1; ++px) {
            const std::size_t i = (static_cast<std::size_t>(py) * image.width + px) * 3;
            for (int c = 0; c < 3; ++c) {
                s.mean[c] += image.pixels[i + c];
                const double se = (standardError == nullptr || standardError->pixels.empty()) ? 0.0 : standardError->pixels[i + c];
                sumSe2[c] += se * se;
            }
            ++s.pixels;
        }
    }
    if (s.pixels > 0) {
        for (int c = 0; c < 3; ++c) {
            s.mean[c] /= s.pixels;
            s.standardError[c] = std::sqrt(sumSe2[c]) / s.pixels;
        }
    }
    return s;
}

PatchStats ComputePatchStats(const CaptureImages& images, std::uint32_t x, std::uint32_t y, std::uint32_t halfSize) {
    return ComputePatchStats(images.linear, &images.standardError, x, y, halfSize);
}

Renderer::Renderer(gfx::Device& device, gfx::GraphicsQueue& queue, std::uint32_t width, std::uint32_t height)
    : device_(device),
      queue_(queue),
      heap_(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, true, L"Renderer Descriptor Heap"),
      timers_(device, queue, gfx::kFramesInFlight, kMaxTimersPerFrame) {
    for (std::uint32_t i = 0; i < gfx::kFramesInFlight; ++i) {
        LC_CHECK_HR(device.Get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames_[i].allocator)));
        gfx::SetName(frames_[i].allocator.Get(), std::format(L"Frame Allocator {}", i));
        frames_[i].arena = std::make_unique<gfx::UploadArena>(device, kArenaBytes, std::format(L"Frame Upload Arena {}", i));
    }
    LC_CHECK_HR(device.Get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&list_)));
    gfx::SetName(list_.Get(), L"Frame Command List");
    LC_CHECK_HR(list_->Close());

    LC_CHECK_HR(device.Get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&utilityAllocator_)));
    gfx::SetName(utilityAllocator_.Get(), L"Utility Allocator");
    LC_CHECK_HR(device.Get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, utilityAllocator_.Get(), nullptr, IID_PPV_ARGS(&utilityList_)));
    gfx::SetName(utilityList_.Get(), L"Utility Command List");
    LC_CHECK_HR(utilityList_->Close());

    uavTable_ = heap_.AllocateRange(kUavCount);

    probeBuffer_ = gfx::GpuBuffer::CreateDefault(device, L"Layout Probe Output", gpu::kLayoutProbeCount * sizeof(std::uint32_t),
                                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    statsBuffer_ = gfx::GpuBuffer::CreateDefault(device, L"Integrator Stats", gpu::kStatsCount * sizeof(std::uint32_t),
                                                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    statsZero_ = gfx::GpuBuffer::CreateUpload(device, L"Integrator Stats Zero", gpu::kStatsCount * sizeof(std::uint32_t));
    std::memset(statsZero_.Map(), 0, gpu::kStatsCount * sizeof(std::uint32_t));
    statsZero_.Unmap();

    auto bufferUav = [&](const gfx::GpuBuffer& buffer, std::uint32_t elements, std::uint32_t slot) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.FirstElement = 0;
        uav.Buffer.NumElements = elements;
        uav.Buffer.StructureByteStride = sizeof(std::uint32_t);
        uav.Buffer.CounterOffsetInBytes = 0;
        uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device.Get()->CreateUnorderedAccessView(buffer.Get(), nullptr, &uav, heap_.At(uavTable_ + slot).cpu);
    };
    bufferUav(probeBuffer_, gpu::kLayoutProbeCount, kUavProbe);
    bufferUav(statsBuffer_, gpu::kStatsCount, kUavStats);

    CreateOutputs(width, height);
    CreateRootSignature();

    const std::filesystem::path shaderDir = files::ExecutableDirectory() / "shaders";
    cameraView_ = std::make_unique<gfx::ComputePipeline>(device, rootSignature_.Get(), shaderDir / "camera_view.cso", L"Camera View CS");
    pathTrace_ = std::make_unique<gfx::ComputePipeline>(device, rootSignature_.Get(), shaderDir / "path_trace.cso", L"Path Trace CS");
    pathTraceGuided_ = std::make_unique<gfx::ComputePipeline>(device, rootSignature_.Get(), shaderDir / "path_trace_guided.cso", L"Path Trace Guided CS");
    compose_ = std::make_unique<gfx::ComputePipeline>(device, rootSignature_.Get(), shaderDir / "compose.cso", L"Compose CS");
    upscale_ = std::make_unique<gfx::ComputePipeline>(device, rootSignature_.Get(), shaderDir / "upscale.cso", L"Upscale CS");
    layoutProbe_ = std::make_unique<gfx::ComputePipeline>(device, rootSignature_.Get(), shaderDir / "layout_probe.cso", L"Layout Probe CS");
}

Renderer::~Renderer() { queue_.DrainForShutdown(); }

void Renderer::CreateOutputs(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        throw Error(std::format("Renderer::CreateOutputs: zero-sized render target requested ({}x{})", width, height));
    }
    nrd_.reset();  // NRD has no resize: recreated on the first denoised frame at the new size (the caller waited for the GPU).
    width_ = width;
    height_ = height;
    display_ = gfx::GpuTexture2D::CreateUav(device_, L"Display RGBA8", width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    linear_ = gfx::GpuTexture2D::CreateUav(device_, L"Linear RGBA32F", width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    hitInfo_ = gfx::GpuTexture2D::CreateUav(device_, L"Hit Info RGBA32U", width, height, DXGI_FORMAT_R32G32B32A32_UINT);
    accum_ = gfx::GpuTexture2D::CreateUav(device_, L"Accumulation Sum RGBA32F", width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    accumSq_ = gfx::GpuTexture2D::CreateUav(device_, L"Accumulation Sum Of Squares RGBA32F", width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    // Denoised path (formats per the contract table in docs/RENDERING.md).
    motion_ = gfx::GpuTexture2D::CreateUav(device_, L"Guide Motion RGBA32F", width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    normalRoughness_ = gfx::GpuTexture2D::CreateUav(device_, L"Guide Normal Roughness R10G10B10A2", width, height, DXGI_FORMAT_R10G10B10A2_UNORM);
    viewZ_ = gfx::GpuTexture2D::CreateUav(device_, L"Guide ViewZ R32F", width, height, DXGI_FORMAT_R32_FLOAT);
    diffIn_ = gfx::GpuTexture2D::CreateUav(device_, L"Signal Diffuse In RGBA16F", width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    specIn_ = gfx::GpuTexture2D::CreateUav(device_, L"Signal Specular In RGBA16F", width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    diffFactor_ = gfx::GpuTexture2D::CreateUav(device_, L"Diffuse Factor RGBA16F", width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    specFactor_ = gfx::GpuTexture2D::CreateUav(device_, L"Specular Factor RGBA16F", width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    emission_ = gfx::GpuTexture2D::CreateUav(device_, L"Emission RGBA16F", width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    direct_ = gfx::GpuTexture2D::CreateUav(device_, L"Direct RGBA16F", width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    diffOut_ = gfx::GpuTexture2D::CreateUav(device_, L"Signal Diffuse Out RGBA32F", width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    specOut_ = gfx::GpuTexture2D::CreateUav(device_, L"Signal Specular Out RGBA32F", width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    validation_ = gfx::GpuTexture2D::CreateUav(device_, L"NRD Validation RGBA8", width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    const std::pair<const gfx::GpuTexture2D*, std::uint32_t> textures[] = {
        {&display_, kUavDisplay},        {&linear_, kUavLinear},       {&hitInfo_, kUavHitInfo},         {&accum_, kUavAccum},
        {&accumSq_, kUavAccumSq},        {&motion_, kUavMotion},       {&normalRoughness_, kUavNormalRoughness},
        {&viewZ_, kUavViewZ},            {&diffIn_, kUavDiffIn},       {&specIn_, kUavSpecIn},           {&diffFactor_, kUavDiffFactor},
        {&specFactor_, kUavSpecFactor},  {&emission_, kUavEmission},   {&direct_, kUavDirect},           {&diffOut_, kUavDiffOut},
        {&specOut_, kUavSpecOut},        {&validation_, kUavValidation}};
    for (const auto& [texture, slot] : textures) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = texture->Format();
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = 0;
        uav.Texture2D.PlaneSlice = 0;
        device_.Get()->CreateUnorderedAccessView(texture->Get(), nullptr, &uav, heap_.At(uavTable_ + slot).cpu);
    }
    CreateOutputDisplay();
    accumulationKey_ = 0;  // Size changed: history is invalid.
    historyResetPending_ = true;
    havePrevCamera_ = false;
    log::Info("Render outputs created: {}x{} (display RGBA8, linear RGBA32F, hit info RGBA32U, accumulation 2x RGBA32F, 12 denoiser textures)",
              width, height);
}

void Renderer::CreateOutputDisplay() {
    const bool scaled = outputWidth_ != 0 && outputHeight_ != 0 && (outputWidth_ != width_ || outputHeight_ != height_);
    if (scaled) {
        displayOut_ = gfx::GpuTexture2D::CreateUav(device_, L"Display Out RGBA8", outputWidth_, outputHeight_, DXGI_FORMAT_R8G8B8A8_UNORM);
    } else {
        displayOut_ = gfx::GpuTexture2D();
        outputWidth_ = width_;
        outputHeight_ = height_;
    }
    // The slot always holds a valid descriptor (the internal display when no resampling happens).
    const gfx::GpuTexture2D& target = scaled ? displayOut_ : display_;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = target.Format();
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device_.Get()->CreateUnorderedAccessView(target.Get(), nullptr, &uav, heap_.At(uavTable_ + kUavDisplayOut).cpu);
}

void Renderer::SetOutputSize(std::uint32_t width, std::uint32_t height) {
    if (frameOpen_) {
        throw Error("Renderer::SetOutputSize called while a frame is being recorded");
    }
    const std::uint32_t w = width == 0 ? width_ : width;
    const std::uint32_t h = height == 0 ? height_ : height;
    if (w == outputWidth_ && h == outputHeight_) {
        return;
    }
    queue_.WaitIdle();
    outputWidth_ = w;
    outputHeight_ = h;
    CreateOutputDisplay();
    log::Info("Presentation size {}x{} for internal {}x{}{}", outputWidth_, outputHeight_, width_, height_,
              displayOut_.IsValid() ? " (bilinear resampling)" : " (direct copy)");
}

void Renderer::CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE1 uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = kUavCount;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 params[kRootCount]{};
    params[kRootFrame].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;  // b0 FrameConstants
    params[kRootFrame].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    params[kRootIntegrator].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;  // b1 IntegratorConstants
    params[kRootIntegrator].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    params[kRootTlas].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t0 TLAS (rebuilt within the list)
    params[kRootTlas].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    params[kRootInstances].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t1 InstanceRecords (per frame)
    params[kRootInstances].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    params[kRootMeshes].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t2 MeshRecords (static geometry)
    params[kRootMeshes].Descriptor = {2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC};
    params[kRootPositions].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t3 positions
    params[kRootPositions].Descriptor = {3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC};
    params[kRootIndices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t4 indices
    params[kRootIndices].Descriptor = {4, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC};
    params[kRootMaterials].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t5 materials (per frame)
    params[kRootMaterials].Descriptor = {5, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    params[kRootEmitters].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t6 emitters (per frame)
    params[kRootEmitters].Descriptor = {6, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    params[kRootEmitterTriangles].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t7 emitter triangles (per frame)
    params[kRootEmitterTriangles].Descriptor = {7, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    params[kRootUavTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;  // u0..u19
    params[kRootUavTable].DescriptorTable = {1, &uavRange};
    params[kRootGuide].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;  // b2 GuideConstants
    params[kRootGuide].Descriptor = {2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    for (auto& p : params) {
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = static_cast<UINT>(std::size(params));
    desc.Desc_1_1.pParameters = params;
    desc.Desc_1_1.NumStaticSamplers = 0;
    desc.Desc_1_1.pStaticSamplers = nullptr;
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rootSignature_ = gfx::CreateRootSignature(device_, desc, L"Trace Root Signature");
}

void Renderer::SetScene(const Scene& scene) {
    queue_.WaitIdle();
    sceneGpu_ = std::make_unique<SceneGpu>(device_, queue_, scene);
    accumulationKey_ = 0;
    historyResetPending_ = true;
    haveMaterialRevision_ = false;
}

void Renderer::SetMode(RenderMode mode) {
    if (mode != mode_) {
        mode_ = mode;
        accumulationKey_ = 0;
        historyResetPending_ = true;
    }
}

void Renderer::SetIntegrator(const IntegratorSettings& settings) {
    settings_ = settings;
    accumulationKey_ = 0;
}

void Renderer::SetDenoiser(const DenoiserSettings& settings) {
    denoiser_ = settings;
}

void Renderer::Resize(std::uint32_t width, std::uint32_t height) {
    if (frameOpen_) {
        throw Error("Renderer::Resize called while a frame is being recorded");
    }
    if (width == width_ && height == height_) {
        return;
    }
    queue_.WaitIdle();
    const bool nativeOutput = outputWidth_ == width_ && outputHeight_ == height_;
    if (nativeOutput) {
        outputWidth_ = width;
        outputHeight_ = height;
    }
    CreateOutputs(width, height);
}

void Renderer::EnsureDenoiser() {
    if (!nrd_) {
        nrd_ = std::make_unique<NrdDenoiser>(device_, width_, height_);
        denoiserFreshlyCreated_ = true;
        historyResetPending_ = true;
        havePrevCamera_ = false;
    }
}

std::vector<std::pair<std::string, std::uint64_t>> Renderer::ShaderHashes() const {
    std::vector<std::pair<std::string, std::uint64_t>> hashes;
    for (const gfx::ComputePipeline* p : {cameraView_.get(), pathTrace_.get(), pathTraceGuided_.get(), compose_.get(), upscale_.get(), layoutProbe_.get()}) {
        if (p != nullptr) hashes.emplace_back(p->Source().filename().string(), p->Hash());
    }
    return hashes;
}

std::uint64_t Renderer::DenoiserPoolBytes() const { return nrd_ ? nrd_->PoolBytes() : 0; }
std::uint32_t Renderer::DenoiserDispatchCount() const { return nrd_ ? nrd_->LastDispatchCount() : 0; }
std::string Renderer::DenoiserVersion() { return NrdDenoiser::VersionText(); }

gpu::FrameConstants Renderer::BuildFrameConstants(const RenderSnapshot& snapshot, std::uint32_t sampleIndex, std::uint32_t seed) const {
    gpu::FrameConstants c{};
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const Camera& cam = snapshot.camera;
    const float verticalFov = cam.VerticalFov(aspect);
    const math::Mat4 viewToWorld = cam.ViewToWorld();
    c.viewToWorld = ToGpu(viewToWorld);
    c.worldToView = ToGpu(viewToWorld.Inverse());
    c.viewToClip = ToGpu(math::PerspectiveRhZeroToOne(verticalFov, aspect, cam.nearZ, cam.farZ));
    c.cameraPosition[0] = cam.position.x;
    c.cameraPosition[1] = cam.position.y;
    c.cameraPosition[2] = cam.position.z;
    c.tanHalfFovY = std::tan(verticalFov * 0.5f);
    c.renderSize[0] = width_;
    c.renderSize[1] = height_;
    c.invRenderSize[0] = 1.0f / static_cast<float>(width_);
    c.invRenderSize[1] = 1.0f / static_cast<float>(height_);
    c.frameIndex = snapshot.frameIndex;
    c.sampleIndex = sampleIndex;
    c.viewMode = static_cast<std::uint32_t>(snapshot.view);
    c.instanceCount = sceneGpu_ ? sceneGpu_->InstanceCount() : 0;
    c.rayTMin = 0.0f;
    c.rayTMax = cam.farZ;
    c.aspectRatio = aspect;
    c.seed = seed;
    return c;
}

gpu::GuideConstants Renderer::BuildGuideConstants(const RenderSnapshot& snapshot, bool reset, float jitterX, float jitterY) const {
    gpu::GuideConstants g{};
    g.jitterPixels[0] = jitterX;
    g.jitterPixels[1] = jitterY;
    g.outputSize[0] = outputWidth_;
    g.outputSize[1] = outputHeight_;
    g.hitDistParams[0] = nrd_settings::kHitDistanceA;
    g.hitDistParams[1] = nrd_settings::kHitDistanceB;
    g.hitDistParams[2] = nrd_settings::kHitDistanceC;
    g.denoisingRange = nrd_settings::kDenoisingRangeMetres;
    g.flags = gpu::kGuideFlagGlobalJitter | (reset ? gpu::kGuideFlagReset : 0u) | (snapshot.overlay ? gpu::kGuideFlagOverlay : 0u) |
              (denoiser_.validationOverlay ? gpu::kGuideFlagValidation : 0u);
    g.historyFrames = denoiser_.historyFrames;
    g.exposure = settings_.exposure;
    g.viewMode = static_cast<std::uint32_t>(snapshot.view);
    return g;
}

std::uint64_t Renderer::AccumulationKey(const RenderSnapshot& snapshot) const {
    const Camera& cam = snapshot.camera;
    std::uint64_t h = 0xA5A5A5A5ull;
    h = HashFloat(h, cam.position.x);
    h = HashFloat(h, cam.position.y);
    h = HashFloat(h, cam.position.z);
    h = HashFloat(h, cam.yawRadians);
    h = HashFloat(h, cam.pitchRadians);
    h = HashFloat(h, cam.horizontalFovRadians);
    h = HashFloat(h, cam.nearZ);
    h = HashFloat(h, cam.farZ);
    h = HashCombine(h, width_);
    h = HashCombine(h, height_);
    h = HashCombine(h, static_cast<std::uint64_t>(mode_));
    h = HashCombine(h, settings_.maxHits);
    h = HashCombine(h, static_cast<std::uint64_t>(settings_.strategy));
    h = HashCombine(h, settings_.seed);
    h = HashCombine(h, settings_.jitter ? 1u : 0u);
    h = HashCombine(h, sceneGpu_ ? sceneGpu_->SceneRevisionHash() : 0);
    return h == 0 ? 1 : h;
}

void Renderer::BindCommon(ID3D12GraphicsCommandList4* list, D3D12_GPU_VIRTUAL_ADDRESS constants, D3D12_GPU_VIRTUAL_ADDRESS integrator,
                          D3D12_GPU_VIRTUAL_ADDRESS guide, D3D12_GPU_VIRTUAL_ADDRESS tlas, D3D12_GPU_VIRTUAL_ADDRESS instances,
                          D3D12_GPU_VIRTUAL_ADDRESS meshes, D3D12_GPU_VIRTUAL_ADDRESS positions, D3D12_GPU_VIRTUAL_ADDRESS indices,
                          D3D12_GPU_VIRTUAL_ADDRESS materials, D3D12_GPU_VIRTUAL_ADDRESS emitters, D3D12_GPU_VIRTUAL_ADDRESS emitterTriangles) {
    ID3D12DescriptorHeap* heaps[] = {heap_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(rootSignature_.Get());
    list->SetComputeRootConstantBufferView(kRootFrame, constants);
    list->SetComputeRootConstantBufferView(kRootIntegrator, integrator);
    list->SetComputeRootShaderResourceView(kRootTlas, tlas);
    list->SetComputeRootShaderResourceView(kRootInstances, instances);
    list->SetComputeRootShaderResourceView(kRootMeshes, meshes);
    list->SetComputeRootShaderResourceView(kRootPositions, positions);
    list->SetComputeRootShaderResourceView(kRootIndices, indices);
    list->SetComputeRootShaderResourceView(kRootMaterials, materials);
    list->SetComputeRootShaderResourceView(kRootEmitters, emitters);
    list->SetComputeRootShaderResourceView(kRootEmitterTriangles, emitterTriangles);
    list->SetComputeRootDescriptorTable(kRootUavTable, heap_.At(uavTable_).gpu);
    list->SetComputeRootConstantBufferView(kRootGuide, guide);
}

void Renderer::BindScene(gfx::UploadArena& arena, const gpu::FrameConstants& frame, const gpu::IntegratorConstants& integrator,
                         const gpu::GuideConstants& guide) {
    const gfx::UploadAllocation cb = arena.Allocate(sizeof(frame));
    std::memcpy(cb.cpu, &frame, sizeof(frame));
    const gfx::UploadAllocation icb = arena.Allocate(sizeof(integrator));
    std::memcpy(icb.cpu, &integrator, sizeof(integrator));
    const gfx::UploadAllocation gcb = arena.Allocate(sizeof(guide));
    std::memcpy(gcb.cpu, &guide, sizeof(guide));
    BindCommon(list_.Get(), cb.gpu, icb.gpu, gcb.gpu, sceneGpu_->TlasAddress(), sceneGpu_->InstanceRecordsAddress(), sceneGpu_->MeshRecordsAddress(),
               sceneGpu_->PositionsAddress(), sceneGpu_->IndicesAddress(), sceneGpu_->MaterialsAddress(), sceneGpu_->EmittersAddress(),
               sceneGpu_->EmitterTrianglesAddress());
}

void Renderer::ClearStats() {
    list_->CopyBufferRegion(statsBuffer_.Get(), 0, statsZero_.Get(), 0, gpu::kStatsCount * sizeof(std::uint32_t));
    const D3D12_RESOURCE_BARRIER toUav = gfx::TransitionBarrier(statsBuffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list_->ResourceBarrier(1, &toUav);
}

void Renderer::BeginFrame() {
    if (frameOpen_) {
        throw Error("Renderer::BeginFrame called twice without EndFrame");
    }
    frameSlot_ = static_cast<std::uint32_t>(frameCounter_ % gfx::kFramesInFlight);
    FrameContext& frame = frames_[frameSlot_];
    if (frame.fenceValue != 0) {
        queue_.WaitForFenceValue(frame.fenceValue);
        lastTimings_ = timers_.Collect(frameSlot_);
    }
    frame.arena->Reset();
    LC_CHECK_HR(frame.allocator->Reset());
    LC_CHECK_HR(list_->Reset(frame.allocator.Get(), nullptr));
    timers_.BeginFrame(frameSlot_);
    frameTimer_ = timers_.Begin(list_.Get(), "frame_gpu");
    frameOpen_ = true;
}

void Renderer::RecordTrace(const RenderSnapshot& snapshot) {
    if (!frameOpen_) {
        throw Error("Renderer::RecordTrace called outside BeginFrame/EndFrame");
    }
    if (!sceneGpu_ || snapshot.scene == nullptr) {
        throw Error("Renderer::RecordTrace: no scene set");
    }
    try {
        FrameContext& frame = frames_[frameSlot_];

        // Order this frame's UAV writes after the previous frame's dispatches and copies. Two frames
        // may be in flight; nothing else serializes their accesses to the output textures.
        const D3D12_RESOURCE_BARRIER allUavs = gfx::UavBarrier(nullptr);
        list_->ResourceBarrier(1, &allUavs);

        const std::uint32_t tlasTimer = timers_.Begin(list_.Get(), "scene_update");
        sceneGpu_->UpdateFrame(*snapshot.scene, *frame.arena, list_.Get());
        timers_.End(list_.Get(), tlasTimer);
        if (sceneGpu_->TlasRebuiltThisFrame()) {
            ++tlasRebuilds_;
        }

        switch (mode_) {
            case RenderMode::Diagnostic: RecordDiagnostic(snapshot, *frame.arena); break;
            case RenderMode::Raw:
            case RenderMode::Reference: RecordPathTrace(snapshot, *frame.arena); break;
            case RenderMode::Denoised: RecordDenoised(snapshot, *frame.arena); break;
        }
    } catch (...) {
        AbandonFrame();
        throw;
    }
}

void Renderer::RecordDiagnostic(const RenderSnapshot& snapshot, gfx::UploadArena& arena) {
    const gpu::FrameConstants constants = BuildFrameConstants(snapshot, 0, settings_.seed);
    const gpu::IntegratorConstants ic{};
    const gpu::GuideConstants guide = BuildGuideConstants(snapshot, false, 0.0f, 0.0f);
    BindScene(arena, constants, ic, guide);
    list_->SetPipelineState(cameraView_->Get());
    const std::uint32_t traceTimer = timers_.Begin(list_.Get(), "trace");
    list_->Dispatch(Groups(width_), Groups(height_), 1);
    timers_.End(list_.Get(), traceTimer);
    lastDispatchCount_ = 1;
}

void Renderer::RecordPathTrace(const RenderSnapshot& snapshot, gfx::UploadArena& arena) {
    const std::uint64_t key = AccumulationKey(snapshot);
    if (key != accumulationKey_) {
        accumulationKey_ = key;
        sampleIndex_ = 0;
    }

    std::uint32_t dispatches = 1;
    std::uint32_t seedBase = settings_.seed;
    if (mode_ == RenderMode::Raw) {
        sampleIndex_ = 0;  // Raw mode shows the current sample only; the seed varies per frame.
        seedBase = settings_.seed + snapshot.frameIndex * 7919u;
    } else {
        dispatches = std::max<std::uint32_t>(1, settings_.samplesPerFrame);
        if (settings_.targetSamples != 0) {
            if (sampleIndex_ >= settings_.targetSamples) {
                dispatches = 0;
            } else {
                dispatches = std::min(dispatches, settings_.targetSamples - sampleIndex_);
            }
        }
    }
    lastDispatchCount_ = dispatches;
    if (dispatches == 0) {
        return;  // Reference target reached: keep the accumulated image as it is.
    }

    if (sampleIndex_ == 0) {
        ClearStats();  // Fresh accumulation: clear the invalid-value counters.
    }

    const gpu::GuideConstants guide = BuildGuideConstants(snapshot, false, 0.0f, 0.0f);
    const std::uint32_t traceTimer = timers_.Begin(list_.Get(), "path_trace");
    for (std::uint32_t i = 0; i < dispatches; ++i) {
        const gpu::FrameConstants constants = BuildFrameConstants(snapshot, sampleIndex_, seedBase);
        gpu::IntegratorConstants ic{};
        ic.maxHits = settings_.maxHits;
        ic.strategy = static_cast<std::uint32_t>(settings_.strategy);
        ic.emitterCount = sceneGpu_->EmitterCount();
        ic.flags = (sampleIndex_ == 0 ? gpu::kIntegratorFlagReset : 0u) | (settings_.jitter ? gpu::kIntegratorFlagJitter : 0u);
        ic.exposure = settings_.exposure;
        BindScene(arena, constants, ic, guide);
        list_->SetPipelineState(pathTrace_->Get());
        list_->Dispatch(Groups(width_), Groups(height_), 1);
        // Each dispatch reads the sums the previous one wrote.
        const D3D12_RESOURCE_BARRIER all = gfx::UavBarrier(nullptr);
        list_->ResourceBarrier(1, &all);
        ++sampleIndex_;
    }
    timers_.End(list_.Get(), traceTimer);
}

void Renderer::RecordDenoised(const RenderSnapshot& snapshot, gfx::UploadArena& arena) {
    EnsureDenoiser();

    // History decisions (docs/RENDERING.md, "History handling").
    const std::uint32_t materialRevision = snapshot.scene->MaterialRevision();
    bool reset = historyResetPending_ || snapshot.cameraCut;
    if (denoiser_.resetOnSourceChange && haveMaterialRevision_ && materialRevision != lastMaterialRevision_) {
        reset = true;
    }
    lastMaterialRevision_ = materialRevision;
    haveMaterialRevision_ = true;
    if (reset) {
        sampleIndex_ = 0;
        ++historyResets_;
        historyResetPending_ = false;
    }
    if (sampleIndex_ == 0) {
        ClearStats();
    }

    // One global sub-pixel offset per frame; NRD receives the identical value below.
    const nrd_settings::Jitter jitter = nrd_settings::FrameJitter(nrdFrameIndex_);
    const std::uint32_t seed = settings_.seed + snapshot.frameIndex * 7919u;
    const gpu::FrameConstants constants = BuildFrameConstants(snapshot, sampleIndex_, seed);
    gpu::IntegratorConstants ic{};
    ic.maxHits = settings_.maxHits;
    ic.strategy = static_cast<std::uint32_t>(settings_.strategy);
    ic.emitterCount = sceneGpu_->EmitterCount();
    ic.flags = (sampleIndex_ == 0 ? gpu::kIntegratorFlagReset : 0u) | (settings_.jitter ? gpu::kIntegratorFlagJitter : 0u);
    ic.exposure = settings_.exposure;
    const gpu::GuideConstants guide = BuildGuideConstants(snapshot, reset, jitter.x, jitter.y);

    // 1. Guided trace: one path per pixel, guides and split signals.
    BindScene(arena, constants, ic, guide);
    list_->SetPipelineState(pathTraceGuided_->Get());
    const std::uint32_t traceTimer = timers_.Begin(list_.Get(), "path_trace");
    list_->Dispatch(Groups(width_), Groups(height_), 1);
    timers_.End(list_.Get(), traceTimer);
    const D3D12_RESOURCE_BARRIER guidesWritten = gfx::UavBarrier(nullptr);
    list_->ResourceBarrier(1, &guidesWritten);

    // 2. NRD REBLUR diffuse + specular.
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const Camera& cam = snapshot.camera;
    const math::Mat4 worldToView = cam.ViewToWorld().Inverse();
    const math::Mat4 viewToClip = math::PerspectiveRhZeroToOne(cam.VerticalFov(aspect), aspect, cam.nearZ, cam.farZ);
    if (!havePrevCamera_) {
        prevWorldToView_ = worldToView;
        prevViewToClip_ = viewToClip;
        prevJitter_[0] = jitter.x;
        prevJitter_[1] = jitter.y;
    }
    nrd::CommonSettings cs{};
    nrd_settings::ToColumnMajor(viewToClip, cs.viewToClipMatrix);
    nrd_settings::ToColumnMajor(prevViewToClip_, cs.viewToClipMatrixPrev);
    nrd_settings::ToColumnMajor(worldToView, cs.worldToViewMatrix);
    nrd_settings::ToColumnMajor(prevWorldToView_, cs.worldToViewMatrixPrev);
    cs.motionVectorScale[0] = 1.0f;
    cs.motionVectorScale[1] = 1.0f;
    cs.motionVectorScale[2] = 1.0f;
    cs.isMotionVectorInWorldSpace = true;
    cs.cameraJitter[0] = jitter.x;
    cs.cameraJitter[1] = jitter.y;
    cs.cameraJitterPrev[0] = prevJitter_[0];
    cs.cameraJitterPrev[1] = prevJitter_[1];
    cs.resourceSize[0] = cs.resourceSizePrev[0] = cs.rectSize[0] = cs.rectSizePrev[0] = static_cast<std::uint16_t>(width_);
    cs.resourceSize[1] = cs.resourceSizePrev[1] = cs.rectSize[1] = cs.rectSizePrev[1] = static_cast<std::uint16_t>(height_);
    cs.viewZScale = 1.0f;
    cs.timeDeltaBetweenFrames = snapshot.frameDeltaMs;
    cs.denoisingRange = nrd_settings::kDenoisingRangeMetres;
    cs.disocclusionThreshold = denoiser_.disocclusionThreshold;
    cs.frameIndex = nrdFrameIndex_;
    cs.accumulationMode = reset ? (denoiserFreshlyCreated_ ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::RESTART)
                                : nrd::AccumulationMode::CONTINUE;
    cs.isHistoryConfidenceAvailable = false;
    cs.isDisocclusionThresholdMixAvailable = false;
    cs.enableValidation = denoiser_.validationOverlay;
    denoiserFreshlyCreated_ = false;

    nrd::ReblurSettings rs{};
    rs.hitDistanceParameters.A = nrd_settings::kHitDistanceA;
    rs.hitDistanceParameters.B = nrd_settings::kHitDistanceB;
    rs.hitDistanceParameters.C = nrd_settings::kHitDistanceC;
    rs.maxAccumulatedFrameNum = std::min<std::uint32_t>(denoiser_.historyFrames, nrd::REBLUR_MAX_HISTORY_FRAME_NUM);
    rs.maxFastAccumulatedFrameNum = std::min(denoiser_.fastHistoryFrames, rs.maxAccumulatedFrameNum);
    rs.historyFixFrameNum = std::min<std::uint32_t>(3, rs.maxFastAccumulatedFrameNum);
    rs.diffusePrepassBlurRadius = denoiser_.diffusePrepassRadius;
    rs.specularPrepassBlurRadius = denoiser_.specularPrepassRadius;
    rs.usePrepassOnlyForSpecularMotionEstimation = denoiser_.prepassOnlyForSpecularMotion;
    rs.maxBlurRadius = denoiser_.maxBlurRadius;
    rs.enableAntiFirefly = denoiser_.antiFirefly;
    rs.returnHistoryLengthInsteadOfOcclusion = true;  // .w of the outputs = history age, the "history validity" diagnostic.
    rs.minMaterialForDiffuse = 0.0f;  // Exact material-id comparison: diffuse surfaces and conductors never mix.
    rs.minMaterialForSpecular = 0.0f;

    NrdInputs inputs;
    inputs.motion = &motion_;
    inputs.normalRoughness = &normalRoughness_;
    inputs.viewZ = &viewZ_;
    inputs.diffIn = &diffIn_;
    inputs.specIn = &specIn_;
    inputs.diffOut = &diffOut_;
    inputs.specOut = &specOut_;
    inputs.validation = &validation_;

    const std::uint32_t denoiseTimer = timers_.Begin(list_.Get(), "denoise");
    nrd_->SetReblurSettings(rs);
    nrd_->SetCommonSettings(cs);
    nrd_->Record(list_.Get(), arena, frameSlot_, inputs);
    timers_.End(list_.Get(), denoiseTimer);

    // 3. Compose: modulate, add emission, expose, accumulate the raw invariant, draw overlays.
    BindScene(arena, constants, ic, guide);  // NRD bound its own heap and root signature.
    const D3D12_RESOURCE_BARRIER denoised = gfx::UavBarrier(nullptr);
    list_->ResourceBarrier(1, &denoised);
    list_->SetPipelineState(compose_->Get());
    const std::uint32_t composeTimer = timers_.Begin(list_.Get(), "compose");
    list_->Dispatch(Groups(width_), Groups(height_), 1);
    timers_.End(list_.Get(), composeTimer);

    prevWorldToView_ = worldToView;
    prevViewToClip_ = viewToClip;
    prevJitter_[0] = jitter.x;
    prevJitter_[1] = jitter.y;
    havePrevCamera_ = true;
    ++nrdFrameIndex_;
    ++sampleIndex_;
    lastDispatchCount_ = 1;
}

void Renderer::RecordCopyToBackBuffer(ID3D12Resource* backBuffer, std::uint32_t backBufferWidth, std::uint32_t backBufferHeight) {
    if (!frameOpen_) {
        throw Error("Renderer::RecordCopyToBackBuffer called outside BeginFrame/EndFrame");
    }
    gfx::GpuTexture2D* source = nullptr;
    if (backBufferWidth == width_ && backBufferHeight == height_) {
        source = &display_;
    } else if (displayOut_.IsValid() && backBufferWidth == outputWidth_ && backBufferHeight == outputHeight_) {
        // Resample the internal display image to the presented size (root parameters are still bound).
        const std::uint32_t upscaleTimer = timers_.Begin(list_.Get(), "upscale");
        const D3D12_RESOURCE_BARRIER displayWritten = gfx::UavBarrier(display_.Get());
        list_->ResourceBarrier(1, &displayWritten);
        list_->SetPipelineState(upscale_->Get());
        list_->Dispatch(Groups(outputWidth_), Groups(outputHeight_), 1);
        const D3D12_RESOURCE_BARRIER resampled = gfx::UavBarrier(displayOut_.Get());
        list_->ResourceBarrier(1, &resampled);
        timers_.End(list_.Get(), upscaleTimer);
        source = &displayOut_;
    } else {
        if (!warnedCopyMismatch_) {
            log::Warn("skipping present copy: render {}x{} (output {}x{}) vs swap chain {}x{}", width_, height_, outputWidth_, outputHeight_,
                      backBufferWidth, backBufferHeight);
            warnedCopyMismatch_ = true;
        }
        return;
    }
    warnedCopyMismatch_ = false;
    const std::uint32_t copyTimer = timers_.Begin(list_.Get(), "copy_out");
    source->Transition(list_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    const D3D12_RESOURCE_BARRIER toCopy = gfx::TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    list_->ResourceBarrier(1, &toCopy);
    list_->CopyResource(backBuffer, source->Get());
    const D3D12_RESOURCE_BARRIER toPresent = gfx::TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    list_->ResourceBarrier(1, &toPresent);
    source->Transition(list_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    timers_.End(list_.Get(), copyTimer);
}

void Renderer::RecordOverlay(const std::function<void(ID3D12GraphicsCommandList4*)>& record) {
    if (!frameOpen_) {
        throw Error("Renderer::RecordOverlay called outside BeginFrame/EndFrame");
    }
    const std::uint32_t timer = timers_.Begin(list_.Get(), "overlay");
    record(list_.Get());
    timers_.End(list_.Get(), timer);
}

std::uint64_t Renderer::EndFrame() {
    if (!frameOpen_) {
        throw Error("Renderer::EndFrame called without BeginFrame");
    }
    timers_.End(list_.Get(), frameTimer_);
    timers_.Resolve(list_.Get());
    frameOpen_ = false;  // Whatever happens below, the frame is no longer being recorded.
    LC_CHECK_HR(list_->Close());
    queue_.Execute(list_.Get());
    FrameContext& frame = frames_[frameSlot_];
    frame.fenceValue = queue_.Signal();
    ++frameCounter_;
    return frame.fenceValue;
}

void Renderer::AbandonFrame() noexcept {
    if (!frameOpen_) {
        return;
    }
    frameOpen_ = false;
    list_->Close();  // Nothing is submitted; the allocator is reset on the next BeginFrame.
}

void Renderer::CollectFinalTimings() {
    if (frameOpen_ || frameCounter_ == 0) {
        return;
    }
    queue_.WaitIdle();
    const auto slot = static_cast<std::uint32_t>((frameCounter_ - 1) % gfx::kFramesInFlight);
    lastTimings_ = timers_.Collect(slot);
}

CaptureImages Renderer::Readback() {
    if (frameOpen_) {
        throw Error("Renderer::Readback called while a frame is being recorded");
    }
    queue_.WaitIdle();  // Documented full wait: captures are not a per-frame path.
    LC_CHECK_HR(utilityAllocator_->Reset());
    LC_CHECK_HR(utilityList_->Reset(utilityAllocator_.Get(), nullptr));

    const bool pathMode = mode_ != RenderMode::Diagnostic;
    const bool denoised = mode_ == RenderMode::Denoised;
    const gfx::ReadbackPlan planDisplay = display_.PlanReadback(device_);
    const gfx::ReadbackPlan planLinear = linear_.PlanReadback(device_);
    const gfx::ReadbackPlan planHit = hitInfo_.PlanReadback(device_);
    const gfx::ReadbackPlan planAccum = accum_.PlanReadback(device_);
    const gfx::ReadbackPlan planViewZ = viewZ_.PlanReadback(device_);
    gfx::GpuBuffer rbDisplay = gfx::GpuBuffer::CreateReadback(device_, L"Readback Display", planDisplay.totalBytes);
    gfx::GpuBuffer rbLinear = gfx::GpuBuffer::CreateReadback(device_, L"Readback Linear", planLinear.totalBytes);
    gfx::GpuBuffer rbHit = gfx::GpuBuffer::CreateReadback(device_, L"Readback Hit Info", planHit.totalBytes);
    gfx::GpuBuffer rbAccum = gfx::GpuBuffer::CreateReadback(device_, L"Readback Accum", planAccum.totalBytes);
    gfx::GpuBuffer rbAccumSq = gfx::GpuBuffer::CreateReadback(device_, L"Readback Accum Sq", planAccum.totalBytes);
    gfx::GpuBuffer rbMotion = gfx::GpuBuffer::CreateReadback(device_, L"Readback Motion", planAccum.totalBytes);
    gfx::GpuBuffer rbDiffOut = gfx::GpuBuffer::CreateReadback(device_, L"Readback Diffuse Out", planAccum.totalBytes);
    gfx::GpuBuffer rbSpecOut = gfx::GpuBuffer::CreateReadback(device_, L"Readback Specular Out", planAccum.totalBytes);
    gfx::GpuBuffer rbViewZ = gfx::GpuBuffer::CreateReadback(device_, L"Readback ViewZ", planViewZ.totalBytes);
    display_.RecordCopyToReadback(utilityList_.Get(), rbDisplay, planDisplay);
    linear_.RecordCopyToReadback(utilityList_.Get(), rbLinear, planLinear);
    hitInfo_.RecordCopyToReadback(utilityList_.Get(), rbHit, planHit);
    if (pathMode) {
        accum_.RecordCopyToReadback(utilityList_.Get(), rbAccum, planAccum);
        accumSq_.RecordCopyToReadback(utilityList_.Get(), rbAccumSq, planAccum);
    }
    if (denoised) {
        motion_.RecordCopyToReadback(utilityList_.Get(), rbMotion, planAccum);
        diffOut_.RecordCopyToReadback(utilityList_.Get(), rbDiffOut, planAccum);
        specOut_.RecordCopyToReadback(utilityList_.Get(), rbSpecOut, planAccum);
        viewZ_.RecordCopyToReadback(utilityList_.Get(), rbViewZ, planViewZ);
    }
    LC_CHECK_HR(utilityList_->Close());
    queue_.Execute(utilityList_.Get());
    queue_.WaitIdle();

    CaptureImages out;
    out.width = width_;
    out.height = height_;
    out.sampleCount = pathMode ? std::max<std::uint32_t>(1, sampleIndex_) : 1;
    out.framesSinceReset = denoised ? sampleIndex_ : 0;
    const std::size_t pixelCount = static_cast<std::size_t>(width_) * height_;
    const std::size_t rowFloats4 = static_cast<std::size_t>(width_) * 16;

    out.display.width = width_;
    out.display.height = height_;
    out.display.pixels.resize(pixelCount * 4);
    CopyRows(rbDisplay, planDisplay, height_, [&](std::uint32_t y, const std::uint8_t* row) {
        std::memcpy(out.display.pixels.data() + static_cast<std::size_t>(y) * width_ * 4, row, static_cast<std::size_t>(width_) * 4);
    });

    auto readRgb = [&](const gfx::GpuBuffer& buffer, ImageRgbF32& image) {
        image.width = width_;
        image.height = height_;
        image.pixels.resize(pixelCount * 3);
        CopyRows(buffer, planAccum, height_, [&](std::uint32_t y, const std::uint8_t* rowBytes) {
            const auto* row = reinterpret_cast<const float*>(rowBytes);
            float* dst = image.pixels.data() + static_cast<std::size_t>(y) * width_ * 3;
            for (std::uint32_t x = 0; x < width_; ++x) {
                dst[x * 3 + 0] = row[x * 4 + 0];
                dst[x * 3 + 1] = row[x * 4 + 1];
                dst[x * 3 + 2] = row[x * 4 + 2];
            }
        });
    };
    (void)rowFloats4;
    readRgb(rbLinear, out.linear);

    out.hitInfo.resize(pixelCount);
    CopyRows(rbHit, planHit, height_, [&](std::uint32_t y, const std::uint8_t* row) {
        std::memcpy(out.hitInfo.data() + static_cast<std::size_t>(y) * width_, row, static_cast<std::size_t>(width_) * sizeof(gpu::HitInfoTexel));
    });

    out.standardError.width = width_;
    out.standardError.height = height_;
    out.standardError.pixels.assign(pixelCount * 3, 0.0f);
    if (pathMode) {
        std::vector<float> sums(pixelCount * 4);
        std::vector<float> sumsSq(pixelCount * 4);
        CopyRows(rbAccum, planAccum, height_, [&](std::uint32_t y, const std::uint8_t* row) {
            std::memcpy(sums.data() + static_cast<std::size_t>(y) * width_ * 4, row, static_cast<std::size_t>(width_) * 16);
        });
        CopyRows(rbAccumSq, planAccum, height_, [&](std::uint32_t y, const std::uint8_t* row) {
            std::memcpy(sumsSq.data() + static_cast<std::size_t>(y) * width_ * 4, row, static_cast<std::size_t>(width_) * 16);
        });
        ImageRgbF32& mean = denoised ? out.rawMean : out.linear;
        ImageRgbF32& se = denoised ? out.rawStandardError : out.standardError;
        mean.width = width_;
        mean.height = height_;
        mean.pixels.resize(pixelCount * 3);
        se.width = width_;
        se.height = height_;
        se.pixels.assign(pixelCount * 3, 0.0f);
        for (std::size_t i = 0; i < pixelCount; ++i) {
            const double n = sums[i * 4 + 3];
            for (int c = 0; c < 3; ++c) {
                const double sum = sums[i * 4 + c];
                const double sq = sumsSq[i * 4 + c];
                const double m = n > 0 ? sum / n : 0.0;
                mean.pixels[i * 3 + c] = static_cast<float>(m);  // Exact mean from the sums.
                if (n >= 2) {
                    const double variance = std::max(0.0, (sq / n - m * m) * n / (n - 1.0));
                    se.pixels[i * 3 + c] = static_cast<float>(std::sqrt(variance / n));
                }
            }
        }
    }
    if (denoised) {
        out.motion.resize(pixelCount * 3);
        CopyRows(rbMotion, planAccum, height_, [&](std::uint32_t y, const std::uint8_t* rowBytes) {
            const auto* row = reinterpret_cast<const float*>(rowBytes);
            float* dst = out.motion.data() + static_cast<std::size_t>(y) * width_ * 3;
            for (std::uint32_t x = 0; x < width_; ++x) {
                dst[x * 3 + 0] = row[x * 4 + 0];
                dst[x * 3 + 1] = row[x * 4 + 1];
                dst[x * 3 + 2] = row[x * 4 + 2];
            }
        });
        out.historyLength.assign(pixelCount * 2, 0.0f);
        CopyRows(rbDiffOut, planAccum, height_, [&](std::uint32_t y, const std::uint8_t* rowBytes) {
            const auto* row = reinterpret_cast<const float*>(rowBytes);
            for (std::uint32_t x = 0; x < width_; ++x) out.historyLength[(static_cast<std::size_t>(y) * width_ + x) * 2 + 0] = row[x * 4 + 3];
        });
        CopyRows(rbSpecOut, planAccum, height_, [&](std::uint32_t y, const std::uint8_t* rowBytes) {
            const auto* row = reinterpret_cast<const float*>(rowBytes);
            for (std::uint32_t x = 0; x < width_; ++x) out.historyLength[(static_cast<std::size_t>(y) * width_ + x) * 2 + 1] = row[x * 4 + 3];
        });
        out.viewZ.resize(pixelCount);
        CopyRows(rbViewZ, planViewZ, height_, [&](std::uint32_t y, const std::uint8_t* rowBytes) {
            std::memcpy(out.viewZ.data() + static_cast<std::size_t>(y) * width_, rowBytes, static_cast<std::size_t>(width_) * sizeof(float));
        });
    }
    return out;
}

StatsCounters Renderer::ReadStats() {
    if (frameOpen_) {
        throw Error("Renderer::ReadStats called while a frame is being recorded");
    }
    queue_.WaitIdle();
    gfx::GpuBuffer readback = gfx::GpuBuffer::CreateReadback(device_, L"Stats Readback", statsBuffer_.Size());
    LC_CHECK_HR(utilityAllocator_->Reset());
    LC_CHECK_HR(utilityList_->Reset(utilityAllocator_.Get(), nullptr));
    utilityList_->CopyResource(readback.Get(), statsBuffer_.Get());  // COMMON -> COPY_SOURCE by promotion.
    LC_CHECK_HR(utilityList_->Close());
    queue_.Execute(utilityList_.Get());
    queue_.WaitIdle();
    std::uint32_t values[gpu::kStatsCount] = {};
    std::memcpy(values, readback.Map(), sizeof(values));
    readback.Unmap();
    StatsCounters s;
    s.nan = values[gpu::kStatsNan];
    s.inf = values[gpu::kStatsInf];
    s.negative = values[gpu::kStatsNegative];
    s.zeroPdf = values[gpu::kStatsZeroPdf];
    return s;
}

std::vector<LayoutProbeEntry> Renderer::RunLayoutProbe() {
    if (frameOpen_) {
        throw Error("Renderer::RunLayoutProbe called while a frame is being recorded");
    }
    queue_.WaitIdle();

    // Known input data. Every probed field has a distinct value so a wrong offset cannot pass.
    gpu::FrameConstants c{};
    for (int r = 0; r < 4; ++r) {
        for (int k = 0; k < 4; ++k) {
            c.viewToWorld.m[r][k] = 100.0f + static_cast<float>(r * 10 + k) + 0.5f;
            c.worldToView.m[r][k] = 200.0f + static_cast<float>(r * 10 + k) + 0.25f;
            c.viewToClip.m[r][k] = 300.0f + static_cast<float>(r * 10 + k) + 0.125f;
        }
    }
    c.cameraPosition[0] = 1.5f;
    c.cameraPosition[1] = 2.5f;
    c.cameraPosition[2] = 3.5f;
    c.tanHalfFovY = 0.75f;
    c.renderSize[0] = 640;
    c.renderSize[1] = 360;
    c.invRenderSize[0] = 1.0f / 640.0f;
    c.invRenderSize[1] = 1.0f / 360.0f;
    c.frameIndex = 42;
    c.sampleIndex = 7;
    c.viewMode = 3;
    c.instanceCount = 2;
    c.rayTMin = 0.01f;
    c.rayTMax = 123.0f;
    c.aspectRatio = 1.777f;
    c.seed = 0xBEEF;

    gpu::InstanceRecord instances[2]{};
    instances[1].objectToWorldRow[0] = {1.0f, 2.0f, 3.0f, 4.0f};
    instances[1].objectToWorldRow[1] = {5.0f, 6.0f, 7.0f, 8.0f};
    instances[1].objectToWorldRow[2] = {9.0f, 10.0f, 11.0f, 12.0f};
    instances[1].prevObjectToWorldRow[0] = {13.0f, 14.0f, 15.0f, 16.0f};
    instances[1].prevObjectToWorldRow[1] = {17.0f, 18.5f, 19.0f, 20.0f};
    instances[1].prevObjectToWorldRow[2] = {21.0f, 22.0f, 23.0f, 24.0f};
    instances[1].meshIndex = 5;
    instances[1].materialIndex = 6;
    instances[1].stableId = 77;
    instances[1].emitterIndex = 9;

    gpu::MeshRecord meshes[2]{};
    meshes[1] = {11, 22, 33, 44};

    math::Vec3 positions[3] = {{0, 0, 0}, {1, 1, 1}, {7.0f, 8.5f, 9.0f}};
    std::uint32_t indices[5] = {0, 1, 2, 3, 0xABCD};
    gpu::IntegratorConstants ic{};
    gpu::GuideConstants g{};
    g.jitterPixels[0] = 0.125f;
    g.jitterPixels[1] = -0.25f;
    g.outputSize[0] = 1919;
    g.outputSize[1] = 1079;
    g.hitDistParams[0] = 1.5f;
    g.hitDistParams[1] = 2.5f;
    g.hitDistParams[2] = 3.5f;
    g.denoisingRange = 321.0f;
    g.flags = 0xA5;
    g.historyFrames = 9;
    g.exposure = 4.5f;
    g.viewMode = 13;

    gfx::UploadArena arena(device_, 64 * 1024, L"Layout Probe Arena");
    auto upload = [&arena](std::span<const std::uint8_t> bytes) {
        const gfx::UploadAllocation a = arena.Allocate(bytes.size());
        std::memcpy(a.cpu, bytes.data(), bytes.size());
        return a.gpu;
    };
    const D3D12_GPU_VIRTUAL_ADDRESS cbVa = upload(AsBytes(c));
    const D3D12_GPU_VIRTUAL_ADDRESS icVa = upload(AsBytes(ic));
    const D3D12_GPU_VIRTUAL_ADDRESS gVa = upload(AsBytes(g));
    const D3D12_GPU_VIRTUAL_ADDRESS instVa = upload(AsBytes(instances));
    const D3D12_GPU_VIRTUAL_ADDRESS meshVa = upload(AsBytes(meshes));
    const D3D12_GPU_VIRTUAL_ADDRESS posVa = upload(AsBytes(positions));
    const D3D12_GPU_VIRTUAL_ADDRESS idxVa = upload(AsBytes(indices));

    gfx::GpuBuffer readback = gfx::GpuBuffer::CreateReadback(device_, L"Layout Probe Readback", probeBuffer_.Size());

    LC_CHECK_HR(utilityAllocator_->Reset());
    LC_CHECK_HR(utilityList_->Reset(utilityAllocator_.Get(), nullptr));
    // Unused root SRVs (TLAS, materials, emitters) get a valid buffer address so every parameter is set.
    BindCommon(utilityList_.Get(), cbVa, icVa, gVa, instVa, instVa, meshVa, posVa, idxVa, instVa, instVa, instVa);
    utilityList_->SetPipelineState(layoutProbe_->Get());
    utilityList_->Dispatch(1, 1, 1);
    const D3D12_RESOURCE_BARRIER toCopy = gfx::TransitionBarrier(probeBuffer_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    utilityList_->ResourceBarrier(1, &toCopy);
    utilityList_->CopyResource(readback.Get(), probeBuffer_.Get());
    LC_CHECK_HR(utilityList_->Close());
    queue_.Execute(utilityList_.Get());
    queue_.WaitIdle();

    std::uint32_t actual[gpu::kLayoutProbeCount] = {};
    std::memcpy(actual, readback.Map(), sizeof(actual));
    readback.Unmap();

    const std::pair<const char*, std::uint32_t> expected[gpu::kLayoutProbeCount] = {
        {"viewToWorld[0][3]", AsUint(c.viewToWorld.m[0][3])},
        {"viewToWorld[1][3]", AsUint(c.viewToWorld.m[1][3])},
        {"viewToWorld[2][3]", AsUint(c.viewToWorld.m[2][3])},
        {"viewToWorld[3][3]", AsUint(c.viewToWorld.m[3][3])},
        {"worldToView[1][2]", AsUint(c.worldToView.m[1][2])},
        {"viewToClip[2][3]", AsUint(c.viewToClip.m[2][3])},
        {"cameraPosition.z", AsUint(c.cameraPosition[2])},
        {"tanHalfFovY", AsUint(c.tanHalfFovY)},
        {"renderSize.x", c.renderSize[0]},
        {"renderSize.y", c.renderSize[1]},
        {"frameIndex", c.frameIndex},
        {"viewMode", c.viewMode},
        {"instanceCount", c.instanceCount},
        {"rayTMax", AsUint(c.rayTMax)},
        {"aspectRatio", AsUint(c.aspectRatio)},
        {"seed", c.seed},
        {"instances[1].objectToWorldRow[0].w", AsUint(instances[1].objectToWorldRow[0].w)},
        {"instances[1].objectToWorldRow[2].w", AsUint(instances[1].objectToWorldRow[2].w)},
        {"instances[1].prevObjectToWorldRow[1].y", AsUint(instances[1].prevObjectToWorldRow[1].y)},
        {"instances[1].meshIndex", instances[1].meshIndex},
        {"instances[1].materialIndex", instances[1].materialIndex},
        {"instances[1].stableId", instances[1].stableId},
        {"instances[1].emitterIndex", instances[1].emitterIndex},
        {"meshes[1].firstVertex", meshes[1].firstVertex},
        {"meshes[1].firstIndex", meshes[1].firstIndex},
        {"meshes[1].vertexCount", meshes[1].vertexCount},
        {"meshes[1].indexCount", meshes[1].indexCount},
        {"positions[2].y", AsUint(positions[2].y)},
        {"indices[4]", indices[4]},
        {"guide.jitterPixels.y", AsUint(g.jitterPixels[1])},
        {"guide.outputSize.x", g.outputSize[0]},
        {"guide.hitDistParams.z", AsUint(g.hitDistParams[2])},
        {"guide.flags", g.flags},
        {"guide.viewMode", g.viewMode},
        {"sentinel", gpu::kLayoutProbeSentinel},
    };

    std::vector<LayoutProbeEntry> entries;
    for (std::uint32_t i = 0; i < gpu::kLayoutProbeCount; ++i) {
        entries.push_back(LayoutProbeEntry{expected[i].first, expected[i].second, actual[i], expected[i].second == actual[i]});
    }
    return entries;
}

}  // namespace lc
