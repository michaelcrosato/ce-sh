#include "core/error.h"

#include <filesystem>
#include <format>

namespace lc {

[[noreturn]] void ThrowError(std::string message, const char* file, int line) {
    const std::string base = std::filesystem::path(file).filename().string();
    throw Error(std::format("{} ({}:{})", message, base, line));
}

}  // namespace lc
