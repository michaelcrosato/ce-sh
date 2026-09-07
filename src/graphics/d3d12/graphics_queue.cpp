#include "graphics/d3d12/graphics_queue.h"

#include "core/error.h"
#include "graphics/d3d12/device.h"

namespace lc::gfx {

GraphicsQueue::GraphicsQueue(Device& device) {
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    LC_CHECK_HR(device.Get()->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue_)));
    SetName(queue_.Get(), L"Graphics Queue");

    LC_CHECK_HR(device.Get()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)));
    SetName(fence_.Get(), L"Graphics Queue Fence");

    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event_ == nullptr) {
        throw Error("CreateEventW failed: " + LastErrorToString(GetLastError()));
    }
    LC_CHECK_HR(queue_->GetTimestampFrequency(&timestampFrequency_));
}

GraphicsQueue::~GraphicsQueue() {
    if (queue_ && fence_) {
        WaitIdle();
    }
    if (event_ != nullptr) {
        CloseHandle(event_);
    }
}

void GraphicsQueue::Execute(ID3D12CommandList* list) {
    ID3D12CommandList* lists[] = {list};
    queue_->ExecuteCommandLists(1, lists);
}

std::uint64_t GraphicsQueue::Signal() {
    const std::uint64_t value = nextValue_++;
    LC_CHECK_HR(queue_->Signal(fence_.Get(), value));
    return value;
}

bool GraphicsQueue::IsFenceComplete(std::uint64_t value) const { return fence_->GetCompletedValue() >= value; }

void GraphicsQueue::WaitForFenceValue(std::uint64_t value) {
    if (fence_->GetCompletedValue() >= value) {
        return;
    }
    LC_CHECK_HR(fence_->SetEventOnCompletion(value, event_));
    WaitForSingleObject(event_, INFINITE);
}

void GraphicsQueue::WaitIdle() { WaitForFenceValue(Signal()); }

}  // namespace lc::gfx
