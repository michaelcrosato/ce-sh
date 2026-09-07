#include "app/options.h"

#include "core/cli.h"

#include <charconv>
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

// Parses a list of unsigned integers separated by `separator` ("1280x720", "8,8,64,64").
bool ParseUnsignedList(std::string_view text, char separator, std::span<std::uint32_t> out) {
    std::size_t index = 0;
    std::size_t start = 0;
    while (index < out.size()) {
        const std::size_t end = text.find(separator, start);
        const std::string_view token = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (token.empty()) return false;
        unsigned long long value = 0;
        const auto res = std::from_chars(token.data(), token.data() + token.size(), value);
        if (res.ec != std::errc() || res.ptr != token.data() + token.size() || value > 0xFFFFFFFFull) return false;
        out[index++] = static_cast<std::uint32_t>(value);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return index == out.size() && text.find(separator, start) == std::string_view::npos;
}

}  // namespace

ParsedOptions ParseAppOptions(std::span<const std::string> args) {
    ArgParser p;
    p.AddFlag("help", "Print this help and exit.");
    p.AddStringOption("scene", "Built-in scene name (see docs/TESTS.md for the list).", "rt_triangle");
    p.AddStringOption("scene-file", "Scene file inside the asset root (for example scenes/two_room.json); replaces --scene.", "");
    p.AddFlag("reload-test", "Render, reload the scene file at a frame boundary, render again, and verify (headless).");
    p.AddIntOption("width", "Window client width in pixels (and the trace width unless --internal is given).", 1280);
    p.AddIntOption("height", "Window client height in pixels (and the trace height unless --internal is given).", 720);
    p.AddStringOption("internal", "Internal trace size WxH when it differs from the window (e.g. 1280x720); the display is resampled.", "");
    p.AddStringOption("mode",
                      "diag (camera-ray diagnostic views), raw (one path sample per frame), reference (progressive accumulation), "
                      "denoised (one sample per frame reconstructed by NRD REBLUR).",
                      "raw");
    p.AddStringOption("view",
                      "Diagnostic view. --mode diag: normals, ids, depth, bary, facing, prims. --mode denoised overlays: normals, depth, "
                      "motion, viewz, history, diffuse, specular, raw, direct, indirect, validation. All: " + ViewModeList() + ".",
                      "normals");
    p.AddStringOption("strategy", "Integrator estimator: mis, light, bsdf.", "mis");
    p.AddIntOption("spp", "Reference mode: samples per pixel to accumulate before capture/validation (never capped).", 256);
    p.AddIntOption("max-hits", "Surface hits per path including the camera hit and mirror hits (2..32).", 4);
    p.AddIntOption("samples-per-frame", "Reference mode: dispatches per frame (1..64), bounding GPU work per submission.", 4);
    p.AddFloatOption("exposure", "Display exposure scale applied before sRGB encoding (does not change radiance).", 1.0f);
    p.AddIntOption("seed", "Deterministic sampling seed.", 0);
    p.AddFlag("no-jitter", "Trace through pixel centres instead of jittering inside the pixel (raw and reference modes).");
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
    p.AddFlag("play", "Live play (scene two_room): WASD move, mouse look, Shift sprint, E interact, F lamp, Escape releases the cursor.");
    p.AddFlag("no-ui", "Windowed runs: no prompts, pause menu (Escape), or diagnostic panel (F1); the benchmark never draws them.");
    p.AddStringOption("record", "With --play: write the per-tick input to this replay file on exit.", "");
    p.AddStringOption("replay", "Drive the simulation from this replay file (one tick per frame; deterministic).", "");
    p.AddIntOption("stop-at-tick", "With --replay: advance exactly this many ticks before rendering, then freeze (for reference mode and checks).", -1);
    p.AddFloatOption("sensitivity", "Mouse look sensitivity in radians per raw count.", 0.0022f);
    p.AddIntOption("history-frames", "Denoised mode: NRD history bound in frames (1..63; 30 = 0.5 s at 60 Hz).", 30);
    p.AddFloatOption("prepass-radius", "Denoised mode: NRD diffuse pre-accumulation blur radius in pixels (0 = off; biases lighting gradients).", 0.0f);
    p.AddFloatOption("blur-radius", "Denoised mode: NRD maximum spatial blur radius in pixels (shrinks as history accumulates).", 30.0f);
    p.AddFlag("no-antifirefly", "Denoised mode: disable NRD's anti-firefly filter.");
    p.AddFlag("validation-overlay", "Denoised mode: let NRD render its validation layer (view it with --view validation).");
    p.AddFlag("reset-on-source-change", "Denoised mode: reset the temporal history whenever an emitter switches (circuit change).");
    p.AddIntOption("blackout-at-frame", "Test hook: before this frame, switch every emitter off and reset the history (-1 = never).", -1);
    p.AddStringOption("capture-sequence", "Directory that receives one cropped display PNG per selected frame plus sequence.json (event log).", "");
    p.AddIntOption("capture-from", "First frame of the sequence capture.", 0);
    p.AddIntOption("capture-to", "Last frame of the sequence capture (0 = until exit).", 0);
    p.AddIntOption("capture-every", "Frame stride of the sequence capture (>= 1).", 1);
    p.AddStringOption("capture-crop", "x,y,w,h crop of the sequence frames in pixels (default: the full image).", "");
    p.AddFloatOption("benchmark-seconds", "With --replay: loop the replay for this many measured seconds and write --report.", 0.0f);
    p.AddFloatOption("warmup-seconds", "Benchmark: excluded warm-up time before measuring.", 5.0f);
    p.AddStringOption("report", "Benchmark report JSON file (required with --benchmark-seconds).", "");

    ParsedOptions result;
    result.usage = p.Usage("LastCircuit.exe",
                           "Last Circuit: custom D3D12 engine with mandatory hardware path tracing (M4: stable real-time image).");

    if (const auto error = p.Parse(args)) {
        result.error = *error;
        return result;
    }

    AppOptions o;
    o.help = p.Has("help");
    o.scene = p.GetString("scene");
    if (const std::string s = p.GetString("scene-file"); !s.empty()) {
        o.sceneFile = std::filesystem::path(s);
        if (p.Has("scene")) {
            result.error = "--scene-file replaces --scene; give one of them";
            return result;
        }
        o.scene = "two_room";  // Scene files describe the two-room proof's entities.
    }
    o.reloadTest = p.Has("reload-test");

    const int width = p.GetInt("width");
    const int height = p.GetInt("height");
    if (width < 16 || width > 16384 || height < 16 || height > 16384) {
        result.error = std::format("--width and --height must be between 16 and 16384 (got {}x{})", width, height);
        return result;
    }
    o.width = static_cast<std::uint32_t>(width);
    o.height = static_cast<std::uint32_t>(height);

    if (const std::string s = p.GetString("internal"); !s.empty()) {
        std::uint32_t wh[2] = {};
        if (!ParseUnsignedList(s, 'x', wh) || wh[0] < 16 || wh[0] > 16384 || wh[1] < 16 || wh[1] > 16384) {
            result.error = std::format("--internal expects WxH between 16 and 16384 (got '{}')", s);
            return result;
        }
        o.internalWidth = wh[0];
        o.internalHeight = wh[1];
    }

    const std::string mode = p.GetString("mode");
    if (mode == "diag") o.mode = AppRenderMode::Diagnostic;
    else if (mode == "raw") o.mode = AppRenderMode::Raw;
    else if (mode == "reference") o.mode = AppRenderMode::Reference;
    else if (mode == "denoised") o.mode = AppRenderMode::Denoised;
    else {
        result.error = std::format("unknown --mode '{}'; valid modes: diag, raw, reference, denoised", mode);
        return result;
    }

    const auto view = ViewModeFromName(p.GetString("view"));
    if (!view) {
        result.error = std::format("unknown --view '{}'; valid views: {}", p.GetString("view"), ViewModeList());
        return result;
    }
    o.view = *view;
    o.viewSet = p.Has("view");
    if (o.viewSet) {
        if (o.mode == AppRenderMode::Diagnostic && IsDenoisedOnlyView(o.view)) {
            result.error = std::format("--view {} exists only in --mode denoised", ViewModeName(o.view));
            return result;
        }
        if (o.mode == AppRenderMode::Denoised && IsDiagnosticOnlyView(o.view)) {
            result.error = std::format("--view {} exists only in --mode diag", ViewModeName(o.view));
            return result;
        }
        if (o.mode == AppRenderMode::Raw || o.mode == AppRenderMode::Reference) {
            result.error = "--view applies to --mode diag and --mode denoised only";
            return result;
        }
    }

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

    o.play = p.Has("play");
    o.noUi = p.Has("no-ui");
    if (const std::string s = p.GetString("record"); !s.empty()) o.record = std::filesystem::path(s);
    if (const std::string s = p.GetString("replay"); !s.empty()) o.replay = std::filesystem::path(s);
    o.stopAtTick = p.GetInt("stop-at-tick");
    o.mouseSensitivity = p.GetFloat("sensitivity");
    if (o.play && o.headless) {
        result.error = "--play needs a window; remove --headless";
        return result;
    }
    if (o.play && o.replay) {
        result.error = "--play and --replay are exclusive";
        return result;
    }
    if (o.record && !o.play) {
        result.error = "--record requires --play";
        return result;
    }
    if (o.stopAtTick >= 0 && !o.replay) {
        result.error = "--stop-at-tick requires --replay";
        return result;
    }
    if (o.stopAtTick < -1) {
        result.error = std::format("--stop-at-tick must be -1 or a tick count (got {})", o.stopAtTick);
        return result;
    }
    if (!(o.mouseSensitivity > 0.0f) || o.mouseSensitivity > 1.0f) {
        result.error = std::format("--sensitivity must be within (0, 1] radians per count (got {})", o.mouseSensitivity);
        return result;
    }

    const int historyFrames = p.GetInt("history-frames");
    if (historyFrames < 1 || historyFrames > 63) {
        result.error = std::format("--history-frames must be between 1 and 63 (got {})", historyFrames);
        return result;
    }
    o.historyFrames = static_cast<std::uint32_t>(historyFrames);
    o.prepassRadius = p.GetFloat("prepass-radius");
    o.blurRadius = p.GetFloat("blur-radius");
    if (o.prepassRadius < 0.0f || o.prepassRadius > 200.0f || o.blurRadius < 0.0f || o.blurRadius > 200.0f) {
        result.error = "--prepass-radius and --blur-radius must be within 0..200 pixels";
        return result;
    }
    o.antiFirefly = !p.Has("no-antifirefly");
    o.validationOverlay = p.Has("validation-overlay");
    o.resetOnSourceChange = p.Has("reset-on-source-change");
    o.blackoutAtFrame = p.GetInt("blackout-at-frame");
    if (o.blackoutAtFrame < -1) {
        result.error = std::format("--blackout-at-frame must be -1 or a frame index (got {})", o.blackoutAtFrame);
        return result;
    }
    if (o.mode != AppRenderMode::Denoised && (p.Has("history-frames") || p.Has("no-antifirefly") || p.Has("validation-overlay") ||
                                              p.Has("reset-on-source-change") || p.Has("prepass-radius") || p.Has("blur-radius"))) {
        result.error = "--history-frames, --prepass-radius, --blur-radius, --no-antifirefly, --validation-overlay, and --reset-on-source-change apply to --mode denoised only";
        return result;
    }

    if (const std::string s = p.GetString("capture-sequence"); !s.empty()) o.captureSequence = std::filesystem::path(s);
    const int captureFrom = p.GetInt("capture-from");
    const int captureTo = p.GetInt("capture-to");
    const int captureEvery = p.GetInt("capture-every");
    if (captureFrom < 0 || captureTo < 0 || captureEvery < 1) {
        result.error = "--capture-from and --capture-to must be zero or positive and --capture-every at least 1";
        return result;
    }
    if (captureTo != 0 && captureTo < captureFrom) {
        result.error = std::format("--capture-to ({}) is before --capture-from ({})", captureTo, captureFrom);
        return result;
    }
    o.captureFrom = static_cast<std::uint32_t>(captureFrom);
    o.captureTo = static_cast<std::uint32_t>(captureTo);
    o.captureEvery = static_cast<std::uint32_t>(captureEvery);
    if (const std::string s = p.GetString("capture-crop"); !s.empty()) {
        std::uint32_t crop[4] = {};
        if (!ParseUnsignedList(s, ',', crop) || crop[2] == 0 || crop[3] == 0) {
            result.error = std::format("--capture-crop expects x,y,w,h with w and h above zero (got '{}')", s);
            return result;
        }
        o.captureCrop = {crop[0], crop[1], crop[2], crop[3]};
    }
    if (!o.captureSequence && (p.Has("capture-from") || p.Has("capture-to") || p.Has("capture-every") || p.Has("capture-crop"))) {
        result.error = "--capture-from/--capture-to/--capture-every/--capture-crop require --capture-sequence";
        return result;
    }

    o.benchmarkSeconds = p.GetFloat("benchmark-seconds");
    o.warmupSeconds = p.GetFloat("warmup-seconds");
    if (const std::string s = p.GetString("report"); !s.empty()) o.report = std::filesystem::path(s);
    if (o.benchmarkSeconds < 0.0f || o.warmupSeconds < 0.0f) {
        result.error = "--benchmark-seconds and --warmup-seconds must be zero or positive";
        return result;
    }
    if (o.benchmarkSeconds > 0.0f) {
        if (!o.replay) {
            result.error = "--benchmark-seconds requires --replay (a fixed replay is the benchmark content)";
            return result;
        }
        if (!o.report) {
            result.error = "--benchmark-seconds requires --report <file.json>";
            return result;
        }
        if (o.stopAtTick >= 0) {
            result.error = "--benchmark-seconds cannot be combined with --stop-at-tick";
            return result;
        }
    } else if (o.report) {
        result.error = "--report requires --benchmark-seconds";
        return result;
    }

    result.options = std::move(o);
    return result;
}

}  // namespace lc
