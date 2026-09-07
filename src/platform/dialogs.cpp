#include "platform/dialogs.h"

#include "platform/win_error.h"

#include <windows.h>

#include <cstdio>

namespace lc {

void ShowErrorDialog(std::string_view title, std::string_view text) {
    MessageBoxW(nullptr, Utf8ToWide(text).c_str(), Utf8ToWide(title).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

bool ReleaseOwnConsole() {
    DWORD processes[2] = {};
    const DWORD count = GetConsoleProcessList(processes, 2);
    if (count != 1) return false;  // No console, or one shared with the process that started us.
    if (FreeConsole() == 0) return false;
    // The C runtime's standard streams still hold the freed console's handle values, which the next
    // CreateFile (the log file) can reuse: point them at NUL so nothing is written into that file twice.
    FILE* stream = nullptr;
    freopen_s(&stream, "NUL", "w", stdout);
    freopen_s(&stream, "NUL", "w", stderr);
    return true;
}

}  // namespace lc
