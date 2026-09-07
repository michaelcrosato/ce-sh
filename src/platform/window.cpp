#include "platform/window.h"

#include "core/error.h"
#include "core/log.h"
#include "platform/win_error.h"

#include <windowsx.h>

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

void Window::SetClientSize(std::uint32_t width, std::uint32_t height) {
    RECT rc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const UINT dpi = GetDpiForWindow(hwnd_);
    const DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_EXSTYLE));
    AdjustWindowRectExForDpi(&rc, style, FALSE, exStyle, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ReadClientSize();
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
            return 0;
        }

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
            return 0;

        case WM_KILLFOCUS:
            focused_ = false;
            events_.focusLost = true;
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                events_.escapePressed = true;
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
