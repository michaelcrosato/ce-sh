#include "core/clock.h"

#include <chrono>
#include <ctime>

namespace lc {

namespace {

std::chrono::steady_clock::time_point StartTime() {
    static const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    return start;
}

std::string FormatLocalTime(const char* format) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), format, &local);
    return buffer;
}

}  // namespace

double Clock::SecondsSinceStart() {
    const auto start = StartTime();
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start).count();
}

std::string Clock::TimestampIso8601() { return FormatLocalTime("%Y-%m-%dT%H:%M:%S"); }

std::string Clock::TimestampCompact() { return FormatLocalTime("%Y%m%d-%H%M%S"); }

}  // namespace lc
