// Application options parsed from the command line. Unknown options are errors (spec §21).
#pragma once

#include "render/view_mode.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace lc {

enum class AppRenderMode { Diagnostic, Raw, Reference, Denoised };
enum class AppStrategy { Mis, Light, Bsdf };

struct CropRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;   // 0 = full image.
    std::uint32_t height = 0;
};

struct AppOptions {
    std::string scene = "rt_triangle";
    std::optional<std::filesystem::path> sceneFile;  // A scene file inside the asset root (replaces --scene).
    bool reloadTest = false;                         // Headless: render, reload the scene file, render, validate.
    std::uint32_t width = 1280;         // Window client size; also the trace size unless --internal is given.
    std::uint32_t height = 720;
    std::uint32_t internalWidth = 0;    // --internal WxH: trace and denoise size; the display is resampled to the window (0 = same).
    std::uint32_t internalHeight = 0;
    ViewMode view = ViewMode::Normals;
    bool viewSet = false;               // --view was given (denoised mode draws it as an overlay).
    AppRenderMode mode = AppRenderMode::Raw;
    AppStrategy strategy = AppStrategy::Mis;
    std::uint32_t spp = 256;            // Reference mode target samples per pixel.
    std::uint32_t maxHits = 4;
    std::uint32_t samplesPerFrame = 4;  // Reference mode dispatches per frame.
    float exposure = 1.0f;
    std::uint32_t seed = 0;
    bool jitter = true;
    bool headless = false;
    bool validate = false;
    bool resizeTest = false;
    bool listAdapters = false;
    bool debugLayer = false;      // Default depends on the build configuration (Debug: on).
    bool gpuValidation = false;
    bool vsync = true;
    int adapterIndex = -1;        // -1 selects the first adapter that supports hardware ray tracing.
    std::uint32_t frames = 0;     // 0 = run until the window closes (or the reference target is reached).
    float horizontalFovDegrees = 90.0f;
    std::optional<std::filesystem::path> capture;
    std::optional<std::filesystem::path> stats;
    std::optional<std::filesystem::path> envReport;
    std::optional<std::filesystem::path> logFile;
    // Simulation (M3): live play, input recording, deterministic replay.
    bool play = false;                                  // Live input with the cursor captured (windowed).
    bool noUi = false;                                  // No on-screen interface (benchmarks; also implied by --benchmark-seconds).
    std::optional<std::filesystem::path> record;        // With --play: write the input replay.
    std::optional<std::filesystem::path> replay;        // Drive the simulation from a replay file.
    std::int64_t stopAtTick = -1;                       // With --replay: run exactly this many ticks, then freeze.
    float mouseSensitivity = 0.0022f;                   // Radians per raw mouse count.
    // Denoised mode (M4).
    std::uint32_t historyFrames = 30;                   // NRD history bound in frames.
    float prepassRadius = 0.0f;                         // NRD diffuse pre-accumulation blur radius in pixels (0 = off).
    float blurRadius = 30.0f;                           // NRD maximum spatial blur radius in pixels.
    bool antiFirefly = true;
    bool validationOverlay = false;                     // NRD renders its validation layer (shown by --view validation).
    bool resetOnSourceChange = false;                   // Explicit history reset when an emitter switches.
    std::int64_t blackoutAtFrame = -1;                  // Test hook: switch every emitter off and reset history before this frame.
    // Frame sequences (spec §18): one cropped display PNG per selected frame plus an event log.
    std::optional<std::filesystem::path> captureSequence;
    std::uint32_t captureFrom = 0;
    std::uint32_t captureTo = 0;                        // 0 = until exit.
    std::uint32_t captureEvery = 1;
    CropRect captureCrop;
    // Benchmark (spec §17, §21): loop the replay and write the report.
    float benchmarkSeconds = 0.0f;
    float warmupSeconds = 5.0f;
    std::optional<std::filesystem::path> report;
    bool help = false;
};

struct ParsedOptions {
    std::optional<AppOptions> options;  // Empty on error.
    std::string error;                  // Non-empty on error.
    std::string usage;                  // Always filled; printed for --help and on error.
};

ParsedOptions ParseAppOptions(std::span<const std::string> args);

}  // namespace lc
