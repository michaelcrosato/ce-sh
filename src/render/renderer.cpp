#include "render/renderer.h"

#include "core/error.h"
#include "core/log.h"
#include "core/math/camera_math.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/graphics_queue.h"
#include "platform/files.h"

#include <cmath>
#include <cstring>
#include <format>

namespace lc {

namespace {

constexpr std::uint32_t kUavCount = 4;  // u0 display, u1 linear, u2 hit info, u3 layout probe.
constexpr std::uint64_t kArenaBytes = 4ull * 1024 * 1024;
constexpr std::uint32_t kMaxTimersPerFrame = 16;

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

}  // namespace

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
    D3D12_UNORDERED_ACCESS_VIEW_DESC probeUav{};
    probeUav.Format = DXGI_FORMAT_UNKNOWN;
    probeUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    probeUav.Buffer.FirstElement = 0;
    probeUav.Buffer.NumElements = gpu::kLayoutProbeCount;
    probeUav.Buffer.StructureByteStride = sizeof(std::uint32_t);
    probeUav.Buffer.CounterOffsetInBytes = 0;
    probeUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device.Get()->CreateUnorderedAccessView(probeBuffer_.Get(), nullptr, &probeUav, heap_.At(uavTable_ + 3).cpu);

    CreateOutputs(width, height);
    CreateRootSignature();

    const std::filesystem::path shaderDir = files::ExecutableDirectory() / "shaders";
    cameraView_ = std::make_unique<gfx::ComputePipeline>(device, rootSignature_.Get(), shaderDir / "camera_view.cso", L"Camera View CS");
    layoutProbe_ = std::make_unique<gfx::ComputePipeline>(device, rootSignature_.Get(), shaderDir / "layout_probe.cso", L"Layout Probe CS");
}

Renderer::~Renderer() { queue_.WaitIdle(); }

void Renderer::CreateOutputs(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        throw Error(std::format("Renderer::CreateOutputs: zero-sized render target requested ({}x{})", width, height));
    }
    width_ = width;
    height_ = height;
    display_ = gfx::GpuTexture2D::CreateUav(device_, L"Display RGBA8", width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    linear_ = gfx::GpuTexture2D::CreateUav(device_, L"Linear RGBA32F", width, height, DXGI_FORMAT_R32G32B32A32_FLOAT);
    hitInfo_ = gfx::GpuTexture2D::CreateUav(device_, L"Hit Info RGBA32U", width, height, DXGI_FORMAT_R32G32B32A32_UINT);

    const gfx::GpuTexture2D* textures[3] = {&display_, &linear_, &hitInfo_};
    for (std::uint32_t i = 0; i < 3; ++i) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = textures[i]->Format();
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = 0;
        uav.Texture2D.PlaneSlice = 0;
        device_.Get()->CreateUnorderedAccessView(textures[i]->Get(), nullptr, &uav, heap_.At(uavTable_ + i).cpu);
    }
    log::Info("Render outputs created: {}x{} (display RGBA8, linear RGBA32F, hit info RGBA32U)", width, height);
}

void Renderer::CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE1 uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = kUavCount;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 params[7]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;  // b0 FrameConstants
    params[0].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t0 TLAS (rebuilt within the list)
    params[1].Descriptor = {0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE};
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;  // t1 InstanceRecords (per frame)
    params[2].Descriptor = {1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE};
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (std::uint32_t i = 3; i <= 5; ++i) {  // t2 MeshRecords, t3 positions, t4 indices (static geometry)
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor = {i - 1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC};
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;  // u0..u3
    params[6].DescriptorTable = {1, &uavRange};
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
}

void Renderer::Resize(std::uint32_t width, std::uint32_t height) {
    if (frameOpen_) {
        throw Error("Renderer::Resize called while a frame is being recorded");
    }
    if (width == width_ && height == height_) {
        return;
    }
    queue_.WaitIdle();
    CreateOutputs(width, height);
}

gpu::FrameConstants Renderer::BuildFrameConstants(const RenderSnapshot& snapshot) const {
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
    c.sampleIndex = 0;
    c.viewMode = static_cast<std::uint32_t>(snapshot.view);
    c.instanceCount = sceneGpu_ ? sceneGpu_->InstanceCount() : 0;
    c.rayTMin = 0.0f;
    c.rayTMax = cam.farZ;
    c.aspectRatio = aspect;
    c.seed = 0;
    return c;
}

void Renderer::BindCommon(ID3D12GraphicsCommandList4* list, D3D12_GPU_VIRTUAL_ADDRESS constants, D3D12_GPU_VIRTUAL_ADDRESS tlas,
                          D3D12_GPU_VIRTUAL_ADDRESS instances, D3D12_GPU_VIRTUAL_ADDRESS meshes, D3D12_GPU_VIRTUAL_ADDRESS positions,
                          D3D12_GPU_VIRTUAL_ADDRESS indices) {
    ID3D12DescriptorHeap* heaps[] = {heap_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(rootSignature_.Get());
    list->SetComputeRootConstantBufferView(0, constants);
    list->SetComputeRootShaderResourceView(1, tlas);
    list->SetComputeRootShaderResourceView(2, instances);
    list->SetComputeRootShaderResourceView(3, meshes);
    list->SetComputeRootShaderResourceView(4, positions);
    list->SetComputeRootShaderResourceView(5, indices);
    list->SetComputeRootDescriptorTable(6, heap_.At(uavTable_).gpu);
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
    FrameContext& frame = frames_[frameSlot_];

    const std::uint32_t tlasTimer = timers_.Begin(list_.Get(), "tlas_update");
    sceneGpu_->UpdateInstances(*snapshot.scene, *frame.arena, list_.Get());
    timers_.End(list_.Get(), tlasTimer);

    const gpu::FrameConstants constants = BuildFrameConstants(snapshot);
    const gfx::UploadAllocation cb = frame.arena->Allocate(sizeof(constants));
    std::memcpy(cb.cpu, &constants, sizeof(constants));

    BindCommon(list_.Get(), cb.gpu, sceneGpu_->TlasAddress(), sceneGpu_->InstanceRecordsAddress(), sceneGpu_->MeshRecordsAddress(),
               sceneGpu_->PositionsAddress(), sceneGpu_->IndicesAddress());
    list_->SetPipelineState(cameraView_->Get());

    const std::uint32_t traceTimer = timers_.Begin(list_.Get(), "trace");
    list_->Dispatch((width_ + 7) / 8, (height_ + 7) / 8, 1);
    timers_.End(list_.Get(), traceTimer);
}

void Renderer::RecordCopyToBackBuffer(ID3D12Resource* backBuffer, std::uint32_t backBufferWidth, std::uint32_t backBufferHeight) {
    if (!frameOpen_) {
        throw Error("Renderer::RecordCopyToBackBuffer called outside BeginFrame/EndFrame");
    }
    if (backBufferWidth != width_ || backBufferHeight != height_) {
        if (!warnedCopyMismatch_) {
            log::Warn("skipping present copy: render {}x{} vs swap chain {}x{}", width_, height_, backBufferWidth, backBufferHeight);
            warnedCopyMismatch_ = true;
        }
        return;
    }
    warnedCopyMismatch_ = false;
    const std::uint32_t copyTimer = timers_.Begin(list_.Get(), "copy_out");
    display_.Transition(list_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
    const D3D12_RESOURCE_BARRIER toCopy = gfx::TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    list_->ResourceBarrier(1, &toCopy);
    list_->CopyResource(backBuffer, display_.Get());
    const D3D12_RESOURCE_BARRIER toPresent = gfx::TransitionBarrier(backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    list_->ResourceBarrier(1, &toPresent);
    display_.Transition(list_.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    timers_.End(list_.Get(), copyTimer);
}

std::uint64_t Renderer::EndFrame() {
    if (!frameOpen_) {
        throw Error("Renderer::EndFrame called without BeginFrame");
    }
    timers_.End(list_.Get(), frameTimer_);
    timers_.Resolve(list_.Get());
    LC_CHECK_HR(list_->Close());
    queue_.Execute(list_.Get());
    FrameContext& frame = frames_[frameSlot_];
    frame.fenceValue = queue_.Signal();
    frameOpen_ = false;
    ++frameCounter_;
    return frame.fenceValue;
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

    const gfx::ReadbackPlan planDisplay = display_.PlanReadback(device_);
    const gfx::ReadbackPlan planLinear = linear_.PlanReadback(device_);
    const gfx::ReadbackPlan planHit = hitInfo_.PlanReadback(device_);
    gfx::GpuBuffer rbDisplay = gfx::GpuBuffer::CreateReadback(device_, L"Readback Display", planDisplay.totalBytes);
    gfx::GpuBuffer rbLinear = gfx::GpuBuffer::CreateReadback(device_, L"Readback Linear", planLinear.totalBytes);
    gfx::GpuBuffer rbHit = gfx::GpuBuffer::CreateReadback(device_, L"Readback Hit Info", planHit.totalBytes);
    display_.RecordCopyToReadback(utilityList_.Get(), rbDisplay, planDisplay);
    linear_.RecordCopyToReadback(utilityList_.Get(), rbLinear, planLinear);
    hitInfo_.RecordCopyToReadback(utilityList_.Get(), rbHit, planHit);
    LC_CHECK_HR(utilityList_->Close());
    queue_.Execute(utilityList_.Get());
    queue_.WaitIdle();

    CaptureImages out;
    out.width = width_;
    out.height = height_;

    out.display.width = width_;
    out.display.height = height_;
    out.display.pixels.resize(static_cast<std::size_t>(width_) * height_ * 4);
    {
        const auto* src = static_cast<const std::uint8_t*>(rbDisplay.Map());
        for (std::uint32_t y = 0; y < height_; ++y) {
            std::memcpy(out.display.pixels.data() + static_cast<std::size_t>(y) * width_ * 4,
                        src + planDisplay.footprint.Offset + static_cast<std::size_t>(y) * planDisplay.footprint.Footprint.RowPitch,
                        static_cast<std::size_t>(width_) * 4);
        }
        rbDisplay.Unmap();
    }

    out.linear.width = width_;
    out.linear.height = height_;
    out.linear.pixels.resize(static_cast<std::size_t>(width_) * height_ * 3);
    {
        const auto* src = static_cast<const std::uint8_t*>(rbLinear.Map());
        for (std::uint32_t y = 0; y < height_; ++y) {
            const auto* row = reinterpret_cast<const float*>(src + planLinear.footprint.Offset + static_cast<std::size_t>(y) * planLinear.footprint.Footprint.RowPitch);
            float* dst = out.linear.pixels.data() + static_cast<std::size_t>(y) * width_ * 3;
            for (std::uint32_t x = 0; x < width_; ++x) {
                dst[x * 3 + 0] = row[x * 4 + 0];
                dst[x * 3 + 1] = row[x * 4 + 1];
                dst[x * 3 + 2] = row[x * 4 + 2];
            }
        }
        rbLinear.Unmap();
    }

    out.hitInfo.resize(static_cast<std::size_t>(width_) * height_);
    {
        const auto* src = static_cast<const std::uint8_t*>(rbHit.Map());
        for (std::uint32_t y = 0; y < height_; ++y) {
            std::memcpy(out.hitInfo.data() + static_cast<std::size_t>(y) * width_,
                        src + planHit.footprint.Offset + static_cast<std::size_t>(y) * planHit.footprint.Footprint.RowPitch,
                        static_cast<std::size_t>(width_) * sizeof(gpu::HitInfoTexel));
        }
        rbHit.Unmap();
    }
    return out;
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
    instances[1].transformRevision = 9;

    gpu::MeshRecord meshes[2]{};
    meshes[1] = {11, 22, 33, 44};

    math::Vec3 positions[3] = {{0, 0, 0}, {1, 1, 1}, {7.0f, 8.5f, 9.0f}};
    std::uint32_t indices[5] = {0, 1, 2, 3, 0xABCD};

    gfx::UploadArena arena(device_, 64 * 1024, L"Layout Probe Arena");
    auto upload = [&arena](std::span<const std::uint8_t> bytes) {
        const gfx::UploadAllocation a = arena.Allocate(bytes.size());
        std::memcpy(a.cpu, bytes.data(), bytes.size());
        return a.gpu;
    };
    const D3D12_GPU_VIRTUAL_ADDRESS cbVa = upload(AsBytes(c));
    const D3D12_GPU_VIRTUAL_ADDRESS instVa = upload(AsBytes(instances));
    const D3D12_GPU_VIRTUAL_ADDRESS meshVa = upload(AsBytes(meshes));
    const D3D12_GPU_VIRTUAL_ADDRESS posVa = upload(AsBytes(positions));
    const D3D12_GPU_VIRTUAL_ADDRESS idxVa = upload(AsBytes(indices));

    gfx::GpuBuffer readback = gfx::GpuBuffer::CreateReadback(device_, L"Layout Probe Readback", probeBuffer_.Size());

    LC_CHECK_HR(utilityAllocator_->Reset());
    LC_CHECK_HR(utilityList_->Reset(utilityAllocator_.Get(), nullptr));
    // t0 (TLAS) is not read by the probe shader; bind a valid buffer address so every root parameter is set.
    BindCommon(utilityList_.Get(), cbVa, instVa, instVa, meshVa, posVa, idxVa);
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
        {"instances[1].transformRevision", instances[1].transformRevision},
        {"meshes[1].firstVertex", meshes[1].firstVertex},
        {"meshes[1].firstIndex", meshes[1].firstIndex},
        {"meshes[1].vertexCount", meshes[1].vertexCount},
        {"meshes[1].indexCount", meshes[1].indexCount},
        {"positions[2].y", AsUint(positions[2].y)},
        {"indices[4]", indices[4]},
        {"sentinel", gpu::kLayoutProbeSentinel},
    };

    std::vector<LayoutProbeEntry> entries;
    for (std::uint32_t i = 0; i < gpu::kLayoutProbeCount; ++i) {
        entries.push_back(LayoutProbeEntry{expected[i].first, expected[i].second, actual[i], expected[i].second == actual[i]});
    }
    return entries;
}

}  // namespace lc
