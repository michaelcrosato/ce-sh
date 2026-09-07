// Application options parsed from the command line. Unknown options are errors (spec §21).
#pragma once

#include "render/view_mode.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace lc {

struct AppOptions {
    std::string scene = "rt_triangle";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    ViewMode view = ViewMode::Normals;
    bool headless = false;
    bool validate = false;
    bool resizeTest = false;
    bool listAdapters = false;
    bool debugLayer = false;      // Default depends on the build configuration (Debug: on).
    bool gpuValidation = false;
    bool vsync = true;
    int adapterIndex = -1;        // -1 selects the first adapter that supports hardware ray tracing.
    std::uint32_t frames = 0;     // 0 = run until the window closes.
    float horizontalFovDegrees = 90.0f;
    std::optional<std::filesystem::path> capture;
    std::optional<std::filesystem::path> envReport;
    std::optional<std::filesystem::path> logFile;
    bool help = false;
};

struct ParsedOptions {
    std::optional<AppOptions> options;  // Empty on error.
    std::string error;                  // Non-empty on error.
    std::string usage;                  // Always filled; printed for --help and on error.
};

ParsedOptions ParseAppOptions(std::span<const std::string> args);

}  // namespace lc
