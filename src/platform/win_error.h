// HRESULT and Win32 error reporting with actionable messages.
#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace lc {

// "0x887A0005 (DXGI_ERROR_DEVICE_REMOVED): The GPU device instance has been suspended..."
std::string HrToString(HRESULT hr);
std::string LastErrorToString(DWORD error);

// Throws lc::Error naming the failing expression, the HRESULT, and the call site.
void CheckHr(HRESULT hr, const char* expression, const char* file, int line);

std::string WideToUtf8(std::wstring_view text);
std::wstring Utf8ToWide(std::string_view text);

}  // namespace lc

#define LC_CHECK_HR(expression) ::lc::CheckHr((expression), #expression, __FILE__, __LINE__)
