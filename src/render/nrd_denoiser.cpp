#include "render/nrd_denoiser.h"

#include "core/error.h"
#include "core/log.h"
#include "graphics/d3d12/device.h"

#include <cstring>
#include <format>

namespace lc {

namespace {

constexpr std::uint32_t kRootConstants = 0;  // Root CBV.
constexpr std::uint32_t kRootTable = 1;      // SRV range then UAV range.

std::uint32_t DivideUp(std::uint32_t value, std::uint32_t divisor) {
    return divisor == 0 ? value : (value + divisor - 1) / divisor;
}

const char* ResourceName(const nrd::ResourceDesc& r) {
    if (r.type == nrd::ResourceType::PERMANENT_POOL) return "PERMANENT_POOL";
    if (r.type == nrd::ResourceType::TRANSIENT_POOL) return "TRANSIENT_POOL";
    return nrd::GetResourceTypeString(r.type);
}

}  // namespace

DXGI_FORMAT NrdFormatToDxgi(nrd::Format format) {
    switch (format) {
        case nrd::Format::R8_UNORM: return DXGI_FORMAT_R8_UNORM;
        case nrd::Format::R8_SNORM: return DXGI_FORMAT_R8_SNORM;
        case nrd::Format::R8_UINT: return DXGI_FORMAT_R8_UINT;
        case nrd::Format::R8_SINT: return DXGI_FORMAT_R8_SINT;
        case nrd::Format::RG8_UNORM: return DXGI_FORMAT_R8G8_UNORM;
        case nrd::Format::RG8_SNORM: return DXGI_FORMAT_R8G8_SNORM;
        case nrd::Format::RG8_UINT: return DXGI_FORMAT_R8G8_UINT;
        case nrd::Format::RG8_SINT: return DXGI_FORMAT_R8G8_SINT;
        case nrd::Format::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case nrd::Format::RGBA8_SNORM: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case nrd::Format::RGBA8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
        case nrd::Format::RGBA8_SINT: return DXGI_FORMAT_R8G8B8A8_SINT;
        case nrd::Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case nrd::Format::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
        case nrd::Format::R16_SNORM: return DXGI_FORMAT_R16_SNORM;
        case nrd::Format::R16_UINT: return DXGI_FORMAT_R16_UINT;
        case nrd::Format::R16_SINT: return DXGI_FORMAT_R16_SINT;
        case nrd::Format::R16_SFLOAT: return DXGI_FORMAT_R16_FLOAT;
        case nrd::Format::RG16_UNORM: return DXGI_FORMAT_R16G16_UNORM;
        case nrd::Format::RG16_SNORM: return DXGI_FORMAT_R16G16_SNORM;
        case nrd::Format::RG16_UINT: return DXGI_FORMAT_R16G16_UINT;
        case nrd::Format::RG16_SINT: return DXGI_FORMAT_R16G16_SINT;
        case nrd::Format::RG16_SFLOAT: return DXGI_FORMAT_R16G16_FLOAT;
        case nrd::Format::RGBA16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case nrd::Format::RGBA16_SNORM: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case nrd::Format::RGBA16_UINT: return DXGI_FORMAT_R16G16B16A16_UINT;
        case nrd::Format::RGBA16_SINT: return DXGI_FORMAT_R16G16B16A16_SINT;
        case nrd::Format::RGBA16_SFLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case nrd::Format::R32_UINT: return DXGI_FORMAT_R32_UINT;
        case nrd::Format::R32_SINT: return DXGI_FORMAT_R32_SINT;
        case nrd::Format::R32_SFLOAT: return DXGI_FORMAT_R32_FLOAT;
        case nrd::Format::RG32_UINT: return DXGI_FORMAT_R32G32_UINT;
        case nrd::Format::RG32_SINT: return DXGI_FORMAT_R32G32_SINT;
        case nrd::Format::RG32_SFLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case nrd::Format::RGBA32_UINT: return DXGI_FORMAT_R32G32B32A32_UINT;
        case nrd::Format::RGBA32_SINT: return DXGI_FORMAT_R32G32B32A32_SINT;
        case nrd::Format::RGBA32_SFLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case nrd::Format::R10_G10_B10_A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case nrd::Format::R10_G10_B10_A2_UINT: return DXGI_FORMAT_R10G10B10A2_UINT;
        case nrd::Format::R11_G11_B10_UFLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
        case nrd::Format::R9_G9_B9_E5_UFLOAT: return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        case nrd::Format::RGB32_UINT:
        case nrd::Format::RGB32_SINT:
        case nrd::Format::RGB32_SFLOAT:
        case nrd::Format::MAX_NUM:
            break;
    }
    throw Error(std::format("NRD requested texture format {} which D3D12 cannot use as a UAV texture", static_cast<std::uint32_t>(format)));
}

std::string NrdDenoiser::VersionText() {
    const nrd::LibraryDesc& lib = *nrd::GetLibraryDesc();
    return std::format("{}.{}.{}", lib.versionMajor, lib.versionMinor, lib.versionBuild);
}

NrdDenoiser::NrdDenoiser(gfx::Device& device, std::uint32_t width, std::uint32_t height)
    : device_(device), width_(width), height_(height) {
    if (width == 0 || height == 0) {
        throw Error(std::format("NrdDenoiser: zero-sized resolution {}x{}", width, height));
    }
    const nrd::LibraryDesc& lib = *nrd::GetLibraryDesc();
    if (lib.versionMajor != NRD_VERSION_MAJOR || lib.versionMinor != NRD_VERSION_MINOR) {
        throw Error(std::format("NRD library {}.{} does not match the headers {}.{}", lib.versionMajor, lib.versionMinor, NRD_VERSION_MAJOR,
                                NRD_VERSION_MINOR));
    }
    // The shaders pack normals and roughness for exactly this encoding (cmake/LcNrd.cmake pins it).
    if (lib.normalEncoding != nrd::NormalEncoding::R10_G10_B10_A2_UNORM || lib.roughnessEncoding != nrd::RoughnessEncoding::LINEAR) {
        throw Error("NRD was built with a normal or roughness encoding other than R10G10B10A2 / linear; rebuild with cmake/LcNrd.cmake settings");
    }
    bool supported = false;
    for (std::uint32_t i = 0; i < lib.supportedDenoisersNum; ++i) {
        supported = supported || lib.supportedDenoisers[i] == nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR;
    }
    if (!supported) {
        throw Error("this NRD build does not include REBLUR_DIFFUSE_SPECULAR");
    }

    const nrd::DenoiserDesc denoisers[] = {{identifier_, nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR}};
    nrd::InstanceCreationDesc create{};
    create.denoisers = denoisers;
    create.denoisersNum = 1;
    if (nrd::CreateInstance(create, instance_) != nrd::Result::SUCCESS || instance_ == nullptr) {
        throw Error("nrd::CreateInstance failed");
    }
    desc_ = nrd::GetInstanceDesc(*instance_);
    if (desc_ == nullptr) {
        throw Error("nrd::GetInstanceDesc returned null");
    }

    texturesPerSet_ = desc_->descriptorPoolDesc.perSetTexturesMaxNum;
    storagesPerSet_ = desc_->descriptorPoolDesc.perSetStorageTexturesMaxNum;
    descriptorsPerSet_ = texturesPerSet_ + storagesPerSet_;
    setsPerFrame_ = desc_->descriptorPoolDesc.setsMaxNum;
    if (descriptorsPerSet_ == 0 || setsPerFrame_ == 0) {
        throw Error("NRD instance describes no descriptor sets");
    }

    CreateRootSignature();
    CreatePipelines();
    CreatePools();
    CreateDescriptorHeap();

    log::Info("NRD {} REBLUR_DIFFUSE_SPECULAR at {}x{}: {} pipelines, {} pool textures ({:.1f} MiB), {} sets per frame of {} + {} descriptors, "
              "constants up to {} bytes",
              VersionText(), width_, height_, pipelines_.size(), pool_.size(), static_cast<double>(poolBytes_) / (1024.0 * 1024.0), setsPerFrame_,
              texturesPerSet_, storagesPerSet_, desc_->constantBufferMaxDataSize);
}

NrdDenoiser::~NrdDenoiser() {
    if (instance_ != nullptr) {
        nrd::DestroyInstance(*instance_);
        instance_ = nullptr;
    }
}

void NrdDenoiser::CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE1 ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = texturesPerSet_;
    ranges[0].BaseShaderRegister = desc_->resourcesBaseRegisterIndex;
    ranges[0].RegisterSpace = desc_->resourcesSpaceIndex;
    ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = storagesPerSet_;
    ranges[1].BaseShaderRegister = desc_->resourcesBaseRegisterIndex;
    ranges[1].RegisterSpace = desc_->resourcesSpaceIndex;
    ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
    ranges[1].OffsetInDescriptorsFromTableStart = texturesPerSet_;

    D3D12_ROOT_PARAMETER1 params[2]{};
    params[kRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kRootConstants].Descriptor.ShaderRegister = desc_->constantBufferRegisterIndex;
    params[kRootConstants].Descriptor.RegisterSpace = desc_->constantBufferAndSamplersSpaceIndex;
    params[kRootConstants].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
    params[kRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[kRootTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootTable].DescriptorTable.NumDescriptorRanges = 2;
    params[kRootTable].DescriptorTable.pDescriptorRanges = ranges;
    params[kRootTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
    for (std::uint32_t i = 0; i < desc_->samplersNum; ++i) {
        D3D12_STATIC_SAMPLER_DESC s{};
        const bool nearest = desc_->samplers[i] == nrd::Sampler::NEAREST_CLAMP;
        s.Filter = nearest ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ShaderRegister = desc_->samplersBaseRegisterIndex + i;
        s.RegisterSpace = desc_->constantBufferAndSamplersSpaceIndex;
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        samplers.push_back(s);
    }

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
    desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    desc.Desc_1_1.NumParameters = 2;
    desc.Desc_1_1.pParameters = params;
    desc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(samplers.size());
    desc.Desc_1_1.pStaticSamplers = samplers.data();
    desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rootSignature_ = gfx::CreateRootSignature(device_, desc, L"NRD Root Signature");
}

void NrdDenoiser::CreatePipelines() {
    pipelines_.reserve(desc_->pipelinesNum);
    for (std::uint32_t i = 0; i < desc_->pipelinesNum; ++i) {
        const nrd::PipelineDesc& p = desc_->pipelines[i];
        const nrd::ComputeShaderDesc& cs = p.computeShaderDXIL;
        if (cs.bytecode == nullptr || cs.size == 0) {
            throw Error(std::format("NRD pipeline {} ({}) has no DXIL bytecode; NRD must be built with NRD_EMBEDS_DXIL_SHADERS", i, p.shaderIdentifier));
        }
        if (p.resourceRangesNum > 2) {
            throw Error(std::format("NRD pipeline {} declares {} resource ranges; this backend expects at most 2", i, p.resourceRangesNum));
        }
        const std::span<const std::uint8_t> bytes(static_cast<const std::uint8_t*>(cs.bytecode), static_cast<std::size_t>(cs.size));
        pipelines_.push_back(std::make_unique<gfx::ComputePipeline>(device_, rootSignature_.Get(), bytes, std::format(L"NRD Pipeline {}", i),
                                                                    p.shaderIdentifier));
    }
}

void NrdDenoiser::CreatePools() {
    const std::uint32_t total = desc_->permanentPoolSize + desc_->transientPoolSize;
    pool_.reserve(total);
    for (std::uint32_t i = 0; i < total; ++i) {
        const bool permanent = i < desc_->permanentPoolSize;
        const nrd::TextureDesc& t = permanent ? desc_->permanentPool[i] : desc_->transientPool[i - desc_->permanentPoolSize];
        const std::uint32_t w = DivideUp(width_, t.downsampleFactor);
        const std::uint32_t h = DivideUp(height_, t.downsampleFactor);
        const std::wstring name = permanent ? std::format(L"NRD Permanent {}", i) : std::format(L"NRD Transient {}", i - desc_->permanentPoolSize);
        pool_.push_back(gfx::GpuTexture2D::CreateUav(device_, name, w, h, NrdFormatToDxgi(t.format)));
        const D3D12_RESOURCE_DESC rd = pool_.back().Get()->GetDesc();
        poolBytes_ += device_.Get()->GetResourceAllocationInfo(0, 1, &rd).SizeInBytes;
    }
}

void NrdDenoiser::CreateDescriptorHeap() {
    const std::uint32_t capacity = gfx::kFramesInFlight * setsPerFrame_ * descriptorsPerSet_;
    heap_ = std::make_unique<gfx::DescriptorHeap>(device_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, capacity, true, L"NRD Descriptor Heap");
    heap_->AllocateRange(capacity);
    // Every slot starts as a null view so a partially filled table never references garbage.
    D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
    nullSrv.Format = DXGI_FORMAT_R8_UNORM;
    nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    nullSrv.Texture2D.MipLevels = 1;
    D3D12_UNORDERED_ACCESS_VIEW_DESC nullUav{};
    nullUav.Format = DXGI_FORMAT_R8_UNORM;
    nullUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (std::uint32_t set = 0; set < gfx::kFramesInFlight * setsPerFrame_; ++set) {
        const std::uint32_t base = set * descriptorsPerSet_;
        for (std::uint32_t i = 0; i < texturesPerSet_; ++i) {
            device_.Get()->CreateShaderResourceView(nullptr, &nullSrv, heap_->At(base + i).cpu);
        }
        for (std::uint32_t i = 0; i < storagesPerSet_; ++i) {
            device_.Get()->CreateUnorderedAccessView(nullptr, nullptr, &nullUav, heap_->At(base + texturesPerSet_ + i).cpu);
        }
    }
}

void NrdDenoiser::SetCommonSettings(const nrd::CommonSettings& settings) {
    if (nrd::SetCommonSettings(*instance_, settings) != nrd::Result::SUCCESS) {
        throw Error("nrd::SetCommonSettings rejected the settings (matrices, sizes, or jitter out of range)");
    }
}

void NrdDenoiser::SetReblurSettings(const nrd::ReblurSettings& settings) {
    if (nrd::SetDenoiserSettings(*instance_, identifier_, &settings) != nrd::Result::SUCCESS) {
        throw Error("nrd::SetDenoiserSettings rejected the REBLUR settings");
    }
}

gfx::GpuTexture2D* NrdDenoiser::Resolve(const nrd::ResourceDesc& r, const NrdInputs& in) const {
    switch (r.type) {
        case nrd::ResourceType::PERMANENT_POOL:
            if (r.indexInPool >= desc_->permanentPoolSize) throw Error("NRD permanent pool index out of range");
            return const_cast<gfx::GpuTexture2D*>(&pool_[r.indexInPool]);
        case nrd::ResourceType::TRANSIENT_POOL:
            if (r.indexInPool >= desc_->transientPoolSize) throw Error("NRD transient pool index out of range");
            return const_cast<gfx::GpuTexture2D*>(&pool_[desc_->permanentPoolSize + r.indexInPool]);
        case nrd::ResourceType::IN_MV: return in.motion;
        case nrd::ResourceType::IN_NORMAL_ROUGHNESS: return in.normalRoughness;
        case nrd::ResourceType::IN_VIEWZ: return in.viewZ;
        case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST: return in.diffIn;
        case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST: return in.specIn;
        case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return in.diffOut;
        case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: return in.specOut;
        case nrd::ResourceType::OUT_VALIDATION: return in.validation;
        default: break;
    }
    throw Error(std::format("NRD asked for resource '{}' which this integration does not provide", ResourceName(r)));
}

void NrdDenoiser::Record(ID3D12GraphicsCommandList4* list, gfx::UploadArena& arena, std::uint32_t frameSlot, const NrdInputs& inputs) {
    if (frameSlot >= gfx::kFramesInFlight) {
        throw Error("NrdDenoiser::Record: frame slot out of range");
    }
    const nrd::DispatchDesc* dispatches = nullptr;
    std::uint32_t count = 0;
    if (nrd::GetComputeDispatches(*instance_, &identifier_, 1, dispatches, count) != nrd::Result::SUCCESS) {
        throw Error("nrd::GetComputeDispatches failed (were the common settings set this frame?)");
    }
    if (count > setsPerFrame_) {
        throw Error(std::format("NRD produced {} dispatches but described at most {} descriptor sets", count, setsPerFrame_));
    }

    ID3D12DescriptorHeap* heaps[] = {heap_->Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(rootSignature_.Get());

    gfx::GpuTexture2D* touched[16] = {inputs.motion, inputs.normalRoughness, inputs.viewZ, inputs.diffIn, inputs.specIn,
                                      inputs.diffOut, inputs.specOut, inputs.validation};
    D3D12_GPU_VIRTUAL_ADDRESS constants = 0;  // Reused only within this Record call (never across frames/arenas).
    for (std::uint32_t d = 0; d < count; ++d) {
        const nrd::DispatchDesc& dispatch = dispatches[d];
        if (dispatch.pipelineIndex >= pipelines_.size()) {
            throw Error("NRD dispatch references an unknown pipeline");
        }
        const nrd::PipelineDesc& pipeline = desc_->pipelines[dispatch.pipelineIndex];
        const std::uint32_t setBase = (frameSlot * setsPerFrame_ + d) * descriptorsPerSet_;

        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        std::uint32_t n = 0;
        for (std::uint32_t r = 0; r < pipeline.resourceRangesNum; ++r) {
            const nrd::ResourceRangeDesc& range = pipeline.resourceRanges[r];
            const bool storage = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE;
            if ((storage ? storagesPerSet_ : texturesPerSet_) < range.descriptorsNum) {
                throw Error("NRD pipeline range exceeds the per-set descriptor maximum");
            }
            for (std::uint32_t j = 0; j < range.descriptorsNum; ++j, ++n) {
                if (n >= dispatch.resourcesNum) {
                    throw Error("NRD dispatch lists fewer resources than its pipeline ranges");
                }
                const nrd::ResourceDesc& rd = dispatch.resources[n];
                gfx::GpuTexture2D* tex = Resolve(rd, inputs);
                if (tex == nullptr || !tex->IsValid()) {
                    throw Error(std::format("NRD resource '{}' is not bound", ResourceName(rd)));
                }
                const D3D12_RESOURCE_STATES want = storage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                if (tex->State() != want) {
                    barriers.push_back(gfx::TransitionBarrier(tex->Get(), tex->State(), want));
                    tex->SetState(want);
                } else if (storage) {
                    barriers.push_back(gfx::UavBarrier(tex->Get()));  // Consecutive UAV writes.
                }
                const std::uint32_t slot = setBase + (storage ? texturesPerSet_ + j : j);
                if (storage) {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                    uav.Format = tex->Format();
                    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                    device_.Get()->CreateUnorderedAccessView(tex->Get(), nullptr, &uav, heap_->At(slot).cpu);
                } else {
                    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                    srv.Format = tex->Format();
                    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srv.Texture2D.MipLevels = 1;
                    device_.Get()->CreateShaderResourceView(tex->Get(), &srv, heap_->At(slot).cpu);
                }
            }
        }
        if (n != dispatch.resourcesNum) {
            throw Error("NRD dispatch lists more resources than its pipeline ranges");
        }
        if (!barriers.empty()) {
            list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }

        if (dispatch.constantBufferDataSize != 0 && (constants == 0 || !dispatch.constantBufferDataMatchesPreviousDispatch)) {
            const gfx::UploadAllocation cb = arena.Allocate(dispatch.constantBufferDataSize);
            std::memcpy(cb.cpu, dispatch.constantBufferData, dispatch.constantBufferDataSize);
            constants = cb.gpu;
        }
        if (pipeline.hasConstantData && constants == 0) {
            throw Error("NRD pipeline expects constants but the dispatch provided none");
        }
        if (constants != 0) {
            list->SetComputeRootConstantBufferView(kRootConstants, constants);
        }
        list->SetComputeRootDescriptorTable(kRootTable, heap_->At(setBase).gpu);
        list->SetPipelineState(pipelines_[dispatch.pipelineIndex]->Get());
        list->Dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
    }
    lastDispatchCount_ = count;

    // Hand the application's textures back in the state the renderer expects.
    for (gfx::GpuTexture2D* tex : touched) {
        if (tex != nullptr && tex->IsValid()) {
            tex->Transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
    }
}

}  // namespace lc
