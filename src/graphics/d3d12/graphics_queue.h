// The single direct (graphics-capable) command queue and its fence.
#pragma once

#include "graphics/d3d12/d3d12_common.h"

namespace lc::gfx {

class Device;

class GraphicsQueue {
public:
    explicit GraphicsQueue(Device& device);
    ~GraphicsQueue();
    GraphicsQueue(const GraphicsQueue&) = delete;
    GraphicsQueue& operator=(const GraphicsQueue&) = delete;

    ID3D12CommandQueue* Get() const { return queue_.Get(); }

    void Execute(ID3D12CommandList* list);

    // Signals the fence and returns the value to wait for.
    std::uint64_t Signal();
    bool IsFenceComplete(std::uint64_t value) const;
    void WaitForFenceValue(std::uint64_t value);

    // Full GPU drain. Acceptable for setup, resize, and captures; never per frame in production.
    void WaitIdle();

    // Drain for destructors: never throws, returns immediately after device removal, bounded wait.
    void DrainForShutdown() noexcept;

    bool IsDeviceRemoved() const { return fence_ && fence_->GetCompletedValue() == UINT64_MAX; }

    std::uint64_t LastSignaledValue() const { return nextValue_ - 1; }
    std::uint64_t TimestampFrequency() const { return timestampFrequency_; }

private:
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE event_ = nullptr;
    std::uint64_t nextValue_ = 1;
    std::uint64_t timestampFrequency_ = 1;
};

}  // namespace lc::gfx
