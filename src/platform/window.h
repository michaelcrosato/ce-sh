// Win32 window with an explicit event record and raw input. The process is per-monitor DPI aware;
// sizes are physical pixels. Rendering happens elsewhere; the window only reports what the user did.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace lc {

struct WindowDesc {
    std::wstring title = L"Last Circuit";
    std::uint32_t clientWidth = 1280;
    std::uint32_t clientHeight = 720;
    bool visible = true;
};

struct WindowEvents {
    bool closeRequested = false;
    bool resized = false;        // Client size changed (not while a drag-resize is in progress).
    bool minimized = false;      // Became minimized during this pump.
    bool focusLost = false;
    bool focusGained = false;
    bool escapePressed = false;
};

// Accumulated since the last ClearInput: relative mouse motion from Raw Input and key states.
struct RawInputState {
    float mouseDx = 0.0f;        // Raw counts, +X right.
    float mouseDy = 0.0f;        // Raw counts, +Y down.
    bool keyDown[256] = {};      // Virtual-key codes currently held.
    bool keyPressed[256] = {};   // Pressed since the last ClearInput (no auto-repeat).
};

class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    HWND Handle() const { return hwnd_; }

    // Dispatches pending messages; afterwards Events() and Input() describe what happened.
    void PumpMessages();
    const WindowEvents& Events() const { return events_; }
    void ClearEvents() { events_ = WindowEvents{}; }

    // Raw mouse input is registered on demand; deltas and key edges accumulate until ClearInput.
    void EnableRawMouse();
    const RawInputState& Input() const { return input_; }
    void ClearInput();

    // Confines and hides the cursor while looking around. Focus loss releases it.
    void CaptureCursor(bool capture);
    bool CursorCaptured() const { return cursorCaptured_; }

    std::uint32_t ClientWidth() const { return clientWidth_; }
    std::uint32_t ClientHeight() const { return clientHeight_; }
    bool IsMinimized() const { return minimized_; }
    bool HasFocus() const { return focused_; }

    // Resizes the frame so the client area has exactly this size (subject to OS limits).
    void SetClientSize(std::uint32_t width, std::uint32_t height);
    void SetTitle(const std::wstring& title);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void ReadClientSize();
    void ApplyCursorClip();

    HWND hwnd_ = nullptr;
    WindowEvents events_;
    RawInputState input_;
    std::uint32_t clientWidth_ = 0;
    std::uint32_t clientHeight_ = 0;
    bool minimized_ = false;
    bool focused_ = false;
    bool inSizeMove_ = false;
    bool sizeChangedDuringMove_ = false;
    bool rawMouseEnabled_ = false;
    bool cursorCaptured_ = false;
    bool cursorHidden_ = false;
};

}  // namespace lc
