#include "platform/files.h"

#include "core/error.h"
#include "core/log.h"

#include <windows.h>

#include <format>
#include <fstream>

namespace lc::files {

std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw Error(std::format("cannot open file for reading: {}", path.string()));
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw Error(std::format("cannot read file: {}", path.string()));
    }
    return bytes;
}

bool WriteBinaryFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::Error("cannot open file for writing: {}", path.string());
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        log::Error("write failed: {}", path.string());
        return false;
    }
    return true;
}

bool WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    return WriteBinaryFile(path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

std::filesystem::path ExecutablePath() {
    wchar_t buffer[MAX_PATH * 4] = {};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0) {
        throw Error("GetModuleFileNameW failed");
    }
    return std::filesystem::path(std::wstring(buffer, length));
}

std::filesystem::path ExecutableDirectory() { return ExecutablePath().parent_path(); }

}  // namespace lc::files
