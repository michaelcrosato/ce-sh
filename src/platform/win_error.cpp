#include "platform/win_error.h"

#include "core/error.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <filesystem>
#include <format>

namespace lc {

namespace {

const char* KnownHrName(HRESULT hr) {
    switch (hr) {
        case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
        case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
        case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
        case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
        case DXGI_ERROR_UNSUPPORTED: return "DXGI_ERROR_UNSUPPORTED";
        case DXGI_ERROR_NOT_FOUND: return "DXGI_ERROR_NOT_FOUND";
        case DXGI_ERROR_ACCESS_DENIED: return "DXGI_ERROR_ACCESS_DENIED";
        case DXGI_ERROR_WAS_STILL_DRAWING: return "DXGI_ERROR_WAS_STILL_DRAWING";
        case D3D12_ERROR_ADAPTER_NOT_FOUND: return "D3D12_ERROR_ADAPTER_NOT_FOUND";
        case D3D12_ERROR_DRIVER_VERSION_MISMATCH: return "D3D12_ERROR_DRIVER_VERSION_MISMATCH";
        case E_INVALIDARG: return "E_INVALIDARG";
        case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
        case E_NOINTERFACE: return "E_NOINTERFACE";
        case E_FAIL: return "E_FAIL";
        case E_NOTIMPL: return "E_NOTIMPL";
        default: return nullptr;
    }
}

std::string SystemMessage(DWORD code) {
    char* buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string message = length > 0 && buffer != nullptr ? std::string(buffer, length) : std::string{};
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }
    return message;
}

}  // namespace

std::string HrToString(HRESULT hr) {
    std::string text = std::format("0x{:08X}", static_cast<std::uint32_t>(hr));
    if (const char* name = KnownHrName(hr)) {
        text += std::format(" ({})", name);
    }
    const std::string message = SystemMessage(static_cast<DWORD>(hr));
    if (!message.empty()) {
        text += ": " + message;
    }
    return text;
}

std::string LastErrorToString(DWORD error) {
    const std::string message = SystemMessage(error);
    return std::format("Win32 error {}{}", error, message.empty() ? "" : ": " + message);
}

void CheckHr(HRESULT hr, const char* expression, const char* file, int line) {
    if (SUCCEEDED(hr)) {
        return;
    }
    const std::string base = std::filesystem::path(file).filename().string();
    throw Error(std::format("{} failed with {} ({}:{})", expression, HrToString(hr), base, line));
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

}  // namespace lc
