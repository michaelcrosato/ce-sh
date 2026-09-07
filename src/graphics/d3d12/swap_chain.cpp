#include "graphics/d3d12/swap_chain.h"

#include "core/error.h"
#include "core/log.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/graphics_queue.h"

#include <algorithm>
#include <format>

namespace lc::gfx {

SwapChain::SwapChain(Device& device, GraphicsQueue& queue, HWND window, std::uint32_t width, std::uint32_t height, bool vsync)
    : queue_(queue), vsync_(vsync), tearing_(device.Caps().tearingSupported) {
    width_ = std::max<std::uint32_t>(1, width);
    height_ = std::max<std::uint32_t>(1, height);

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = Format();
    desc.Stereo = FALSE;
    desc.SampleDesc = {1, 0};
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kBufferCount;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    LC_CHECK_HR(device.Factory()->CreateSwapChainForHwnd(queue.Get(), window, &desc, nullptr, nullptr, &swapChain1));
    LC_CHECK_HR(device.Factory()->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
    LC_CHECK_HR(swapChain1.As(&swapChain_));
    AcquireBuffers();
    log::Info("Swap chain created: {}x{}, {} buffers, flip discard, vsync {}, tearing {}", width_, height_, kBufferCount,
              vsync_ ? "on" : "off", tearing_ ? "allowed" : "unavailable");
}

SwapChain::~SwapChain() { queue_.DrainForShutdown(); }

void SwapChain::AcquireBuffers() {
    for (std::uint32_t i = 0; i < kBufferCount; ++i) {
        LC_CHECK_HR(swapChain_->GetBuffer(i, IID_PPV_ARGS(&buffers_[i])));
        SetName(buffers_[i].Get(), std::format(L"Swap Chain Buffer {}", i));
    }
}

void SwapChain::Resize(std::uint32_t width, std::uint32_t height) {
    width = std::max<std::uint32_t>(1, width);
    height = std::max<std::uint32_t>(1, height);
    if (width == width_ && height == height_) {
        return;
    }
    queue_.WaitIdle();
    for (auto& buffer : buffers_) {
        buffer.Reset();
    }
    LC_CHECK_HR(swapChain_->ResizeBuffers(kBufferCount, width, height, DXGI_FORMAT_UNKNOWN,
                                          tearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0));
    width_ = width;
    height_ = height;
    AcquireBuffers();
    log::Info("Swap chain resized to {}x{}", width_, height_);
}

HRESULT SwapChain::Present() {
    const UINT interval = vsync_ ? 1 : 0;
    const UINT flags = (!vsync_ && tearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    return swapChain_->Present(interval, flags);
}

}  // namespace lc::gfx
