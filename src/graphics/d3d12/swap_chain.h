// Flip-model swap chain. Resize waits for the GPU (documented full wait) and never creates a
// zero-sized buffer; callers skip presentation while the window is minimized.
#pragma once

#include "graphics/d3d12/d3d12_common.h"

namespace lc::gfx {

class Device;
class GraphicsQueue;

class SwapChain {
public:
    static constexpr std::uint32_t kBufferCount = 3;

    SwapChain(Device& device, GraphicsQueue& queue, HWND window, std::uint32_t width, std::uint32_t height, bool vsync);
    ~SwapChain();
    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;

    void Resize(std::uint32_t width, std::uint32_t height);

    ID3D12Resource* CurrentBackBuffer() const { return buffers_[CurrentIndex()].Get(); }
    std::uint32_t CurrentIndex() const { return swapChain_->GetCurrentBackBufferIndex(); }

    // Returns the Present HRESULT; DXGI_ERROR_DEVICE_REMOVED / DEVICE_RESET must be handled by the caller.
    HRESULT Present();

    std::uint32_t Width() const { return width_; }
    std::uint32_t Height() const { return height_; }
    DXGI_FORMAT Format() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }

private:
    void AcquireBuffers();

    GraphicsQueue& queue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12Resource> buffers_[kBufferCount];
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool vsync_ = true;
    bool tearing_ = false;
};

}  // namespace lc::gfx
