// GPU timestamp queries with a per-frame-slot readback ring. Results for a slot are read after
// that slot's fence has completed (two frames later with two frames in flight).
#pragma once

#include "graphics/d3d12/d3d12_common.h"
#include "graphics/d3d12/gpu_buffer.h"

#include <string>
#include <vector>

namespace lc::gfx {

class Device;
class GraphicsQueue;

struct TimerResult {
    std::string name;
    double milliseconds = 0.0;
};

class TimestampQueries {
public:
    static constexpr std::uint32_t kInvalidTimer = 0xFFFFFFFFu;

    TimestampQueries(Device& device, GraphicsQueue& queue, std::uint32_t frameSlots, std::uint32_t maxTimersPerFrame);

    void BeginFrame(std::uint32_t slot);
    // Returns a timer id for End, or kInvalidTimer when the per-frame budget is exhausted.
    std::uint32_t Begin(ID3D12GraphicsCommandList* list, const char* name);
    void End(ID3D12GraphicsCommandList* list, std::uint32_t timer);
    // Resolves this frame's queries into the readback ring. Record at the end of the frame.
    void Resolve(ID3D12GraphicsCommandList* list);
    // Reads the results of the last frame that used this slot. Call only after its fence completed.
    std::vector<TimerResult> Collect(std::uint32_t slot);

private:
    std::uint32_t QueryIndex(std::uint32_t slot, std::uint32_t timer, bool end) const {
        return (slot * maxTimers_ + timer) * 2 + (end ? 1 : 0);
    }

    ComPtr<ID3D12QueryHeap> heap_;
    GpuBuffer readback_;
    std::uint32_t slots_ = 0;
    std::uint32_t maxTimers_ = 0;
    std::uint32_t currentSlot_ = 0;
    double ticksToMilliseconds_ = 0.0;
    std::vector<std::vector<std::string>> names_;  // Per slot.
    bool warnedBudget_ = false;
};

}  // namespace lc::gfx
