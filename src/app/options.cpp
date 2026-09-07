#include "app/options.h"

#include "core/cli.h"

#include <format>

namespace lc {

namespace {

#if defined(NDEBUG)
constexpr const char* kDefaultDebugLayer = "off";
#else
constexpr const char* kDefaultDebugLayer = "on";
#endif

std::optional<bool> ParseOnOff(std::string_view text) {
    if (text == "on" || text == "1" || text == "true") return true;
    if (text == "off" || text == "0" || text == "false") return false;
    return std::nullopt;
}

std::string ViewModeList() {
    std::string list;
    for (const auto name : kViewModeNames) {
        if (!list.empty()) list += ", ";
        list += name;
    }
    return list;
}

}  // namespace

ParsedOptions ParseAppOptions(std::span<const std::string> args) {
    ArgParser p;
    p.AddFlag("help", "Print this help and exit.");
    p.AddStringOption("scene", "Built-in scene name (see docs/TESTS.md for the list).", "rt_triangle");
    p.AddIntOption("width", "Window client width and internal trace width in pixels.", 1280);
    p.AddIntOption("height", "Window client height and internal trace height in pixels.", 720);
    p.AddStringOption("mode", "diag (camera-ray diagnostic views), raw (one path sample per frame), reference (progressive accumulation).", "raw");
    p.AddStringOption("view", "Diagnostic view for --mode diag: " + ViewModeList() + ".", "normals");
    p.AddStringOption("strategy", "Integrator estimator: mis, light, bsdf.", "mis");
    p.AddIntOption("spp", "Reference mode: samples per pixel to accumulate before capture/validation (never capped).", 256);
    p.AddIntOption("max-hits", "Surface hits per path including the camera hit and mirror hits (2..32).", 4);
    p.AddIntOption("samples-per-frame", "Reference mode: dispatches per frame (1..64), bounding GPU work per submission.", 4);
    p.AddFloatOption("exposure", "Display exposure scale applied before sRGB encoding (does not change radiance).", 1.0f);
    p.AddIntOption("seed", "Deterministic sampling seed.", 0);
    p.AddFlag("no-jitter", "Trace through pixel centres instead of jittering inside the pixel.");
    p.AddFlag("headless", "Render without a window or swap chain (used by tests and captures).");
    p.AddFlag("validate", "After rendering, check hit identifiers, radiance expectations, GPU layouts, invalid-value counters, and debug-layer messages; exit 0 or 1.");
    p.AddFlag("resize-test", "Cycle the window through several client sizes and verify the outputs follow.");
    p.AddFlag("list-adapters", "List graphics adapters with their ray-tracing support, then exit.");
    p.AddStringOption("debug-layer", "on|off: D3D12 debug layer.", kDefaultDebugLayer);
    p.AddFlag("gpu-validation", "Enable GPU-based validation (slow; forces the debug layer on).");
    p.AddStringOption("vsync", "on|off: synchronize presentation with the display.", "on");
    p.AddIntOption("adapter", "Adapter index from --list-adapters; -1 = first adapter with DXR Tier 1.1.", -1);
    p.AddIntOption("frames", "Exit after this many rendered frames; 0 = run until closed (reference mode: until --spp is reached).", 0);
    p.AddFloatOption("fov", "Horizontal field of view in degrees at the current aspect ratio.", 90.0f);
    p.AddStringOption("capture", "Directory that receives PNG, PFM, and JSON captures of the final image.", "");
    p.AddStringOption("stats", "Write patch means and standard errors of the scene's statistics patches to this JSON file.", "");
    p.AddStringOption("env-report", "Write a JSON environment report to this file.", "");
    p.AddStringOption("log", "Append the log to this file.", "");

    ParsedOptions result;
    result.usage = p.Usage("LastCircuit.exe",
                           "Last Circuit: custom D3D12 engine with mandatory hardware path tracing (M2: raw light transport).");

    if (const auto error = p.Parse(args)) {
        result.error = *error;
        return result;
    }

    AppOptions o;
    o.help = p.Has("help");
    o.scene = p.GetString("scene");

    const int width = p.GetInt("width");
    const int height = p.GetInt("height");
    if (width < 16 || width > 16384 || height < 16 || height > 16384) {
        result.error = std::format("--width and --height must be between 16 and 16384 (got {}x{})", width, height);
        return result;
    }
    o.width = static_cast<std::uint32_t>(width);
    o.height = static_cast<std::uint32_t>(height);

    const std::string mode = p.GetString("mode");
    if (mode == "diag") o.mode = AppRenderMode::Diagnostic;
    else if (mode == "raw") o.mode = AppRenderMode::Raw;
    else if (mode == "reference") o.mode = AppRenderMode::Reference;
    else {
        result.error = std::format("unknown --mode '{}'; valid modes: diag, raw, reference", mode);
        return result;
    }

    const auto view = ViewModeFromName(p.GetString("view"));
    if (!view) {
        result.error = std::format("unknown --view '{}'; valid views: {}", p.GetString("view"), ViewModeList());
        return result;
    }
    o.view = *view;

    const std::string strategy = p.GetString("strategy");
    if (strategy == "mis") o.strategy = AppStrategy::Mis;
    else if (strategy == "light") o.strategy = AppStrategy::Light;
    else if (strategy == "bsdf") o.strategy = AppStrategy::Bsdf;
    else {
        result.error = std::format("unknown --strategy '{}'; valid strategies: mis, light, bsdf", strategy);
        return result;
    }

    const int spp = p.GetInt("spp");
    if (spp < 1) {
        result.error = std::format("--spp must be at least 1 (got {})", spp);
        return result;
    }
    o.spp = static_cast<std::uint32_t>(spp);

    const int maxHits = p.GetInt("max-hits");
    if (maxHits < 2 || maxHits > 32) {
        result.error = std::format("--max-hits must be between 2 and 32 (got {})", maxHits);
        return result;
    }
    o.maxHits = static_cast<std::uint32_t>(maxHits);

    const int samplesPerFrame = p.GetInt("samples-per-frame");
    if (samplesPerFrame < 1 || samplesPerFrame > 64) {
        result.error = std::format("--samples-per-frame must be between 1 and 64 (got {})", samplesPerFrame);
        return result;
    }
    o.samplesPerFrame = static_cast<std::uint32_t>(samplesPerFrame);

    o.exposure = p.GetFloat("exposure");
    if (!(o.exposure > 0.0f) || o.exposure > 1e6f) {
        result.error = std::format("--exposure must be positive and finite (got {})", o.exposure);
        return result;
    }

    const int seed = p.GetInt("seed");
    if (seed < 0) {
        result.error = std::format("--seed must be zero or positive (got {})", seed);
        return result;
    }
    o.seed = static_cast<std::uint32_t>(seed);
    o.jitter = !p.Has("no-jitter");

    o.headless = p.Has("headless");
    o.validate = p.Has("validate");
    o.resizeTest = p.Has("resize-test");
    o.listAdapters = p.Has("list-adapters");

    const auto debugLayer = ParseOnOff(p.GetString("debug-layer"));
    if (!debugLayer) {
        result.error = std::format("--debug-layer expects on or off, got '{}'", p.GetString("debug-layer"));
        return result;
    }
    o.gpuValidation = p.Has("gpu-validation");
    o.debugLayer = *debugLayer || o.gpuValidation;

    const auto vsync = ParseOnOff(p.GetString("vsync"));
    if (!vsync) {
        result.error = std::format("--vsync expects on or off, got '{}'", p.GetString("vsync"));
        return result;
    }
    o.vsync = *vsync;

    o.adapterIndex = p.GetInt("adapter");
    if (o.adapterIndex < -1) {
        result.error = std::format("--adapter must be -1 or a non-negative index (got {})", o.adapterIndex);
        return result;
    }

    const int frames = p.GetInt("frames");
    if (frames < 0) {
        result.error = std::format("--frames must be zero or positive (got {})", frames);
        return result;
    }
    o.frames = static_cast<std::uint32_t>(frames);

    o.horizontalFovDegrees = p.GetFloat("fov");
    if (o.horizontalFovDegrees < 10.0f || o.horizontalFovDegrees > 170.0f) {
        result.error = std::format("--fov must be between 10 and 170 degrees (got {})", o.horizontalFovDegrees);
        return result;
    }

    if (const std::string s = p.GetString("capture"); !s.empty()) o.capture = std::filesystem::path(s);
    if (const std::string s = p.GetString("stats"); !s.empty()) o.stats = std::filesystem::path(s);
    if (const std::string s = p.GetString("env-report"); !s.empty()) o.envReport = std::filesystem::path(s);
    if (const std::string s = p.GetString("log"); !s.empty()) o.logFile = std::filesystem::path(s);

    if (o.resizeTest && o.headless) {
        result.error = "--resize-test needs a window; remove --headless";
        return result;
    }

    result.options = std::move(o);
    return result;
}

}  // namespace lc
