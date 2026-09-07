// File helpers. Paths for runtime assets are resolved relative to the executable, never the CWD.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace lc::files {

// Throws lc::Error naming the path when the file cannot be read.
std::vector<std::uint8_t> ReadBinaryFile(const std::filesystem::path& path);

// Creates parent directories. Returns false (and logs) on failure.
bool WriteBinaryFile(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);
bool WriteTextFile(const std::filesystem::path& path, std::string_view text);

std::filesystem::path ExecutablePath();
std::filesystem::path ExecutableDirectory();

// The user's writable data directory for settings and logs: %LOCALAPPDATA%\LastCircuit (created
// on demand); <executable dir>\userdata when the variable is not set.
std::filesystem::path UserDataDirectory();

}  // namespace lc::files
