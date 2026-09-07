// Shared includes and small helpers for the D3D12 layer. Every graphics header includes this first
// so the Windows macros (GetMessage and friends) expand consistently everywhere.
#pragma once

#include <windows.h>

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "platform/win_error.h"

#include <cstdint>
#include <string_view>

namespace lc::gfx {

template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

inline constexpr std::uint32_t kFramesInFlight = 2;

inline void SetName(ID3D12Object* object, std::wstring_view name) {
    if (object != nullptr) {
        object->SetName(std::wstring(name).c_str());
    }
}

inline constexpr std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

inline D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                                D3D12_RESOURCE_STATES after,
                                                UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    b.Transition.Subresource = subresource;
    return b;
}

inline D3D12_RESOURCE_BARRIER UavBarrier(ID3D12Resource* resource) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    b.UAV.pResource = resource;
    return b;
}

}  // namespace lc::gfx
