#include "core/log.h"

#include "core/clock.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

namespace lc::log {

namespace {

std::mutex g_mutex;
std::ofstream g_file;
std::atomic<Level> g_minLevel{Level::Info};
std::atomic<std::size_t> g_errorCount{0};

const char* LevelTag(Level level) {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO ";
        case Level::Warn: return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?????";
}

}  // namespace

void Init(const std::filesystem::path* fileOrNull) {
    Clock::SecondsSinceStart();  // Start the process clock as early as possible.
    std::lock_guard lock(g_mutex);
    if (fileOrNull != nullptr) {
        std::error_code ec;
        if (fileOrNull->has_parent_path()) {
            std::filesystem::create_directories(fileOrNull->parent_path(), ec);
        }
        g_file.open(*fileOrNull, std::ios::out | std::ios::app);
        if (!g_file.is_open()) {
            std::fprintf(stderr, "[log] could not open log file: %s\n", fileOrNull->string().c_str());
        } else {
            g_file << "==== Last Circuit log opened " << Clock::TimestampIso8601() << " ====\n";
        }
    }
}

void Shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) {
        g_file.flush();
        g_file.close();
    }
}

void SetMinLevel(Level level) { g_minLevel.store(level); }
Level MinLevel() { return g_minLevel.load(); }

void Write(Level level, std::string_view message) {
    if (level == Level::Error) {
        g_errorCount.fetch_add(1);
    }
    if (level < g_minLevel.load()) {
        return;
    }
    const std::string line = std::format("[{:>9.3f}s] [{}] {}\n", Clock::SecondsSinceStart(), LevelTag(level), message);
    std::lock_guard lock(g_mutex);
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
    if (g_file.is_open()) {
        g_file << line;
        g_file.flush();
    }
}

std::size_t ErrorCount() { return g_errorCount.load(); }

}  // namespace lc::log
