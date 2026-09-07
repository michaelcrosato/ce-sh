#include "graphics/d3d12/gpu_buffer.h"

#include "core/error.h"
#include "graphics/d3d12/device.h"

#include <cstring>
#include <format>

namespace lc::gfx {

GpuBuffer::~GpuBuffer() { Unmap(); }

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : resource_(std::move(other.resource_)), size_(other.size_), heapType_(other.heapType_), mapped_(other.mapped_) {
    other.size_ = 0;
    other.mapped_ = nullptr;
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        Unmap();
        resource_ = std::move(other.resource_);
        size_ = other.size_;
        heapType_ = other.heapType_;
        mapped_ = other.mapped_;
        other.size_ = 0;
        other.mapped_ = nullptr;
    }
    return *this;
}

GpuBuffer GpuBuffer::Create(Device& device, std::wstring_view name, std::uint64_t size, D3D12_HEAP_TYPE heapType,
                            D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState) {
    if (size == 0) {
        throw Error(std::format("cannot create a zero-sized buffer '{}'", WideToUtf8(name)));
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc = {1, 0};
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    GpuBuffer buffer;
    LC_CHECK_HR(device.Get()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr,
                                                      IID_PPV_ARGS(&buffer.resource_)));
    SetName(buffer.resource_.Get(), name);
    buffer.size_ = size;
    buffer.heapType_ = heapType;
    return buffer;
}

GpuBuffer GpuBuffer::CreateDefault(Device& device, std::wstring_view name, std::uint64_t size, D3D12_RESOURCE_FLAGS flags) {
    return Create(device, name, size, D3D12_HEAP_TYPE_DEFAULT, flags, D3D12_RESOURCE_STATE_COMMON);
}

GpuBuffer GpuBuffer::CreateUpload(Device& device, std::wstring_view name, std::uint64_t size) {
    return Create(device, name, size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ);
}

GpuBuffer GpuBuffer::CreateReadback(Device& device, std::wstring_view name, std::uint64_t size) {
    return Create(device, name, size, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
}

GpuBuffer GpuBuffer::CreateAccelerationStructure(Device& device, std::wstring_view name, std::uint64_t size) {
    return Create(device, name, size, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                  D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
}

void* GpuBuffer::Map() {
    if (mapped_ == nullptr) {
        if (heapType_ == D3D12_HEAP_TYPE_DEFAULT) {
            throw Error("cannot map a default-heap buffer");
        }
        // Readback: read everything. Upload: nothing is read back, but a null range is always valid.
        LC_CHECK_HR(resource_->Map(0, nullptr, &mapped_));
    }
    return mapped_;
}

void GpuBuffer::Unmap() {
    if (mapped_ != nullptr && resource_) {
        if (heapType_ == D3D12_HEAP_TYPE_READBACK) {
            const D3D12_RANGE nothingWritten{0, 0};
            resource_->Unmap(0, &nothingWritten);
        } else {
            resource_->Unmap(0, nullptr);
        }
    }
    mapped_ = nullptr;
}

GpuBuffer UploadToDefaultBuffer(Device& device, ID3D12GraphicsCommandList* list, GpuBuffer& dst,
                                std::span<const std::uint8_t> data, D3D12_RESOURCE_STATES finalState) {
    if (data.size() > dst.Size()) {
        throw Error(std::format("upload of {} bytes exceeds the destination buffer size {}", data.size(), dst.Size()));
    }
    GpuBuffer staging = GpuBuffer::CreateUpload(device, L"Upload Staging", data.size());
    std::memcpy(staging.Map(), data.data(), data.size());
    staging.Unmap();
    // The destination is in COMMON and is promoted to COPY_DEST by the copy.
    list->CopyBufferRegion(dst.Get(), 0, staging.Get(), 0, data.size());
    const D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(dst.Get(), D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    list->ResourceBarrier(1, &barrier);
    return staging;
}

}  // namespace lc::gfx
