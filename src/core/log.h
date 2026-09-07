// Process-wide logging. Every line goes to stderr and, when Init received a path, to a log file.
// Error-level messages are counted so validation modes can fail when anything logged an error.
#pragma once

#include <cstddef>
#include <filesystem>
#include <format>
#include <string_view>
#include <utility>

namespace lc::log {

enum class Level { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

// Opens the optional log file (appending) and starts the log clock. Safe to call once per process.
void Init(const std::filesystem::path* fileOrNull);
void Shutdown();

void SetMinLevel(Level level);
Level MinLevel();

void Write(Level level, std::string_view message);

// Number of Error-level messages written since Init (counted even when filtered by level).
std::size_t ErrorCount();

template <class... Args>
void Trace(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Debug(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Info(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Warn(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    Write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace lc::log
