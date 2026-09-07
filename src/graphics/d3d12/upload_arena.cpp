#include "graphics/d3d12/upload_arena.h"

#include "core/error.h"

#include <format>

namespace lc::gfx {

UploadArena::UploadArena(Device& device, std::uint64_t capacity, std::wstring_view name)
    : buffer_(GpuBuffer::CreateUpload(device, name, capacity)), capacity_(capacity) {
    base_ = static_cast<std::uint8_t*>(buffer_.Map());
}

UploadAllocation UploadArena::Allocate(std::uint64_t size, std::uint64_t alignment) {
    const std::uint64_t start = AlignUp(offset_, alignment);
    if (start + size > capacity_) {
        throw Error(std::format("upload arena exhausted: {} bytes requested at offset {} of {}", size, start, capacity_));
    }
    offset_ = start + size;
    UploadAllocation a;
    a.cpu = base_ + start;
    a.gpu = buffer_.Address() + start;
    a.size = size;
    return a;
}

}  // namespace lc::gfx
