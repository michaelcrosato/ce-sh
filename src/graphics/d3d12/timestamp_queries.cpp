#include "graphics/d3d12/timestamp_queries.h"

#include "core/log.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/graphics_queue.h"

#include <cstring>

namespace lc::gfx {

TimestampQueries::TimestampQueries(Device& device, GraphicsQueue& queue, std::uint32_t frameSlots, std::uint32_t maxTimersPerFrame)
    : slots_(frameSlots), maxTimers_(maxTimersPerFrame), names_(frameSlots) {
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = frameSlots * maxTimersPerFrame * 2;
    LC_CHECK_HR(device.Get()->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap_)));
    SetName(heap_.Get(), L"Timestamp Query Heap");
    readback_ = GpuBuffer::CreateReadback(device, L"Timestamp Readback", static_cast<std::uint64_t>(desc.Count) * sizeof(std::uint64_t));
    ticksToMilliseconds_ = 1000.0 / static_cast<double>(queue.TimestampFrequency());
}

void TimestampQueries::BeginFrame(std::uint32_t slot) {
    currentSlot_ = slot;
    names_[slot].clear();
}

std::uint32_t TimestampQueries::Begin(ID3D12GraphicsCommandList* list, const char* name) {
    auto& names = names_[currentSlot_];
    if (names.size() >= maxTimers_) {
        if (!warnedBudget_) {
            log::Warn("timestamp budget of {} timers per frame exhausted; '{}' is not timed", maxTimers_, name);
            warnedBudget_ = true;
        }
        return kInvalidTimer;
    }
    const auto timer = static_cast<std::uint32_t>(names.size());
    names.emplace_back(name);
    list->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(currentSlot_, timer, false));
    return timer;
}

void TimestampQueries::End(ID3D12GraphicsCommandList* list, std::uint32_t timer) {
    if (timer == kInvalidTimer) {
        return;
    }
    list->EndQuery(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(currentSlot_, timer, true));
}

void TimestampQueries::Resolve(ID3D12GraphicsCommandList* list) {
    const auto count = static_cast<std::uint32_t>(names_[currentSlot_].size());
    if (count == 0) {
        return;
    }
    const std::uint32_t first = QueryIndex(currentSlot_, 0, false);
    list->ResolveQueryData(heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first, count * 2, readback_.Get(),
                           static_cast<UINT64>(first) * sizeof(std::uint64_t));
}

std::vector<TimerResult> TimestampQueries::Collect(std::uint32_t slot) {
    std::vector<TimerResult> results;
    const auto& names = names_[slot];
    if (names.empty()) {
        return results;
    }
    const auto* ticks = static_cast<const std::uint64_t*>(readback_.Map());
    for (std::uint32_t t = 0; t < names.size(); ++t) {
        const std::uint64_t begin = ticks[QueryIndex(slot, t, false)];
        const std::uint64_t end = ticks[QueryIndex(slot, t, true)];
        const double ms = end >= begin ? static_cast<double>(end - begin) * ticksToMilliseconds_ : 0.0;
        results.push_back(TimerResult{names[t], ms});
    }
    readback_.Unmap();
    return results;
}

}  // namespace lc::gfx
