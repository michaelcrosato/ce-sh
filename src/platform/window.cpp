#include "platform/window.h"

#include "core/error.h"
#include "core/log.h"
#include "platform/win_error.h"

#include <windowsx.h>

#include <vector>

namespace lc {

namespace {

constexpr wchar_t kClassName[] = L"LastCircuitWindowClass";
bool g_classRegistered = false;

void EnsureDpiAwareness() {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        // Already set (by a manifest or an earlier call) or unsupported; sizes may then be scaled.
        log::Debug("SetProcessDpiAwarenessContext not applied: {}", LastErrorToString(GetLastError()));
    }
}

}  // namespace

Window::Window(const WindowDesc& desc) {
    EnsureDpiAwareness();
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    if (!g_classRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Window::WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (RegisterClassExW(&wc) == 0) {
            throw Error("RegisterClassExW failed: " + LastErrorToString(GetLastError()));
        }
        g_classRegistered = true;
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    hwnd_ = CreateWindowExW(0, kClassName, desc.title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                            static_cast<int>(desc.clientWidth), static_cast<int>(desc.clientHeight), nullptr, nullptr,
                            instance, this);
    if (hwnd_ == nullptr) {
        throw Error("CreateWindowExW failed: " + LastErrorToString(GetLastError()));
    }

    SetClientSize(desc.clientWidth, desc.clientHeight);
    if (desc.visible) {
        ShowWindow(hwnd_, SW_SHOW);
    }
    ReadClientSize();
    events_ = WindowEvents{};  // Creation-time WM_SIZE messages are not user events.
    log::Info("Window created: client {}x{} px, DPI {}", clientWidth_, clientHeight_, GetDpiForWindow(hwnd_));
}

Window::~Window() {
    if (hwnd_ != nullptr) {
        CaptureCursor(false);
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void Window::PumpMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            events_.closeRequested = true;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Window::EnableRawMouse() {
    if (rawMouseEnabled_) {
        return;
    }
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01;  // Generic desktop controls.
    device.usUsage = 0x02;      // Mouse.
    device.dwFlags = 0;         // Deliver only while this window has focus.
    device.hwndTarget = hwnd_;
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        throw Error("RegisterRawInputDevices failed: " + LastErrorToString(GetLastError()));
    }
    rawMouseEnabled_ = true;
    log::Info("Raw mouse input registered");
}

void Window::ClearInput() {
    input_.mouseDx = 0.0f;
    input_.mouseDy = 0.0f;
    for (bool& pressed : input_.keyPressed) {
        pressed = false;
    }
}

void Window::ApplyCursorClip() {
    if (!cursorCaptured_ || minimized_) {
        ClipCursor(nullptr);
        return;
    }
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    POINT topLeft{rc.left, rc.top};
    POINT bottomRight{rc.right, rc.bottom};
    ClientToScreen(hwnd_, &topLeft);
    ClientToScreen(hwnd_, &bottomRight);
    const RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screen);
}

void Window::CaptureCursor(bool capture) {
    if (capture == cursorCaptured_) {
        return;
    }
    cursorCaptured_ = capture;
    if (capture) {
        if (!cursorHidden_) {
            ShowCursor(FALSE);
            cursorHidden_ = true;
        }
        ApplyCursorClip();
    } else {
        ClipCursor(nullptr);
        if (cursorHidden_) {
            ShowCursor(TRUE);
            cursorHidden_ = false;
        }
    }
}

void Window::SetClientSize(std::uint32_t width, std::uint32_t height) {
    RECT rc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const UINT dpi = GetDpiForWindow(hwnd_);
    const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_EXSTYLE));
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ReadClientSize();
    ApplyCursorClip();
}

void Window::SetTitle(const std::wstring& title) { SetWindowTextW(hwnd_, title.c_str()); }

void Window::ReadClientSize() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    clientWidth_ = static_cast<std::uint32_t>(rc.right - rc.left);
    clientHeight_ = static_cast<std::uint32_t>(rc.bottom - rc.top);
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr || self->hwnd_ != hwnd) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return self->HandleMessage(message, wParam, lParam);
}

LRESULT Window::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CLOSE:
            events_.closeRequested = true;
            return 0;

        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                minimized_ = true;
                events_.minimized = true;
                ApplyCursorClip();
                return 0;
            }
            minimized_ = false;
            const std::uint32_t w = LOWORD(lParam);
            const std::uint32_t h = HIWORD(lParam);
            if (w != clientWidth_ || h != clientHeight_) {
                clientWidth_ = w;
                clientHeight_ = h;
                if (inSizeMove_) {
                    sizeChangedDuringMove_ = true;
                } else {
                    events_.resized = true;
                }
            }
            ApplyCursorClip();
            return 0;
        }

        case WM_MOVE:
            ApplyCursorClip();
            return 0;

        case WM_ENTERSIZEMOVE:
            inSizeMove_ = true;
            sizeChangedDuringMove_ = false;
            return 0;

        case WM_EXITSIZEMOVE:
            inSizeMove_ = false;
            ReadClientSize();
            if (sizeChangedDuringMove_) {
                events_.resized = true;
                sizeChangedDuringMove_ = false;
            }
            ApplyCursorClip();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = 320;
            info->ptMinTrackSize.y = 240;
            return 0;
        }

        case WM_SETFOCUS:
            focused_ = true;
            events_.focusGained = true;
            ApplyCursorClip();
            return 0;

        case WM_KILLFOCUS:
            focused_ = false;
            events_.focusLost = true;
            for (bool& down : input_.keyDown) {
                down = false;
            }
            input_.mouseDx = 0.0f;
            input_.mouseDy = 0.0f;
            ClipCursor(nullptr);  // The cursor is released while another window has focus.
            return 0;

        case WM_INPUT: {
            if (!rawMouseEnabled_) {
                break;
            }
            UINT size = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
            if (size == 0) {
                break;
            }
            std::vector<std::uint8_t> buffer(size);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) {
                break;
            }
            const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
            if (raw->header.dwType == RIM_TYPEMOUSE && (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                input_.mouseDx += static_cast<float>(raw->data.mouse.lLastX);
                input_.mouseDy += static_cast<float>(raw->data.mouse.lLastY);
            }
            break;  // DefWindowProc must still run for WM_INPUT cleanup.
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const unsigned key = static_cast<unsigned>(wParam) & 0xFF;
            const bool repeat = (lParam & (1 << 30)) != 0;
            if (!repeat) {
                input_.keyPressed[key] = true;
            }
            input_.keyDown[key] = true;
            if (wParam == VK_ESCAPE && !repeat) {
                events_.escapePressed = true;
            }
            if (message == WM_SYSKEYDOWN) {
                break;  // Let Alt combinations reach DefWindowProc.
            }
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP:
            input_.keyDown[static_cast<unsigned>(wParam) & 0xFF] = false;
            if (message == WM_SYSKEYUP) {
                break;
            }
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU && lParam == 0) {
                return 0;  // No menu: swallow a lone Alt press; Alt+Space (system menu) still works.
            }
            break;

        case WM_PAINT:
            ValidateRect(hwnd_, nullptr);  // Presentation is done by DXGI.
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

}  // namespace lc
