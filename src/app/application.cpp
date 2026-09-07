#include "app/application.h"

#include "app/benchmark.h"
#include "app/environment_report.h"
#include "app/temporal_checks.h"
#include "audio/audio_system.h"
#include "core/build_info.h"
#include "core/clock.h"
#include "core/error.h"
#include "core/image_write.h"
#include "core/json_writer.h"
#include "core/log.h"
#include "game/replay.h"
#include "game/simulation.h"
#include "game/state_checks.h"
#include "game/world.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/graphics_queue.h"
#include "graphics/d3d12/swap_chain.h"
#include "platform/files.h"
#include "platform/window.h"
#include "render/capture.h"
#include "render/renderer.h"
#include "scene/builtin_scenes.h"
#include "scene/scene_file.h"
#include "scene/two_room_level.h"
#include "ui/ui.h"

#define PSAPI_VERSION 2
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <thread>

namespace lc {

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitUnsupported = 3;

struct ResizeStep {
    std::uint32_t width;
    std::uint32_t height;
};
constexpr ResizeStep kResizeSteps[] = {{640, 360}, {1920, 1080}, {800, 600}, {1280, 720}};
constexpr std::uint32_t kFramesPerResizeStep = 6;

std::string TimingsText(const std::vector<gfx::TimerResult>& timings) {
    std::string text;
    for (const gfx::TimerResult& t : timings) {
        if (!text.empty()) text += ", ";
        text += std::format("{} {:.3f} ms", t.name, t.milliseconds);
    }
    return text.empty() ? "no GPU timings yet" : text;
}

double FindTiming(const std::vector<gfx::TimerResult>& timings, const char* name) {
    for (const gfx::TimerResult& t : timings) {
        if (t.name == name) return t.milliseconds;
    }
    return 0.0;
}

RenderMode ToRenderMode(AppRenderMode mode) {
    switch (mode) {
        case AppRenderMode::Diagnostic: return RenderMode::Diagnostic;
        case AppRenderMode::Raw: return RenderMode::Raw;
        case AppRenderMode::Reference: return RenderMode::Reference;
        case AppRenderMode::Denoised: return RenderMode::Denoised;
    }
    return RenderMode::Raw;
}

// Documented tolerance for the denoised image against an analytic patch (docs/TESTS.md, T12):
// the denoiser is biased by design; the recomposed raw mean is held to the reference tolerance.
constexpr float kDenoisedRelativeTolerance = 0.05f;

Strategy ToStrategy(AppStrategy s) {
    switch (s) {
        case AppStrategy::Mis: return Strategy::Mis;
        case AppStrategy::Light: return Strategy::Light;
        case AppStrategy::Bsdf: return Strategy::Bsdf;
    }
    return Strategy::Mis;
}

struct PixelPoint {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    bool visible = false;
};

PixelPoint ProjectPoint(const Camera& camera, std::uint32_t width, std::uint32_t height, math::Vec3 point) {
    PixelPoint p;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const auto uv = camera.ProjectToImage(aspect, point);
    if (!uv || uv->x < 0.0f || uv->x >= 1.0f || uv->y < 0.0f || uv->y >= 1.0f) {
        return p;
    }
    p.x = std::min(width - 1, static_cast<std::uint32_t>(uv->x * static_cast<float>(width)));
    p.y = std::min(height - 1, static_cast<std::uint32_t>(uv->y * static_cast<float>(height)));
    p.visible = true;
    return p;
}

PixelPoint PixelFromUv(std::uint32_t width, std::uint32_t height, float u, float v) {
    PixelPoint p;
    p.x = std::min(width - 1, static_cast<std::uint32_t>(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width)));
    p.y = std::min(height - 1, static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(height)));
    p.visible = true;
    return p;
}

double ChannelAverage(const double v[3]) { return (v[0] + v[1] + v[2]) / 3.0; }
double Luminance(const double v[3]) { return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]; }

// Live input to a simulation tick. Mouse deltas are consumed by the first tick of a frame.
game::InputFrame InputFromWindow(const Window& window, float sensitivity, bool invertY, bool consumeEdges) {
    const RawInputState& raw = window.Input();
    game::InputFrame f;
    if (raw.keyDown['W']) f.moveZ += 1.0f;
    if (raw.keyDown['S']) f.moveZ -= 1.0f;
    if (raw.keyDown['D']) f.moveX += 1.0f;
    if (raw.keyDown['A']) f.moveX -= 1.0f;
    f.sprint = raw.keyDown[VK_SHIFT];
    if (consumeEdges) {
        f.lookDx = -raw.mouseDx * sensitivity;  // Mouse right turns right (toward +X at yaw 0).
        f.lookDy = (invertY ? raw.mouseDy : -raw.mouseDy) * sensitivity;  // Mouse up looks up unless inverted.
        f.interactPressed = raw.keyPressed['E'];
        f.lampPressed = raw.keyPressed['F'];
    }
    return f;
}

bool InputIsActive(const game::InputFrame& f) {
    return f.moveX != 0.0f || f.moveZ != 0.0f || f.lookDx != 0.0f || f.lookDy != 0.0f || f.interactPressed || f.lampPressed;
}

// Player-facing prompt for the current interaction target (spec §14: a clear prompt): the world
// names the action from the door state, the item, or the socket.
std::string PromptFor(const game::World&, const game::InteractionTarget& target) {
    return target.text.empty() ? "Use " + target.name : target.text;
}

// Every emitter instance is a sound source whose hum follows its emitter state and transform.
struct AudioFixture {
    InstanceId instance;
    std::uint32_t material = 0;
    std::string name;
    audio::ClipId hum = audio::ClipId::FixtureHum;
};

std::vector<AudioFixture> CollectAudioFixtures(const Scene& scene, std::uint32_t lampMaterial) {
    std::vector<AudioFixture> out;
    for (const Instance& inst : scene.Instances()) {
        const Material& m = scene.Materials()[inst.materialIndex];
        if (m.type != MaterialType::Emitter) continue;
        AudioFixture f;
        f.instance = inst.id;
        f.material = inst.materialIndex;
        f.name = inst.name;
        f.hum = inst.materialIndex == lampMaterial ? audio::ClipId::LampHum
                : inst.name.find("emergency") != std::string::npos ? audio::ClipId::EmergencyHum
                                                                     : audio::ClipId::FixtureHum;
        out.push_back(f);
    }
    return out;
}

// The world as the sound rules see it (spec §15: actual world state, never display data).
audio::WorldSnapshot AudioSnapshotOf(game::World& world, const std::vector<AudioFixture>& fixtures) {
    audio::WorldSnapshot s;
    s.playerFeet = world.GetPlayer().Current().position;
    for (const game::Door& door : world.Doors()) {
        audio::DoorSnapshot d;
        d.moving = door.IsMoving();
        d.closed = door.State() == game::DoorState::Closed;
        d.position = door.Handle().centre;
        s.doors.push_back(d);
    }
    for (const game::Item& item : world.Items()) {
        audio::ItemSnapshot it;
        it.held = item.State() == game::ItemState::Held;
        it.position = item.Current().position + math::Vec3{0.0f, kLampBaseOffset + 0.05f, 0.0f};
        s.items.push_back(it);
        if (item.HasLight()) {
            s.lampOn = item.IsOn();
            s.lampHeld = it.held;
            s.lampPosition = it.position;
        }
    }
    for (const game::Fan& fan : world.Fans()) {
        audio::FanSnapshot f;
        f.position = fan.Level().centre;
        f.speedFraction = fan.SpeedFraction();
        s.fans.push_back(f);
    }
    s.threatPosition = world.GetThreat().Current().position;
    const math::Vec3 eye = world.GetPlayer().EyePosition(world.GetPlayer().Current());
    s.threatOccluded = !world.Colliders().SegmentClear(eye, s.threatPosition + math::Vec3{0.0f, 1.0f, 0.0f});
    const Scene& scene = world.GetScene();
    for (const AudioFixture& f : fixtures) {
        const Instance* inst = scene.FindInstance(f.instance);
        if (inst == nullptr) continue;
        audio::FixtureSnapshot fs;
        fs.name = f.name;
        fs.position = inst->objectToWorld.TransformPoint({0.0f, 0.0f, 0.0f});
        fs.on = scene.Materials()[f.material].emitterOn;
        fs.hum = f.hum;
        s.fixtures.push_back(fs);
    }
    return s;
}

// Evaluates one radiance-style expectation on the readback. Returns the pass/fail and a detail line.
bool EvaluateRadianceExpectation(const RadianceExpectation& r, const Camera& camera, const CaptureImages& images, std::string& detail) {
    switch (r.kind) {
        case RadianceExpectation::Kind::ZeroImage: {
            float maxAbs = 0.0f;
            for (const float v : images.linear.pixels) maxAbs = std::max(maxAbs, std::fabs(v));
            detail = std::format("max |radiance| {:.3e} (tolerance {:.1e})", maxAbs, r.absoluteTolerance);
            return maxAbs <= r.absoluteTolerance;
        }
        case RadianceExpectation::Kind::PositivePatch: {
            const PixelPoint p = ProjectPoint(camera, images.width, images.height, r.point);
            if (!p.visible) {
                detail = "patch is not on screen";
                return false;
            }
            const PatchStats s = ComputePatchStats(images, p.x, p.y, r.halfSize);
            const double lum = Luminance(s.mean);
            detail = std::format("pixel ({}, {}) mean luminance {:.5f} (minimum {:.1e})", p.x, p.y, lum, r.minimum);
            return lum > r.minimum;
        }
        case RadianceExpectation::Kind::AnalyticPatch: {
            const PixelPoint p = ProjectPoint(camera, images.width, images.height, r.point);
            if (!p.visible) {
                detail = "patch is not on screen";
                return false;
            }
            const PatchStats s = ComputePatchStats(images, p.x, p.y, r.halfSize);
            const double mean = ChannelAverage(s.mean);
            const double se = ChannelAverage(s.standardError);
            const double tolerance = std::max(static_cast<double>(r.relativeTolerance) * r.expected, 3.0 * se);
            detail = std::format("pixel ({}, {}) mean {:.5f} expected {:.5f} (diff {:+.2f} %, se {:.2e}, tolerance {:.5f})", p.x, p.y, mean,
                                 r.expected, (mean - r.expected) / r.expected * 100.0, se, tolerance);
            return std::fabs(mean - r.expected) <= tolerance;
        }
        case RadianceExpectation::Kind::RatioGreaterThan: {
            const PixelPoint a = ProjectPoint(camera, images.width, images.height, r.point);
            const PixelPoint b = ProjectPoint(camera, images.width, images.height, r.otherPoint);
            if (!a.visible || !b.visible) {
                detail = "a patch is not on screen";
                return false;
            }
            const PatchStats sa = ComputePatchStats(images, a.x, a.y, r.halfSize);
            const PatchStats sb = ComputePatchStats(images, b.x, b.y, r.halfSize);
            const double ra = sa.mean[0] / std::max(1e-6, sa.mean[1] + sa.mean[2]);
            const double rb = sb.mean[0] / std::max(1e-6, sb.mean[1] + sb.mean[2]);
            detail = std::format("R/(G+B) {:.4f} vs {:.4f} (required factor {:.2f})", ra, rb, r.ratioFactor);
            return ra > r.ratioFactor * rb;
        }
    }
    detail = "unknown expectation kind";
    return false;
}

}  // namespace

Application::Application(AppOptions options) : options_(std::move(options)) {}

int Application::Run() {
    log::Init(options_.logFile ? &*options_.logFile : nullptr);
#if !defined(NDEBUG)
    log::SetMinLevel(log::Level::Debug);
#endif
    log::Info("Last Circuit v{} build {}{} ({})", build::kVersion, build::kGitCommit, build::kGitDirty ? "-dirty" : "", build::kConfig);

    if (options_.listAdapters) {
        return RunListAdapters();
    }
    if (options_.simulateOnly) {
        return RunSimulateOnly();
    }
    return RunRender();
}

int Application::RunSimulateOnly() {
    // Spec §15 / T14: the rules run without any renderer. The same ticks, checks, and hash as the
    // rendered runs; a GPU run with --expect-state-hash must reproduce this hash.
    SetAssetRoot(files::ExecutableDirectory() / "assets");
    const std::filesystem::path levelFile = options_.sceneFile ? *options_.sceneFile : std::filesystem::path("scenes") / (options_.scene + ".json");
    SceneFileResult loaded = LoadSceneFile(levelFile, AssetRoot());
    if (!loaded.Ok()) {
        log::Error("scene file '{}' is invalid ({} problem(s)):", loaded.sourceName, loaded.errors.size());
        for (const std::string& e : loaded.errors) log::Error("  {}", e);
        return kExitUsage;
    }
    std::string error;
    const std::optional<game::Replay> replay = game::Replay::Load(*options_.replay, error);
    if (!replay) {
        log::Error("{}", error);
        return kExitUsage;
    }
    const game::ThreatBehaviour behaviour = replay->hunt ? game::ThreatBehaviour::Hunt : game::ThreatBehaviour::Patrol;
    game::World world(std::move(*loaded.level), behaviour);
    game::StateCheckLog checks(*replay);
    const std::uint64_t ticks = options_.stopAtTick >= 0 ? static_cast<std::uint64_t>(options_.stopAtTick) : replay->LastTick() + 1;
    game::Simulation simulation(replay->tickRate);
    log::Info("simulate-only: replay '{}' for {} ticks at {} Hz, threat {}, scene hash {:016x}", options_.replay->string(), ticks, replay->tickRate,
              game::ThreatBehaviourName(behaviour), loaded.contentHash);
    simulation.RunTicks(static_cast<std::uint32_t>(ticks), [&](std::uint64_t tick, float dt) {
        world.Tick(replay->InputAt(tick), dt);
        for (const std::string& e : world.TakeEvents()) log::Info("{}", e);
        checks.AfterTick(world, tick + 1);
    });
    for (const std::string& line : checks.Lines()) log::Info("{}", line);
    const math::Vec3 feet = world.GetPlayer().Current().position;
    const PoseSpec threat = world.GetThreat().Current();
    log::Info("simulate-only: {} ticks; objective {}, threat {} at ({:.2f}, {:.2f}, {:.2f}), catches {}, restarts {}, player at ({:.2f}, {:.2f}, {:.2f}), "
              "lamp {} ({}), door {}; state hash {:016x}",
              simulation.Tick(), world.Phase(), game::ThreatStateName(world.GetThreat().State()), threat.position.x, threat.position.y,
              threat.position.z, world.Catches(), world.Restarts(), feet.x, feet.y, feet.z, world.GetLamp().IsOn() ? "on" : "off",
              world.GetLamp().State() == game::LampState::Held ? "held" : world.GetLamp().SocketName(), game::DoorStateName(world.GetDoor().State()),
              world.StateHash());
    std::printf("STATE HASH %016llx\n", static_cast<unsigned long long>(world.StateHash()));
    // A run stopped early (--stop-at-tick) leaves the later checks unevaluated on purpose.
    const std::size_t beyond = checks.PendingBeyond(simulation.Tick());
    const std::size_t missed = checks.Pending() - beyond;
    int problems = static_cast<int>(checks.Failed() + missed);
    if (missed != 0) log::Error("{} state check(s) never reached their tick", missed);
    if (beyond != 0) log::Info("{} state check(s) lie beyond the stop tick {} and were not evaluated", beyond, simulation.Tick());
    if (options_.expectStateHash) {
        const bool same = *options_.expectStateHash == world.StateHash();
        log::Info("state hash {:016x} expected {:016x} -> {}", world.StateHash(), *options_.expectStateHash, same ? "PASS" : "FAIL");
        if (!same) ++problems;
    }
    if (options_.validate || options_.expectStateHash) {
        if (problems == 0) {
            log::Info("VALIDATION PASSED ({} state check(s))", checks.Evaluated());
            std::printf("VALIDATION PASSED\n");
            return kExitOk;
        }
        log::Error("VALIDATION FAILED: {} problem(s)", problems);
        std::printf("VALIDATION FAILED: %d problem(s)\n", problems);
        return kExitFailure;
    }
    return kExitOk;
}

int Application::RunListAdapters() {
    if (options_.debugLayer) {
        gfx::Device::EnableDebugLayer(options_.gpuValidation);
    }
    const std::vector<gfx::AdapterInfo> adapters = gfx::Device::EnumerateAdapters();
    const EnvironmentInfo env = CollectEnvironment(adapters, nullptr);
    LogEnvironment(env);
    std::printf("Adapters (high-performance order):\n");
    bool anySupported = false;
    for (const gfx::AdapterInfo& a : adapters) {
        std::printf("  %s%s\n", a.Summary().c_str(), a.SupportsLastCircuit() ? "  [SUPPORTED]" : "");
        anySupported = anySupported || a.SupportsLastCircuit();
    }
    if (options_.envReport) {
        files::WriteTextFile(*options_.envReport, EnvironmentToJson(env));
        log::Info("Environment report written: {}", options_.envReport->string());
    }
    if (!anySupported) {
        std::printf("No adapter supports hardware ray tracing (DXR Tier 1.1) with Shader Model 6.5.\n");
        return kExitUnsupported;
    }
    return kExitOk;
}

int Application::RunRender() {
    // The approved asset root sits beside the executable (spec §16, §17: nothing is fetched during play).
    SetAssetRoot(files::ExecutableDirectory() / "assets");

    // Scene first: a bad scene name or file is a usage error and needs no GPU.
    const bool simulated = options_.play || options_.replay.has_value();
    std::optional<SceneDescription> staticDesc;
    std::unique_ptr<game::World> world;
    std::optional<game::Replay> replay;
    game::Replay recording;
    // The scene file behind the two-room level (the built-in name or --scene-file); reloads re-read it.
    std::filesystem::path levelFile = kTwoRoomSceneFile;
    std::uint64_t sceneContentHash = 0;
    auto loadLevel = [&](std::optional<TwoRoomLevel>& out) -> bool {
        SceneFileResult r = LoadSceneFile(levelFile, AssetRoot());
        if (!r.Ok()) {
            log::Error("scene file '{}' is invalid ({} problem(s)):", r.sourceName, r.errors.size());
            for (const std::string& e : r.errors) log::Error("  {}", e);
            return false;
        }
        sceneContentHash = r.contentHash;
        out = std::move(*r.level);
        log::Info("scene file '{}' loaded: content hash {:016x}, {} instances, {} materials", r.sourceName, r.contentHash,
                  out->description.scene.Instances().size(), out->description.scene.Materials().size());
        return true;
    };
    if (options_.sceneFile) {
        levelFile = *options_.sceneFile;
    }
    // A scene name that is a file in the asset root (scenes/<name>.json) is a level; built-in names stay generated.
    if (!options_.sceneFile) {
        const std::filesystem::path named = std::filesystem::path("scenes") / (options_.scene + ".json");
        std::error_code ec;
        if (std::filesystem::exists(AssetRoot() / named, ec)) levelFile = named;
    }
    const bool fileLevel = options_.sceneFile.has_value() || levelFile != std::filesystem::path(kTwoRoomSceneFile) || options_.scene == "two_room";
    std::optional<TwoRoomLevel> level;
    if (fileLevel && !loadLevel(level)) {
        return kExitUsage;
    }
    // The threat's behaviour: replays carry it (the visual tests keep the deterministic path); play uses --threat.
    game::ThreatBehaviour threatBehaviour = options_.threat == "hunt" ? game::ThreatBehaviour::Hunt : game::ThreatBehaviour::Patrol;
    if (simulated) {
        if (!fileLevel) {
            log::Error("--play and --replay drive the 'two_room' scene (or a --scene-file); got '{}'", options_.scene);
            return kExitUsage;
        }
        if (options_.replay) {
            std::string error;
            replay = game::Replay::Load(*options_.replay, error);
            if (!replay) {
                log::Error("{}", error);
                return kExitUsage;
            }
            threatBehaviour = replay->hunt ? game::ThreatBehaviour::Hunt : game::ThreatBehaviour::Patrol;
            if (!replay->scene.empty() && replay->scene != level->description.name) {
                log::Error("replay '{}' was recorded for scene '{}', not '{}'", options_.replay->string(), replay->scene, level->description.name);
                return kExitUsage;
            }
        }
        world = std::make_unique<game::World>(std::move(*level), threatBehaviour);
        level.reset();
        recording.scene = world->Level().description.name;
        recording.seed = options_.seed;
    } else if (fileLevel) {
        staticDesc = level->description;  // The level rendered statically (threat parked, lamp on its socket).
        level.reset();
        staticDesc->camera.horizontalFovRadians = math::DegreesToRadians(options_.horizontalFovDegrees);
    } else {
        staticDesc = BuildBuiltinScene(options_.scene);
        if (!staticDesc) {
            std::string names;
            for (const std::string& n : BuiltinSceneNames()) names += (names.empty() ? "" : ", ") + n;
            log::Error("unknown scene '{}'; built-in scenes: {}", options_.scene, names);
            return kExitUsage;
        }
        staticDesc->camera.horizontalFovRadians = math::DegreesToRadians(options_.horizontalFovDegrees);
    }
    if (options_.reloadTest && !world) {
        log::Error("--reload-test needs a scene file level (--scene two_room or --scene-file) with --replay or --play");
        return kExitUsage;
    }
    const RenderMode mode = ToRenderMode(options_.mode);
    Scene* scenePtr = world ? &world->GetScene() : &staticDesc->scene;
    const std::string sceneName = world ? world->Level().description.name : staticDesc->name;

    // Device.
    gfx::DeviceOptions deviceOptions;
    deviceOptions.debugLayer = options_.debugLayer;
    deviceOptions.gpuValidation = options_.gpuValidation;
    deviceOptions.adapterIndex = options_.adapterIndex;
    std::unique_ptr<gfx::Device> device;
    try {
        device = std::make_unique<gfx::Device>(deviceOptions);
    } catch (const UnsupportedHardware& e) {
        log::Error("Unsupported hardware: {}", e.what());
        const EnvironmentInfo env = CollectEnvironment(gfx::Device::EnumerateAdapters(), nullptr);
        if (options_.envReport) {
            files::WriteTextFile(*options_.envReport, EnvironmentToJson(env));
        }
        return kExitUnsupported;
    }

    const EnvironmentInfo env = CollectEnvironment(device->AllAdapters(), device.get());
    LogEnvironment(env);
    if (options_.envReport) {
        if (files::WriteTextFile(*options_.envReport, EnvironmentToJson(env))) {
            log::Info("Environment report written: {}", options_.envReport->string());
        }
    }

    int exitCode = kExitOk;
    {
        gfx::GraphicsQueue queue(*device);
        std::unique_ptr<Window> window;
        std::unique_ptr<gfx::SwapChain> swapChain;
        const std::string modeLabel = mode == RenderMode::Diagnostic ? std::format("DIAGNOSTIC: {}", ViewModeName(options_.view))
                                      : (mode == RenderMode::Denoised && options_.viewSet) ? std::format("denoised [{}]", ViewModeName(options_.view))
                                                                                             : std::format("{}", RenderModeName(mode));
        if (!options_.headless) {
            WindowDesc wd;
            wd.title = std::format(L"Last Circuit [{}] {}", Utf8ToWide(modeLabel), Utf8ToWide(sceneName));
            wd.clientWidth = options_.width;
            wd.clientHeight = options_.height;
            window = std::make_unique<Window>(wd);
            swapChain = std::make_unique<gfx::SwapChain>(*device, queue, window->Handle(), window->ClientWidth(), window->ClientHeight(), options_.vsync);
            if (options_.play) {
                window->EnableRawMouse();
                window->CaptureCursor(true);
            }
        }
        // On-screen interface (spec §14 prompts, §15 cues, §16 diagnostic panel): never in a benchmark
        // or with --no-ui. Drawn into the back buffer after the present copy; the linear output is untouched.
        std::unique_ptr<ui::Ui> ui;
        if (window && !options_.noUi && options_.benchmarkSeconds <= 0.0f) {
            ui = std::make_unique<ui::Ui>(*window, *device, queue, swapChain->Format());
        }
        ui::Settings uiSettings;
        uiSettings.mouseSensitivity = options_.mouseSensitivity;
        uiSettings.horizontalFovDegrees = options_.horizontalFovDegrees;
        uiSettings.exposure = options_.exposure;
        ui::Overlay overlay;
        overlay.introCard = options_.play && ui != nullptr;
        bool paused = false;
        bool showDiagnostics = false;
        bool menuReload = false;
        std::string lastReloadText;
        double lastCpuMs = 0.0;
        // Sound (spec §15): generated clips through miniaudio in windowed runs; the rules (attenuation,
        // pan, occlusion through the collision solids, state-following) live in audio::Director.
        std::unique_ptr<audio::AudioSystem> audio;
        audio::Director audioDirector;
        std::vector<AudioFixture> audioFixtures;
        if (window && world && !options_.noAudio) {
            audio = std::make_unique<audio::AudioSystem>(true);
            audioFixtures = CollectAudioFixtures(*scenePtr, world->Level().lampMaterial);
            log::Info("audio: {} emitter fixture(s) with hums", audioFixtures.size());
        }

        const std::uint32_t initialWidth = window ? window->ClientWidth() : options_.width;
        const std::uint32_t initialHeight = window ? window->ClientHeight() : options_.height;
        const bool scaledOutput = options_.internalWidth != 0 && options_.internalHeight != 0;
        Renderer renderer(*device, queue, scaledOutput ? options_.internalWidth : initialWidth, scaledOutput ? options_.internalHeight : initialHeight);
        if (scaledOutput) {
            renderer.SetOutputSize(initialWidth, initialHeight);
        }
        renderer.SetScene(*scenePtr);
        renderer.SetMode(mode);
        IntegratorSettings integrator;
        integrator.maxHits = options_.maxHits;
        integrator.strategy = ToStrategy(options_.strategy);
        integrator.exposure = options_.exposure;
        integrator.seed = options_.seed;
        integrator.jitter = options_.jitter;
        integrator.samplesPerFrame = options_.samplesPerFrame;
        integrator.targetSamples = mode == RenderMode::Reference ? options_.spp : 0;
        renderer.SetIntegrator(integrator);
        DenoiserSettings denoiserSettings;
        denoiserSettings.historyFrames = options_.historyFrames;
        denoiserSettings.diffusePrepassRadius = options_.prepassRadius;
        denoiserSettings.maxBlurRadius = options_.blurRadius;
        denoiserSettings.antiFirefly = options_.antiFirefly;
        denoiserSettings.validationOverlay = options_.validationOverlay;
        denoiserSettings.resetOnSourceChange = options_.resetOnSourceChange;
        renderer.SetDenoiser(denoiserSettings);
        const std::uint32_t tickRate = replay ? replay->tickRate : 60;

        // Simulation setup.
        game::Simulation simulation(replay ? replay->tickRate : 60);
        const bool frozenReplay = replay && options_.stopAtTick >= 0;
        // World-state checks run right after their tick, in every replay mode (T14/T15).
        std::unique_ptr<game::StateCheckLog> stateChecks;
        if (replay && world) stateChecks = std::make_unique<game::StateCheckLog>(*replay);
        auto afterTick = [&](std::uint64_t tick) {
            for (const std::string& e : world->TakeEvents()) log::Info("{}", e);
            if (stateChecks) stateChecks->AfterTick(*world, tick + 1);
        };
        if (world && frozenReplay) {
            const auto stopTick = static_cast<std::uint32_t>(options_.stopAtTick);
            simulation.RunTicks(stopTick, [&](std::uint64_t tick, float dt) {
                world->Tick(replay->InputAt(tick), dt);
                afterTick(tick);
            });
            log::Info("replay '{}': advanced {} ticks; door {}, lamp {} ({}), threat at ({:.2f}, {:.2f}, {:.2f}), player at ({:.2f}, {:.2f}, {:.2f})",
                      options_.replay->string(), stopTick, game::DoorStateName(world->GetDoor().State()),
                      world->GetLamp().IsOn() ? "on" : "off", world->GetLamp().State() == game::LampState::Held ? "held" : world->GetLamp().SocketName(),
                      world->GetThreat().Current().position.x, world->GetThreat().Current().position.y, world->GetThreat().Current().position.z,
                      world->GetPlayer().Current().position.x, world->GetPlayer().Current().position.y, world->GetPlayer().Current().position.z);
        }

        std::uint32_t targetFrames = options_.frames;
        if (targetFrames == 0 && options_.headless && mode != RenderMode::Reference) {
            targetFrames = mode == RenderMode::Denoised ? 32 : (options_.validate ? 2 : 1);  // Denoised: a bounded history to look at.
        }
        if (options_.resizeTest) {
            targetFrames = kFramesPerResizeStep * (static_cast<std::uint32_t>(std::size(kResizeSteps)) + 1);
        }
        if (replay && !frozenReplay && targetFrames == 0 && options_.headless) {
            targetFrames = static_cast<std::uint32_t>(replay->LastTick() + 1);
        }
        const bool stopAtTarget = mode == RenderMode::Reference && (options_.headless || options_.validate || options_.capture || options_.stats);
        log::Info("Rendering scene '{}' mode {}{} at {}x{}{}{}{}{}", sceneName, RenderModeName(mode),
                  mode == RenderMode::Diagnostic ? std::format(" view '{}'", ViewModeName(options_.view))
                                                 : std::format(" strategy {} max-hits {} seed {}", StrategyName(integrator.strategy), integrator.maxHits, integrator.seed),
                  initialWidth, initialHeight, options_.headless ? " (headless)" : "",
                  targetFrames ? std::format(" for {} frames", targetFrames) : "",
                  mode == RenderMode::Reference ? std::format(" until {} spp", options_.spp) : "",
                  world ? (options_.play ? " [live play]" : frozenReplay ? " [replay, frozen]" : " [replay, one tick per frame]") : "");

        std::uint32_t frameIndex = 0;
        std::uint32_t resizeStep = 0;
        bool resizeTestPassed = options_.resizeTest;
        bool deviceRemoved = false;
        bool aborted = false;
        std::uint64_t motionFrames = 0;  // Frames in which the world changed a transform.
        double cpuAccumMs = 0.0;
        std::uint32_t cpuAccumFrames = 0;
        auto lastReport = std::chrono::steady_clock::now();
        auto lastFrameTime = std::chrono::steady_clock::now();
        Camera camera = world ? world->CameraAt(1.0f) : staticDesc->camera;
        camera.horizontalFovRadians = math::DegreesToRadians(options_.horizontalFovDegrees);
        std::string lastInteraction;

        // Per-frame readback for frame sequences (spec §18) and trail-lag checks (§19): a full GPU wait
        // per frame, test paths only. Motion checks need the transforms of the rendered image before
        // CommitRenderedFrame replaces the previous ones.
        struct TrailRecord {
            std::size_t checkIndex = 0;
            std::uint32_t entityId = 0;
            std::vector<double> luminance;
            std::vector<std::uint32_t> occupied;
            std::vector<std::uint64_t> frames;
        };
        std::vector<TrailRecord> trailRecords;
        bool keepRenderedInstances = false;
        const bool benchmark = options_.benchmarkSeconds > 0.0f;  // A benchmark never reads back per frame (spec §17: timing only).
        if (replay && world && !benchmark) {
            for (std::size_t i = 0; i < replay->checks.size(); ++i) {
                const game::ReplayCheck& c = replay->checks[i];
                if (game::IsPerFrameCheck(c) && !frozenReplay) {
                    trailRecords.push_back(TrailRecord{i, world->StableIdOf(c.entity), {}, {}, {}});
                }
                if (c.kind == "motion" || c.kind == "static_motion") keepRenderedInstances = true;
            }
        }
        const bool perFrameReadback = !benchmark && (options_.captureSequence.has_value() || !trailRecords.empty());
        std::vector<Instance> renderedInstances;
        JsonWriter sequenceLog;
        std::uint32_t sequenceFrames = 0;
        if (options_.captureSequence) {
            sequenceLog.BeginObject();
            sequenceLog.Field("scene", sceneName);
            sequenceLog.Field("mode", RenderModeName(mode));
            sequenceLog.Field("replay", options_.replay ? options_.replay->string() : std::string());
            sequenceLog.Key("crop");
            sequenceLog.BeginArray();
            sequenceLog.Value(options_.captureCrop.x);
            sequenceLog.Value(options_.captureCrop.y);
            sequenceLog.Value(options_.captureCrop.width);
            sequenceLog.Value(options_.captureCrop.height);
            sequenceLog.EndArray();
            sequenceLog.Key("frames");
            sequenceLog.BeginArray();
        }

        // Benchmark (spec §17 protocol): loop the replay, exclude the warm-up, record every frame's
        // CPU and GPU time, then write the report. No per-frame readback, no validation.
        std::uint32_t reloadCount = 0;
        std::uint32_t reloadFailures = 0;
        std::vector<double> benchCpuMs;
        std::vector<double> benchGpuMs;
        std::map<std::string, std::pair<double, std::uint32_t>> benchPasses;
        std::uint32_t benchWarmupFrames = 0;
        std::uint32_t replayLoops = 0;
        bool benchMeasuring = false;
        auto benchStart = std::chrono::steady_clock::now();
        auto measureStart = benchStart;
        if (benchmark) {
            targetFrames = 0;  // The measured duration ends the run.
            log::Info("benchmark: {} s warm-up then {} s measured; replay '{}' ({} ticks) loops", options_.warmupSeconds, options_.benchmarkSeconds,
                      options_.replay->string(), replay->LastTick() + 1);
        }

        try {
        while (true) {
            const auto frameStart = std::chrono::steady_clock::now();
            const double realSeconds = std::chrono::duration<double>(frameStart - lastFrameTime).count();
            lastFrameTime = frameStart;

            if (window) {
                window->PumpMessages();
                const WindowEvents& ev = window->Events();
                if (ev.closeRequested) {
                    log::Info("Window close requested");
                    window->ClearEvents();
                    break;
                }
                if (ev.escapePressed) {
                    if (options_.play && ui) {
                        paused = !paused;  // The menu releases the cursor; the simulation stops while it is open.
                        window->CaptureCursor(!paused);
                        log::Info("{}", paused ? "paused: menu open" : "resumed");
                    } else if (options_.play) {
                        window->CaptureCursor(!window->CursorCaptured());  // Escape pauses look/move by releasing the cursor.
                        log::Info("cursor {}", window->CursorCaptured() ? "captured" : "released (Escape again to capture)");
                    } else {
                        log::Info("Escape pressed: closing");
                        window->ClearEvents();
                        break;
                    }
                }
                if (ev.focusLost) {
                    log::Debug("Focus lost");
                    if (options_.play) {
                        window->CaptureCursor(false);
                        if (ui && !paused) {
                            paused = true;
                            log::Info("paused: focus lost");
                        }
                    }
                }
                if (ev.focusGained) log::Debug("Focus gained");
                if (ev.resized || swapChain->Width() != window->ClientWidth() || swapChain->Height() != window->ClientHeight()) {
                    if (!window->IsMinimized() && window->ClientWidth() > 0 && window->ClientHeight() > 0) {
                        swapChain->Resize(window->ClientWidth(), window->ClientHeight());
                        if (scaledOutput) {
                            renderer.SetOutputSize(window->ClientWidth(), window->ClientHeight());  // The internal size stays fixed.
                        } else {
                            renderer.Resize(window->ClientWidth(), window->ClientHeight());
                        }
                    }
                }
                window->ClearEvents();
                if (window->IsMinimized() || window->ClientWidth() == 0 || window->ClientHeight() == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Never render to a zero-sized target.
                    if (window) window->ClearInput();
                    continue;
                }
            }

            // Frame-level key commands consume their own edges; the game keys and mouse deltas stay in
            // the input record until a simulation tick reads them (frames outnumber ticks at high
            // refresh rates, so clearing per frame would drop presses and mouse motion).
            const bool diagnosticsKey = window && window->ConsumeKeyPressed(VK_F1);
            const bool reloadKey = window && options_.play && window->ConsumeKeyPressed('R');
            if (diagnosticsKey && ui) {
                showDiagnostics = !showDiagnostics;
            }
            bool clearInput = window != nullptr;
            bool restartedThisFrame = false;  // A catch or a menu restart: a camera cut for the temporal history.
            bool advancedThisFrame = false;   // The objective reached a new phase.

            // Simulation (spec §9 order: input, fixed-step ticks, interpolated render data).
            float alpha = 1.0f;
            bool worldChanged = false;
            if (world) {
                if (options_.play) {
                    const bool active = window && window->CursorCaptured() && window->HasFocus() && !paused;
                    if (active) {
                        bool first = true;
                        const auto step = simulation.Advance(realSeconds, [&](std::uint64_t tick, float dt) {
                            const game::InputFrame input = InputFromWindow(*window, uiSettings.mouseSensitivity, uiSettings.invertY, first);
                            first = false;
                            if (overlay.introCard && InputIsActive(input)) overlay.introCard = false;
                            world->Tick(input, dt);
                            restartedThisFrame = restartedThisFrame || world->JustRestarted();
                            advancedThisFrame = advancedThisFrame || world->JustAdvanced();
                            afterTick(tick);
                            if (options_.record) recording.Record(tick, input);
                        });
                        alpha = step.alpha;
                        clearInput = step.ticksRun > 0;  // Otherwise the record accumulates for the next tick.
                        if (step.clamped) log::Warn("simulation clamped a long frame ({:.3f} s)", realSeconds);
                    } else {
                        (void)simulation.Advance(0.0, [](std::uint64_t, float) {});  // Paused: no ticks, no catch-up.
                    }
                    const auto target = world->CurrentInteraction();
                    const std::string targetName = target ? target->name : "";
                    if (targetName != lastInteraction) {
                        if (!targetName.empty()) log::Info("[E] {}", targetName);
                        lastInteraction = targetName;
                    }
                    overlay.prompt = target ? PromptFor(*world, *target) : std::string();
                    const bool lampNear = world->GetLamp().State() == game::LampState::Held || targetName == "lamp";
                    overlay.hint = lampNear ? (world->GetLamp().IsOn() ? "Switch the lamp off" : "Switch the lamp on") : std::string();
                    overlay.objectiveLine = world->ObjectiveLine();
                    overlay.endCard = world->Complete();
                    if (world->CompletedSteps() > 0) overlay.introCard = false;
                } else if (!frozenReplay) {
                    if (benchmark && simulation.Tick() > replay->LastTick()) {
                        world->Reset();  // Back to the start: a camera cut, so the temporal history is invalid.
                        renderer.ResetHistory();
                        simulation = game::Simulation(tickRate);
                        if (audio) {
                            audio->StopAll();
                            audioDirector.Reset();
                        }
                        ++replayLoops;
                        log::Info("benchmark: replay loop {} restarts at frame {}", replayLoops, frameIndex);
                    }
                    simulation.RunTicks(1, [&](std::uint64_t tick, float dt) {
                        world->Tick(replay->InputAt(tick), dt);
                        restartedThisFrame = restartedThisFrame || world->JustRestarted();
                        advancedThisFrame = advancedThisFrame || world->JustAdvanced();
                        afterTick(tick);
                    });
                    alpha = 1.0f;
                }
                if (restartedThisFrame) {
                    renderer.ResetHistory();  // The player jumped to the checkpoint: a camera cut.
                    if (ui && options_.play) {
                        overlay.prompt.clear();
                    }
                }
                worldChanged = world->WriteRenderScene(*scenePtr, alpha);
                if (worldChanged) ++motionFrames;
                camera = world->CameraAt(alpha);
                camera.horizontalFovRadians = math::DegreesToRadians(ui ? uiSettings.horizontalFovDegrees : options_.horizontalFovDegrees);
                if (audio) {
                    // Sound follows the world state written above; the listener is the camera; occlusion
                    // asks the collision solids (the door leaf included) between the eye and the source.
                    const float audioDt = !options_.play ? 1.0f / static_cast<float>(tickRate) : static_cast<float>(realSeconds);
                    audio::WorldSnapshot audioSnapshot = AudioSnapshotOf(*world, audioFixtures);
                    audioSnapshot.objectiveChime = advancedThisFrame;
                    audioSnapshot.catchSting = restartedThisFrame && world->Catches() > 0;
                    const std::vector<audio::Command> commands = audioDirector.Update(audioSnapshot, audioDt);
                    audio::Listener listener;
                    listener.position = camera.position;
                    listener.forward = camera.Forward();
                    listener.right = camera.ViewToWorld().TransformDirection({1.0f, 0.0f, 0.0f});
                    audio->SetVolumes({uiSettings.masterVolume, uiSettings.effectsVolume, uiSettings.ambienceVolume});
                    audio->SetPaused(paused);
                    audio->Update(listener, commands, [&](math::Vec3 a, math::Vec3 b) { return !world->Colliders().SegmentClear(a, b); });
                    for (const std::string& cue : audio->TakeCues()) {
                        log::Debug("cue: {}", cue);
                        if (uiSettings.textCues) {
                            overlay.cue = cue;
                            overlay.cueSeconds = 2.5f;
                        }
                    }
                    overlay.cueSeconds = std::max(0.0f, overlay.cueSeconds - audioDt);
                }
            }
            if (clearInput) window->ClearInput();

            // Interface for this frame: prompts and cues while playing, the pause menu, the diagnostic
            // panel. Settings from the menu apply immediately (sensitivity, look inversion, field of
            // view, exposure); the volumes wait for the sound system.
            ui::MenuAction menuAction = ui::MenuAction::None;
            if (ui) {
                ui->BeginFrame();
                if (world && options_.play && !paused) ui->DrawOverlay(overlay);
                if (paused) menuAction = ui->DrawPauseMenu(uiSettings, showDiagnostics);
                if (showDiagnostics) {
                    ui::Diagnostics d;
                    d.mode = modeLabel;
                    d.gpuMs = FindTiming(renderer.LastTimings(), "frame_gpu");
                    d.cpuMs = lastCpuMs;
                    d.frame = frameIndex;
                    d.tick = simulation.Tick();
                    d.historyResets = renderer.HistoryResetCount();
                    d.denoiserDispatches = renderer.DenoiserDispatchCount();
                    for (const gfx::TimerResult& t : renderer.LastTimings()) d.passes.emplace_back(t.name, t.milliseconds);
                    if (world) {
                        d.door = game::DoorStateName(world->GetDoor().State());
                        d.lampOn = world->GetLamp().IsOn();
                        d.lamp = world->GetLamp().State() == game::LampState::Held ? "held" : world->GetLamp().SocketName();
                        const PoseSpec tp = world->GetThreat().Current();
                        d.threat = std::format("({:.2f}, {:.2f}, {:.2f}) along {:.1f} of {:.1f} m", tp.position.x, tp.position.y, tp.position.z,
                                               world->GetThreat().Distance(), world->GetThreat().PathLength());
                        d.threat += std::format(", {} [{}]", game::ThreatStateName(world->GetThreat().State()), game::ThreatBehaviourName(world->Behaviour()));
                        d.objective = std::format("{}: {} (catches {}, restarts {})", world->Phase(), world->ObjectiveLine(), world->Catches(), world->Restarts());
                        std::string circuits;
                        for (const CircuitState& c : world->Level().circuits) {
                            if (!c.poweredByItem.empty()) circuits += std::format("{}{} {}", circuits.empty() ? "" : ", ", c.id, world->CircuitOn(c.id) ? "on" : "off");
                        }
                        if (!circuits.empty()) d.objective += "  circuits: " + circuits;
                    }
                    d.sceneFile = levelFile.string();
                    d.sceneHash = sceneContentHash != 0 ? sceneContentHash : scenePtr->ContentHash();
                    d.lastReload = lastReloadText;
                    if (audio) {
                        const audio::AudioStats& st = audio->Stats();
                        d.audio = st.deviceReady ? std::format("{} voice(s) on '{}' at {} Hz, {} dropped", st.activeVoices, st.deviceName, st.sampleRate, st.droppedOneShots)
                                                 : "no output device (cues only)";
                    } else {
                        d.audio = "off";
                    }
                    ui->DrawDiagnostics(d);
                }
                if (uiSettings.exposure != integrator.exposure) {
                    integrator.exposure = uiSettings.exposure;
                    renderer.SetIntegrator(integrator);
                }
            }
            if (menuAction == ui::MenuAction::Quit) {
                log::Info("quit from the menu");
                break;
            }
            if (menuAction == ui::MenuAction::Resume || menuAction == ui::MenuAction::Restart || menuAction == ui::MenuAction::Reload) {
                paused = false;
                if (window && options_.play) window->CaptureCursor(true);
                log::Info("resumed from the menu");
            }
            if (menuAction == ui::MenuAction::Restart && world) {
                // Spec §15 restart command: the last checkpoint; after the route is complete, the whole route.
                if (world->Complete()) {
                    world->Reset();
                    simulation = game::Simulation(tickRate);
                } else {
                    world->RestartFromCheckpoint();
                }
                renderer.ResetHistory();  // A camera cut: the temporal history is invalid.
                if (audio) {
                    audio->StopAll();
                    audioDirector.Reset();
                }
                for (const std::string& e : world->TakeEvents()) log::Info("{}", e);
                log::Info("restart from the menu at frame {} (objective {})", frameIndex, world->Phase());
            }
            menuReload = menuAction == ui::MenuAction::Reload;

            // Scene reload (spec §16): parse and validate first; only a valid document replaces the world, at
            // this frame boundary after a full GPU wait, with the temporal history reset. R in play mode, the
            // menu, or the --reload-test hook at frame 2.
            const bool reloadRequested = world && (reloadKey || menuReload || (options_.reloadTest && frameIndex == 2));
            if (reloadRequested) {
                std::optional<TwoRoomLevel> fresh;
                if (loadLevel(fresh)) {
                    queue.WaitIdle();
                    world = std::make_unique<game::World>(std::move(*fresh), threatBehaviour);
                    scenePtr = &world->GetScene();
                    renderer.SetScene(*scenePtr);
                    renderer.ResetHistory();
                    simulation = game::Simulation(tickRate);
                    if (audio) {
                        audio->StopAll();
                        audioDirector.Reset();
                        audioFixtures = CollectAudioFixtures(*scenePtr, world->Level().lampMaterial);
                    }
                    ++reloadCount;
                    lastReloadText = std::format("ok at frame {} (reload {})", frameIndex, reloadCount);
                    log::Info("scene reloaded ({}): world reset, history reset, frame {}", reloadCount, frameIndex);
                } else {
                    log::Warn("scene reload refused: the current scene stays in place");
                    ++reloadFailures;
                    lastReloadText = std::format("REFUSED at frame {}: the file has problems (see the log)", frameIndex);
                }
            }

            // Test hook (spec §19, reset correctness): every emitter off and an explicit history reset.
            if (options_.blackoutAtFrame >= 0 && frameIndex == static_cast<std::uint32_t>(options_.blackoutAtFrame)) {
                std::uint32_t switched = 0;
                const std::vector<Material>& materials = scenePtr->Materials();
                for (std::uint32_t i = 0; i < materials.size(); ++i) {
                    if (materials[i].type == MaterialType::Emitter && materials[i].emitterOn) {
                        scenePtr->SetEmitterOn(i, false);
                        ++switched;
                    }
                }
                renderer.ResetHistory();
                log::Info("blackout before frame {}: {} emitter(s) switched off, history reset requested", frameIndex, switched);
            }

            RenderSnapshot snapshot;
            snapshot.scene = scenePtr;
            snapshot.camera = camera;
            snapshot.frameIndex = frameIndex;
            snapshot.view = options_.view;
            snapshot.overlay = options_.viewSet;
            // NRD's time delta: the replay period when the world is stepped by ticks, wall time in a window, a nominal 60 Hz otherwise.
            snapshot.frameDeltaMs = (world && !options_.play) ? 1000.0f / static_cast<float>(tickRate)
                                    : (window && frameIndex > 0) ? static_cast<float>(realSeconds * 1000.0)
                                                                 : 1000.0f / 60.0f;

            renderer.BeginFrame();
            renderer.RecordTrace(snapshot);
            const bool captureBackBufferNow = swapChain && options_.captureBackBuffer >= 0 && frameIndex == static_cast<std::uint32_t>(options_.captureBackBuffer);
            if (swapChain) {
                renderer.RecordCopyToBackBuffer(swapChain->CurrentBackBuffer(), swapChain->Width(), swapChain->Height());
                if (ui) {
                    renderer.RecordOverlay([&](ID3D12GraphicsCommandList4* list) { ui->Render(list, swapChain->CurrentBackBuffer()); });
                }
                if (captureBackBufferNow) {
                    renderer.RecordBackBufferReadback(swapChain->CurrentBackBuffer(), swapChain->Width(), swapChain->Height());
                }
            }
            renderer.EndFrame();
            if (swapChain) {
                const HRESULT hr = swapChain->Present();
                if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                    device->ReportDeviceRemoved();
                    deviceRemoved = true;
                    break;
                }
                LC_CHECK_HR(hr);
            }
            if (captureBackBufferNow) {
                // Evidence for the presented image: the back buffer as submitted, with its alpha channel.
                queue.WaitIdle();
                std::uint32_t bbWidth = 0;
                std::uint32_t bbHeight = 0;
                const std::vector<std::uint8_t> bb = renderer.TakeBackBufferReadback(bbWidth, bbHeight);
                std::size_t alphaBelow = 0;
                std::size_t allZero = 0;
                std::uint32_t minX = bbWidth, minY = bbHeight, maxX = 0, maxY = 0;
                ImageRgba8 colour{bbWidth, bbHeight, bb};
                ImageRgba8 alphaImage{bbWidth, bbHeight, std::vector<std::uint8_t>(bb.size())};
                for (std::uint32_t y = 0; y < bbHeight; ++y) {
                    for (std::uint32_t x = 0; x < bbWidth; ++x) {
                        const std::size_t i = (static_cast<std::size_t>(y) * bbWidth + x) * 4;
                        const std::uint8_t a = bb[i + 3];
                        if (a != 255) {
                            ++alphaBelow;
                            minX = std::min(minX, x);
                            minY = std::min(minY, y);
                            maxX = std::max(maxX, x);
                            maxY = std::max(maxY, y);
                        }
                        if (a == 0 && bb[i] == 0 && bb[i + 1] == 0 && bb[i + 2] == 0) ++allZero;
                        colour.pixels[i + 3] = 255;
                        alphaImage.pixels[i] = alphaImage.pixels[i + 1] = alphaImage.pixels[i + 2] = a;
                        alphaImage.pixels[i + 3] = 255;
                    }
                }
                std::error_code ec;
                std::filesystem::create_directories(*options_.capture, ec);
                const std::filesystem::path colourPath = *options_.capture / std::format("backbuffer_frame{}.png", frameIndex);
                const std::filesystem::path alphaPath = *options_.capture / std::format("backbuffer_frame{}_alpha.png", frameIndex);
                files::WriteBinaryFile(colourPath, EncodePng(colour));
                files::WriteBinaryFile(alphaPath, EncodePng(alphaImage));
                log::Info("back buffer frame {}: {}x{}, {} pixel(s) with alpha != 255 ({} fully zero){} -> {}", frameIndex, bbWidth, bbHeight, alphaBelow, allZero,
                          alphaBelow ? std::format(", bounding box x [{}..{}] y [{}..{}]", minX, maxX, minY, maxY) : std::string(), colourPath.string());
            }
            if (keepRenderedInstances) {
                renderedInstances = scenePtr->Instances();  // The image just rendered: current and previous transforms.
            }
            if (perFrameReadback) {
                const bool inSequence = options_.captureSequence && frameIndex >= options_.captureFrom &&
                                        (options_.captureTo == 0 || frameIndex <= options_.captureTo) &&
                                        (frameIndex - options_.captureFrom) % options_.captureEvery == 0;
                bool trailWanted = false;
                for (const TrailRecord& t : trailRecords) {
                    trailWanted = trailWanted || frameIndex >= replay->checks[t.checkIndex].fromFrame;
                }
                if (inSequence || trailWanted) {
                    const CaptureImages frameImages = renderer.Readback();
                    for (TrailRecord& t : trailRecords) {
                        const game::ReplayCheck& c = replay->checks[t.checkIndex];
                        if (frameIndex < c.fromFrame) continue;
                        PixelPoint p;
                        if (c.point) {
                            p = ProjectPoint(camera, frameImages.width, frameImages.height, *c.point);
                        } else if (c.pixel) {
                            p = PixelFromUv(frameImages.width, frameImages.height, c.pixel->x, c.pixel->y);
                        }
                        double lum = 0.0;
                        std::uint32_t occupied = 0;
                        if (p.visible) {
                            const PatchStats s = ComputePatchStats(frameImages, p.x, p.y, c.halfSize);
                            lum = Luminance(s.mean);
                            const std::uint32_t x0 = p.x >= c.halfSize ? p.x - c.halfSize : 0;
                            const std::uint32_t y0 = p.y >= c.halfSize ? p.y - c.halfSize : 0;
                            const std::uint32_t x1 = std::min(frameImages.width - 1, p.x + c.halfSize);
                            const std::uint32_t y1 = std::min(frameImages.height - 1, p.y + c.halfSize);
                            for (std::uint32_t py = y0; py <= y1; ++py) {
                                for (std::uint32_t px = x0; px <= x1; ++px) {
                                    if (frameImages.HitAt(px, py).stableId == t.entityId) ++occupied;
                                }
                            }
                        }
                        t.luminance.push_back(lum);
                        t.occupied.push_back(occupied);
                        t.frames.push_back(frameIndex);
                    }
                    if (inSequence) {
                        const ImageRgba8 crop = CropImage(frameImages.display, options_.captureCrop.x, options_.captureCrop.y,
                                                          options_.captureCrop.width, options_.captureCrop.height);
                        const std::string fileName = std::format("frame_{:05}.png", frameIndex);
                        if (!WriteSequenceFrame(*options_.captureSequence, fileName, crop)) exitCode = kExitFailure;
                        ++sequenceFrames;
                        sequenceLog.BeginObject();
                        sequenceLog.Field("frame", frameIndex);
                        sequenceLog.Field("file", fileName);
                        sequenceLog.Field("tick", static_cast<std::uint64_t>(simulation.Tick()));
                        sequenceLog.Field("historyResets", renderer.HistoryResetCount());
                        sequenceLog.Field("framesSinceReset", frameImages.framesSinceReset);
                        if (world) {
                            sequenceLog.Field("door", std::string(game::DoorStateName(world->GetDoor().State())));
                            sequenceLog.Field("lampOn", world->GetLamp().IsOn());
                            sequenceLog.Field("lamp", world->GetLamp().State() == game::LampState::Held ? std::string("held") : std::string(world->GetLamp().SocketName()));
                            const math::Vec3 tp = world->GetThreat().Current().position;
                            const math::Vec3 pp = world->GetPlayer().Current().position;
                            sequenceLog.Key("threat");
                            sequenceLog.BeginArray();
                            sequenceLog.Value(static_cast<double>(tp.x));
                            sequenceLog.Value(static_cast<double>(tp.y));
                            sequenceLog.Value(static_cast<double>(tp.z));
                            sequenceLog.EndArray();
                            sequenceLog.Key("player");
                            sequenceLog.BeginArray();
                            sequenceLog.Value(static_cast<double>(pp.x));
                            sequenceLog.Value(static_cast<double>(pp.y));
                            sequenceLog.Value(static_cast<double>(pp.z));
                            sequenceLog.EndArray();
                        }
                        sequenceLog.EndObject();
                    }
                }
            }
            scenePtr->CommitRenderedFrame();
            ++frameIndex;

            const double cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
            lastCpuMs = cpuMs;
            cpuAccumMs += cpuMs;
            ++cpuAccumFrames;
            const auto now = std::chrono::steady_clock::now();
            const bool targetReached = stopAtTarget && renderer.SampleIndex() >= options_.spp;
            if (now - lastReport >= std::chrono::seconds(2) || (targetFrames != 0 && frameIndex == targetFrames) || targetReached) {
                const double avgCpu = cpuAccumFrames ? cpuAccumMs / cpuAccumFrames : 0.0;
                log::Info("frame {}: CPU {:.3f} ms avg over {} frames | GPU {}{}{}", frameIndex, avgCpu, cpuAccumFrames, TimingsText(renderer.LastTimings()),
                          mode == RenderMode::Reference ? std::format(" | {} spp accumulated", renderer.SampleIndex()) : "",
                          world ? std::format(" | tick {} door {} lamp {} TLAS rebuilds {}", simulation.Tick(), game::DoorStateName(world->GetDoor().State()),
                                              world->GetLamp().IsOn() ? "on" : "off", renderer.TlasRebuildCount())
                                : "");
                if (window) {
                    window->SetTitle(std::format(L"Last Circuit [{}] {} {}x{} | GPU frame {:.2f} ms | CPU {:.2f} ms | {} spp{}",
                                                 Utf8ToWide(modeLabel), Utf8ToWide(sceneName), renderer.Width(), renderer.Height(),
                                                 FindTiming(renderer.LastTimings(), "frame_gpu"), avgCpu,
                                                 mode == RenderMode::Diagnostic ? 0u : std::max<std::uint32_t>(1, renderer.SampleIndex()),
                                                 world ? std::format(L" | tick {} | door {} | lamp {}", simulation.Tick(),
                                                                     Utf8ToWide(game::DoorStateName(world->GetDoor().State())),
                                                                     world->GetLamp().IsOn() ? L"on" : L"off")
                                                       : L""));
                }
                cpuAccumMs = 0.0;
                cpuAccumFrames = 0;
                lastReport = now;
            }

            if (options_.resizeTest && window) {
                if (frameIndex % kFramesPerResizeStep == kFramesPerResizeStep - 1) {
                    if (resizeStep > 0) {
                        const ResizeStep& s = kResizeSteps[resizeStep - 1];
                        // With a fixed internal size the presented (output) size must follow the window instead.
                        const std::uint32_t followW = scaledOutput ? renderer.OutputWidth() : renderer.Width();
                        const std::uint32_t followH = scaledOutput ? renderer.OutputHeight() : renderer.Height();
                        const bool followed = followW == window->ClientWidth() && followH == window->ClientHeight() &&
                                              swapChain->Width() == window->ClientWidth() && swapChain->Height() == window->ClientHeight();
                        const bool reached = window->ClientWidth() == s.width && window->ClientHeight() == s.height;
                        log::Info("resize step {}: requested {}x{}, client {}x{}, renderer {}x{}, swap chain {}x{} -> {}", resizeStep, s.width,
                                  s.height, window->ClientWidth(), window->ClientHeight(), renderer.Width(), renderer.Height(),
                                  swapChain->Width(), swapChain->Height(), followed ? (reached ? "PASS" : "PASS (OS clamped the size)") : "FAIL");
                        resizeTestPassed = resizeTestPassed && followed;
                    }
                    if (resizeStep < std::size(kResizeSteps)) {
                        window->SetClientSize(kResizeSteps[resizeStep].width, kResizeSteps[resizeStep].height);
                        ++resizeStep;
                    }
                }
            }

            device->DrainInfoQueue();  // No-op when the message callback is active.
            if (benchmark) {
                const auto nowBench = std::chrono::steady_clock::now();
                if (!benchMeasuring) {
                    ++benchWarmupFrames;
                    if (std::chrono::duration<double>(nowBench - benchStart).count() >= options_.warmupSeconds) {
                        benchMeasuring = true;
                        measureStart = nowBench;
                        log::Info("benchmark: warm-up done after {} frames; measuring for {} s", benchWarmupFrames, options_.benchmarkSeconds);
                    }
                } else {
                    benchCpuMs.push_back(cpuMs);
                    const double gpuMs = FindTiming(renderer.LastTimings(), "frame_gpu");  // Two frames old (KI-005); a distribution does not mind.
                    if (gpuMs > 0.0) benchGpuMs.push_back(gpuMs);
                    for (const gfx::TimerResult& t : renderer.LastTimings()) {
                        auto& acc = benchPasses[t.name];
                        acc.first += t.milliseconds;
                        ++acc.second;
                    }
                    if (std::chrono::duration<double>(nowBench - measureStart).count() >= options_.benchmarkSeconds) {
                        break;
                    }
                }
            }
            if (targetFrames != 0 && frameIndex >= targetFrames) {
                break;
            }
            if (targetReached) {
                log::Info("reference target reached: {} samples per pixel", renderer.SampleIndex());
                break;
            }
        }
        } catch (const std::exception& e) {
            log::Error("frame loop aborted after {} frames: {}", frameIndex, e.what());
            aborted = true;
            if (queue.IsDeviceRemoved()) {
                device->ReportDeviceRemoved();
                deviceRemoved = true;
            }
        }

        if (window && options_.play) {
            window->CaptureCursor(false);
        }
        if (options_.record && !aborted) {
            if (recording.Save(*options_.record)) {
                log::Info("Replay recorded: {} ({} segments, {} ticks)", options_.record->string(), recording.segments.size(), simulation.Tick());
            } else {
                exitCode = kExitFailure;
            }
        }

        if (aborted || deviceRemoved) {
            queue.DrainForShutdown();
            exitCode = kExitFailure;
        } else {
            queue.WaitIdle();
            renderer.CollectFinalTimings();
            log::Info("final frame GPU timings: {}", TimingsText(renderer.LastTimings()));
            if (world) {
                log::Info("motion frames {} of {}; TLAS rebuilds {}", motionFrames, frameIndex, renderer.TlasRebuildCount());
            }
            if (options_.captureSequence) {
                sequenceLog.EndArray();
                sequenceLog.Field("frameCount", sequenceFrames);
                sequenceLog.Field("historyResets", renderer.HistoryResetCount());
                sequenceLog.EndObject();
                std::error_code ec;
                std::filesystem::create_directories(*options_.captureSequence, ec);
                if (files::WriteTextFile(*options_.captureSequence / "sequence.json", sequenceLog.Text() + "\n")) {
                    log::Info("Frame sequence written: {} frame(s) in {} with sequence.json", sequenceFrames, options_.captureSequence->string());
                } else {
                    exitCode = kExitFailure;
                }
            }

            if (benchmark) {
                BenchmarkReport report;
                report.buildCommit = build::kGitCommit;
                report.buildDirty = build::kGitDirty;
                report.buildConfig = build::kConfig;
                report.scene = sceneName;
                report.sceneContentHash = sceneContentHash != 0 ? sceneContentHash : scenePtr->ContentHash();
                for (const auto& [name, hash] : renderer.ShaderHashes()) report.shaderHashes.push_back({name, hash});
                report.denoiserVersion = mode == RenderMode::Denoised ? std::format("nrd-reblur-{}", Renderer::DenoiserVersion()) : "none";
                report.replay = options_.replay->string();
                report.replayTicks = replay->LastTick() + 1;
                report.replayLoops = replayLoops;
                report.adapter = WideToUtf8(device->AdapterDetails().description);
                report.vendorId = device->AdapterDetails().vendorId;
                report.deviceId = device->AdapterDetails().deviceId;
                report.driver = device->AdapterDetails().driverVersion;
                report.os = std::format("{} ({})", env.osVersion, env.osDisplayVersion);
                report.mode = RenderModeName(mode);
                report.renderWidth = renderer.Width();
                report.renderHeight = renderer.Height();
                report.outputWidth = swapChain ? swapChain->Width() : renderer.OutputWidth();
                report.outputHeight = swapChain ? swapChain->Height() : renderer.OutputHeight();
                report.maxHits = integrator.maxHits;
                report.historyFrames = denoiserSettings.historyFrames;
                report.antiFirefly = denoiserSettings.antiFirefly;
                report.vsync = options_.vsync && swapChain != nullptr;
                report.frameCap = false;
                report.windowed = window != nullptr;
                report.overlay = options_.viewSet;
                report.warmupSeconds = std::chrono::duration<double>(measureStart - benchStart).count();
                report.warmupFrames = benchWarmupFrames;
                report.measuredSeconds = benchMeasuring ? std::chrono::duration<double>(std::chrono::steady_clock::now() - measureStart).count() : 0.0;
                report.cpu = ComputeFrameTimeStatistics(benchCpuMs);
                report.gpu = ComputeFrameTimeStatistics(benchGpuMs);
                for (const auto& [name, acc] : benchPasses) {
                    report.passNames.push_back(name);
                    report.passAverageMs.push_back(acc.second ? acc.first / acc.second : 0.0);
                }
                device->UpdateVideoMemoryInfo();
                report.videoMemoryBudget = device->Caps().videoMemoryBudget;
                report.videoMemoryUsage = device->Caps().videoMemoryCurrentUsage;
                PROCESS_MEMORY_COUNTERS pmc{};
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                    report.processWorkingSet = pmc.WorkingSetSize;
                    report.processPeakWorkingSet = pmc.PeakWorkingSetSize;
                }
                report.denoiserPoolBytes = renderer.DenoiserPoolBytes();
                report.timestamp = Clock::TimestampIso8601();
                log::Info("benchmark: {} measured frames over {:.1f} s ({} replay loop(s)); CPU avg {:.2f} / p95 {:.2f} / p99 {:.2f} / max {:.2f} ms; "
                          "GPU avg {:.2f} / p95 {:.2f} / p99 {:.2f} / max {:.2f} ms; frames above 33.3 ms: {} (GPU), above 50 ms: {} (GPU)",
                          report.cpu.count, report.measuredSeconds, replayLoops, report.cpu.averageMs, report.cpu.p95Ms, report.cpu.p99Ms, report.cpu.maxMs,
                          report.gpu.averageMs, report.gpu.p95Ms, report.gpu.p99Ms, report.gpu.maxMs, report.gpu.above33Ms, report.gpu.above50Ms);
                if (!benchMeasuring || report.cpu.count == 0) {
                    log::Error("benchmark: the run ended before any measured frame");
                    exitCode = kExitFailure;
                } else if (files::WriteTextFile(*options_.report, report.ToJson())) {
                    log::Info("Benchmark report written: {}", options_.report->string());
                } else {
                    exitCode = kExitFailure;
                }
            }

            std::optional<CaptureImages> images;
            if (options_.capture || options_.validate || options_.stats) {
                images = renderer.Readback();
            }
            std::optional<CaptureImages> rawView;  // Denoised mode: the recomposed raw mean in place of the linear image.
            if (images && mode == RenderMode::Denoised) {
                rawView = *images;
                rawView->linear = images->rawMean;
                rawView->standardError = images->rawStandardError;
            }

            if (options_.capture && images) {
                CaptureMetadata meta;
                meta.scene = sceneName;
                meta.view = mode == RenderMode::Diagnostic ? std::string(ViewModeName(options_.view)) : std::string(RenderModeName(mode));
                meta.mode = RenderModeName(mode);
                meta.strategy = StrategyName(integrator.strategy);
                meta.frameIndex = frameIndex > 0 ? frameIndex - 1 : 0;
                meta.sampleIndex = images->sampleCount;
                meta.seed = integrator.seed;
                meta.pathDepth = mode == RenderMode::Diagnostic ? 1 : integrator.maxHits;
                meta.exposure = integrator.exposure;
                meta.renderWidth = renderer.Width();
                meta.renderHeight = renderer.Height();
                meta.outputWidth = swapChain ? swapChain->Width() : renderer.OutputWidth();
                meta.outputHeight = swapChain ? swapChain->Height() : renderer.OutputHeight();
                meta.reconstruction = mode == RenderMode::Denoised ? std::format("nrd-reblur-{}", Renderer::DenoiserVersion()) : "none";
                meta.historyResets = renderer.HistoryResetCount();
                meta.sceneFile = fileLevel ? levelFile.generic_string() : std::string();
                meta.sceneContentHash = sceneContentHash != 0 ? sceneContentHash : scenePtr->ContentHash();
                meta.adapter = WideToUtf8(device->AdapterDetails().description);
                meta.driver = device->AdapterDetails().driverVersion;
                meta.buildCommit = build::kGitCommit;
                meta.buildDirty = build::kGitDirty;
                meta.buildConfig = build::kConfig;
                meta.timestamp = Clock::TimestampIso8601();
                meta.instanceCount = renderer.SceneData() ? renderer.SceneData()->InstanceCount() : 0;
                meta.triangleInstances = renderer.SceneData() ? renderer.SceneData()->TriangleCount() : 0;
                meta.emitterCount = renderer.SceneData() ? renderer.SceneData()->EmitterCount() : 0;
                meta.timings = renderer.LastTimings();
                const std::string tickSuffix = world ? std::format("_tick{}", simulation.Tick()) : "";
                const std::string base = mode == RenderMode::Diagnostic
                                             ? std::format("{}{}_{}_f{}", sceneName, tickSuffix, meta.view, meta.frameIndex)
                                             : std::format("{}{}_{}_{}_spp{}", sceneName, tickSuffix, RenderModeName(mode), StrategyName(integrator.strategy), images->sampleCount);
                if (!WriteCapture(*options_.capture, base, *images, meta)) {
                    exitCode = kExitFailure;
                }
            }

            const std::vector<StatsPatch>& statsPatches = world ? world->Level().description.statsPatches : staticDesc->statsPatches;
            if (options_.stats && images) {
                JsonWriter j;
                j.BeginObject();
                j.Field("scene", sceneName);
                j.Field("mode", RenderModeName(mode));
                j.Field("strategy", StrategyName(integrator.strategy));
                j.Field("maxHits", integrator.maxHits);
                j.Field("seed", integrator.seed);
                j.Field("samplesPerPixel", images->sampleCount);
                if (world) j.Field("tick", static_cast<std::uint64_t>(simulation.Tick()));
                j.Key("patches");
                j.BeginObject();
                for (const StatsPatch& patch : statsPatches) {
                    const PixelPoint p = ProjectPoint(camera, images->width, images->height, patch.point);
                    j.Key(patch.name);
                    j.BeginObject();
                    j.Field("visible", p.visible);
                    if (p.visible) {
                        const PatchStats s = ComputePatchStats(*images, p.x, p.y, patch.halfSize);
                        j.Field("x", p.x);
                        j.Field("y", p.y);
                        j.Field("pixels", s.pixels);
                        j.Key("mean");
                        j.BeginArray();
                        for (const double v : s.mean) j.Value(v);
                        j.EndArray();
                        j.Key("standardError");
                        j.BeginArray();
                        for (const double v : s.standardError) j.Value(v);
                        j.EndArray();
                    }
                    j.EndObject();
                }
                j.EndObject();
                j.EndObject();
                if (files::WriteTextFile(*options_.stats, j.Text() + "\n")) {
                    log::Info("Statistics written: {}", options_.stats->string());
                } else {
                    exitCode = kExitFailure;
                }
            }

            if (options_.validate && images) {
                int problems = 0;

                const std::vector<LayoutProbeEntry> probe = renderer.RunLayoutProbe();
                int probeOk = 0;
                for (const LayoutProbeEntry& e : probe) {
                    if (e.ok) {
                        ++probeOk;
                    } else {
                        log::Error("layout probe mismatch: {} expected 0x{:08X} got 0x{:08X}", e.name, e.expected, e.actual);
                        ++problems;
                    }
                }
                log::Info("layout probe: {}/{} fields match", probeOk, probe.size());

                // Static hit expectations (diagnostic scenes and the static two_room description).
                const std::vector<HitExpectation> noHits;
                const std::vector<HitExpectation>& hitExpectations = world ? noHits : staticDesc->expectations;
                for (const HitExpectation& e : hitExpectations) {
                    PixelPoint p;
                    if (e.worldPoint) {
                        p = ProjectPoint(camera, images->width, images->height, *e.worldPoint);
                        if (!p.visible) {
                            log::Error("expectation '{}': point is not on screen", e.description);
                            ++problems;
                            continue;
                        }
                    } else {
                        p = PixelFromUv(images->width, images->height, e.u, e.v);
                    }
                    const gpu::HitInfoTexel& t = images->HitAt(p.x, p.y);
                    const bool frontFace = (t.flags & gpu::kHitFlagFrontFace) != 0;
                    bool ok = true;
                    if (e.expectedStableId == 0) {
                        ok = t.stableId == gpu::kMissId;
                    } else {
                        ok = t.stableId == e.expectedStableId && (e.worldPoint || frontFace == e.expectFrontFace);
                    }
                    log::Info("expectation '{}' at pixel ({}, {}): stable id {}{} front {} -> {}", e.description, p.x, p.y,
                              t.stableId == gpu::kMissId ? std::string("miss") : std::to_string(t.stableId),
                              mode == RenderMode::Diagnostic ? "" : std::format(" after {} mirror bounce(s)", t.instanceIndex),
                              frontFace ? "yes" : "no", ok ? "PASS" : "FAIL");
                    if (!ok) ++problems;
                }

                if (mode == RenderMode::Diagnostic) {
                    std::uint64_t hits = 0;
                    std::uint64_t facingMismatches = 0;
                    std::uint64_t grazing = 0;
                    for (const gpu::HitInfoTexel& t : images->hitInfo) {
                        if (t.stableId == gpu::kMissId) continue;
                        ++hits;
                        if ((t.flags & gpu::kHitFlagGrazing) != 0) {
                            ++grazing;
                            continue;
                        }
                        const bool front = (t.flags & gpu::kHitFlagFrontFace) != 0;
                        const bool geometric = (t.flags & gpu::kHitFlagGeometricFacing) != 0;
                        if (front != geometric) ++facingMismatches;
                    }
                    log::Info("hit pixels: {} of {}; RayQuery facing vs geometric winding mismatches: {} ({} grazing pixels excluded)", hits,
                              images->hitInfo.size(), facingMismatches, grazing);
                    if (hits == 0) {
                        log::Error("no camera ray hit anything; the scene should be visible");
                        ++problems;
                    }
                    if (facingMismatches != 0) {
                        log::Error("front-face flag disagrees with the geometric normal on {} pixels (winding or handedness bug)", facingMismatches);
                        ++problems;
                    }
                } else {
                    const StatsCounters stats = renderer.ReadStats();
                    log::Info("invalid-value counters over {} spp: nan {}, inf {}, negative {}, zero-pdf {}", images->sampleCount, stats.nan,
                              stats.inf, stats.negative, stats.zeroPdf);
                    if (stats.Total() != 0) {
                        log::Error("the integrator produced invalid values");
                        ++problems;
                    }
                    const std::vector<RadianceExpectation> none;
                    const std::vector<RadianceExpectation>& radianceExpectations = world ? none : staticDesc->radianceExpectations;
                    if (mode == RenderMode::Denoised && options_.blackoutAtFrame >= 0) {
                        // Reset correctness (spec §19): after the blackout and the explicit history reset, no
                        // prior illumination may remain in the denoised image beyond two rendered frames.
                        float maxDenoised = 0.0f;
                        for (const float v : images->linear.pixels) maxDenoised = std::max(maxDenoised, std::fabs(v));
                        float maxRaw = 0.0f;
                        for (const float v : images->rawMean.pixels) maxRaw = std::max(maxRaw, std::fabs(v));
                        const std::uint32_t framesAfter = frameIndex > static_cast<std::uint32_t>(options_.blackoutAtFrame) ? frameIndex - static_cast<std::uint32_t>(options_.blackoutAtFrame) : 0;
                        const bool okDenoised = maxDenoised <= 1e-4f;
                        const bool okRaw = maxRaw <= 1e-6f;
                        log::Info("blackout reset check: {} frame(s) after the blackout, max denoised radiance {:.3e} (limit 1e-4), max raw mean {:.3e} (limit 1e-6), "
                                  "{} history reset(s), {} frame(s) since reset -> {}",
                                  framesAfter, maxDenoised, maxRaw, renderer.HistoryResetCount(), images->framesSinceReset, okDenoised && okRaw ? "PASS" : "FAIL");
                        if (!okDenoised || !okRaw) ++problems;
                        log::Info("scene radiance expectations skipped: the blackout switched every emitter off");
                    } else if (mode == RenderMode::Denoised) {
                        // The recomposed raw samples must meet the reference tolerances (the split and the
                        // demodulation are lossless); the denoised image gets its own documented tolerance.
                        const CaptureImages& rawImages = *rawView;
                        CaptureImages denoisedView = *images;
                        denoisedView.standardError.pixels.assign(denoisedView.standardError.pixels.size(), 0.0f);
                        log::Info("denoised mode: {} frame(s) since the last history reset, {} reset(s), NRD {} dispatch(es), {:.1f} MiB of pool textures",
                                  images->framesSinceReset, renderer.HistoryResetCount(), renderer.DenoiserDispatchCount(),
                                  static_cast<double>(renderer.DenoiserPoolBytes()) / (1024.0 * 1024.0));
                        for (const RadianceExpectation& r : radianceExpectations) {
                            std::string detail;
                            const bool okRaw = EvaluateRadianceExpectation(r, camera, rawImages, detail);
                            log::Info("raw-mean expectation '{}': {} -> {}", r.description, detail, okRaw ? "PASS" : "FAIL");
                            if (!okRaw) ++problems;
                            RadianceExpectation d = r;
                            d.relativeTolerance = std::max(r.relativeTolerance, kDenoisedRelativeTolerance);
                            std::string detailDenoised;
                            const bool okDenoised = EvaluateRadianceExpectation(d, camera, denoisedView, detailDenoised);
                            log::Info("denoised expectation '{}': {} -> {}", r.description, detailDenoised, okDenoised ? "PASS" : "FAIL");
                            if (!okDenoised) ++problems;
                        }
                    } else {
                        for (const RadianceExpectation& r : radianceExpectations) {
                            std::string detail;
                            const bool ok = EvaluateRadianceExpectation(r, camera, *images, detail);
                            log::Info("radiance expectation '{}': {} -> {}", r.description, detail, ok ? "PASS" : "FAIL");
                            if (!ok) ++problems;
                        }
                    }
                }

                // Replay image checks at the stop tick (the world-state checks have their own log
                // and run right after their tick, whether or not it is the stop tick).
                if (world && replay) {
                    const std::uint64_t tick = simulation.Tick();
                    int evaluated = 0;
                    std::size_t imageChecks = 0;
                    for (const game::ReplayCheck& c : replay->checks) {
                        if (game::IsStateCheck(c)) continue;
                        ++imageChecks;
                        if (c.tick != tick) continue;
                        ++evaluated;
                        bool ok = false;
                        std::string detail;
                        PixelPoint p;
                        if (c.point) {
                            p = ProjectPoint(camera, images->width, images->height, *c.point);
                        } else if (c.pixel) {
                            p = PixelFromUv(images->width, images->height, c.pixel->x, c.pixel->y);
                        }
                        if (c.kind == "hit") {
                            const std::uint32_t expected = world->StableIdOf(c.entity);
                            if (!p.visible) {
                                detail = "point is not on screen";
                            } else if (expected == 0) {
                                detail = std::format("unknown entity '{}'", c.entity);
                            } else {
                                const gpu::HitInfoTexel& t = images->HitAt(p.x, p.y);
                                ok = t.stableId == expected;
                                detail = std::format("pixel ({}, {}) first non-mirror hit id {} after {} mirror bounce(s); expected {} ('{}')", p.x, p.y,
                                                     t.stableId == gpu::kMissId ? std::string("miss") : std::to_string(t.stableId), t.instanceIndex, expected, c.entity);
                            }
                        } else if (c.kind == "not_visible") {
                            const std::uint32_t id = world->StableIdOf(c.entity);
                            std::uint64_t direct = 0;
                            for (const gpu::HitInfoTexel& t : images->hitInfo) {
                                if (t.stableId == id && t.instanceIndex == 0) ++direct;  // Zero mirror bounces = seen directly.
                            }
                            ok = id != 0 && direct == 0;
                            detail = std::format("'{}' (id {}) appears directly in {} pixel(s)", c.entity, id, direct);
                        } else if (c.kind == "motion" || c.kind == "static_motion") {
                            if (mode != RenderMode::Denoised) {
                                detail = "requires --mode denoised (guide motion is a denoised-mode output)";
                            } else if (!p.visible) {
                                detail = "point is not on screen";
                            } else {
                                const gpu::HitInfoTexel& t = images->HitAt(p.x, p.y);
                                const float* mv = images->MotionAt(p.x, p.y);
                                const math::Vec3 got{mv[0], mv[1], mv[2]};
                                const std::uint32_t expectedId = c.entity.empty() ? 0 : world->StableIdOf(c.entity);
                                if (!c.entity.empty() && expectedId == 0) {
                                    detail = std::format("unknown entity '{}'", c.entity);
                                } else if (t.stableId == gpu::kMissId || (expectedId != 0 && t.stableId != expectedId)) {
                                    detail = std::format("pixel ({}, {}) reports id {}{}", p.x, p.y,
                                                         t.stableId == gpu::kMissId ? std::string("miss") : std::to_string(t.stableId),
                                                         expectedId != 0 ? std::format(", expected {} ('{}')", expectedId, c.entity) : std::string());
                                } else if (c.kind == "static_motion") {
                                    ok = got.x == 0.0f && got.y == 0.0f && got.z == 0.0f;
                                    detail = std::format("pixel ({}, {}) id {} motion ({:.6f}, {:.6f}, {:.6f}) m; expected exactly zero", p.x, p.y, t.stableId,
                                                         got.x, got.y, got.z);
                                } else {
                                    const Instance* inst = nullptr;
                                    for (const Instance& i : renderedInstances) {
                                        if (i.id.value == t.stableId) inst = &i;
                                    }
                                    if (inst == nullptr) {
                                        detail = "no transform snapshot of the rendered frame";
                                    } else {
                                        // Rigid translation of the instance between the previous and this rendered image
                                        // (the test entities translate; no rotation-dependent point motion is exercised).
                                        math::Vec3 expected = inst->prevObjectToWorld.TransformPoint({0.0f, 0.0f, 0.0f}) -
                                                              inst->objectToWorld.TransformPoint({0.0f, 0.0f, 0.0f});
                                        if (c.reflected) {
                                            const math::Vec3 n = world->Level().mirrorNormal;
                                            expected = expected - n * (2.0f * math::Dot(expected, n));
                                        }
                                        const float err = math::Length(got - expected);
                                        ok = err <= c.tolerance;
                                        detail = std::format("pixel ({}, {}) id {} motion ({:.5f}, {:.5f}, {:.5f}) m, expected ({:.5f}, {:.5f}, {:.5f}){} (error {:.2e}, tolerance {:.1e})",
                                                             p.x, p.y, t.stableId, got.x, got.y, got.z, expected.x, expected.y, expected.z,
                                                             c.reflected ? " reflected in the mirror plane" : "", err, c.tolerance);
                                    }
                                }
                            }
                        } else if (c.kind == "trail_lag") {
                            const std::size_t checkIndex = static_cast<std::size_t>(&c - replay->checks.data());
                            const TrailRecord* record = nullptr;
                            for (const TrailRecord& t : trailRecords) {
                                if (t.checkIndex == checkIndex) record = &t;
                            }
                            if (mode != RenderMode::Denoised) {
                                detail = "requires --mode denoised";
                            } else if (record == nullptr || record->entityId == 0) {
                                detail = "no per-frame statistics (frozen replay, or unknown entity)";
                            } else {
                                TrailLagSettings settings;
                                settings.settleFraction = c.settleFraction;
                                settings.maxLagFrames = c.maxLagFrames;
                                const TrailLagResult r = EvaluateTrailLag(record->luminance, record->occupied, settings);
                                ok = r.valid && r.passed;
                                detail = r.detail;
                                std::string series;
                                const std::size_t first = r.valid && r.departureIndex > 5 ? r.departureIndex - 5 : 0;
                                const std::size_t last = std::min(record->frames.size(), r.valid ? r.departureIndex + 36 : record->frames.size());
                                for (std::size_t i = first; i < last; ++i) {
                                    series += std::format(" {}:{:.3e}/{}", record->frames[i], record->luminance[i], record->occupied[i]);
                                }
                                log::Info("trail series (frame:luminance/entity pixels), {} frames recorded:{}", record->frames.size(), series);
                            }
                        } else if (c.kind == "denoised_patch_positive" || c.kind == "denoised_patch_dark") {
                            const bool positive = c.kind == "denoised_patch_positive";
                            if (mode != RenderMode::Denoised) {
                                detail = "requires --mode denoised";
                            } else if (!p.visible) {
                                detail = "patch is not on screen";
                            } else {
                                const PatchStats s = ComputePatchStats(*images, p.x, p.y, c.halfSize);  // The denoised image.
                                const double lum = Luminance(s.mean);
                                ok = positive ? lum > c.minimum : lum < c.maximum;
                                detail = std::format("pixel ({}, {}) denoised mean luminance {:.5f} ({} {:.1e})", p.x, p.y, lum, positive ? "minimum" : "maximum",
                                                     positive ? c.minimum : c.maximum);
                            }
                        } else {
                            const CaptureImages& patchImages = rawView ? *rawView : *images;  // Raw kinds read the (raw) linear image.
                            RadianceExpectation r;
                            r.description = c.description;
                            r.point = c.point.value_or(math::Vec3{});
                            r.otherPoint = c.otherPoint.value_or(math::Vec3{});
                            r.halfSize = c.halfSize;
                            r.minimum = c.minimum;
                            r.absoluteTolerance = c.tolerance;
                            r.ratioFactor = c.ratioFactor;
                            if (c.kind == "patch_positive") {
                                r.kind = RadianceExpectation::Kind::PositivePatch;
                                ok = EvaluateRadianceExpectation(r, camera, patchImages, detail);
                            } else if (c.kind == "patch_dark") {
                                if (!p.visible) {
                                    detail = "patch is not on screen";
                                } else {
                                    const PatchStats s = ComputePatchStats(patchImages, p.x, p.y, c.halfSize);
                                    const double lum = Luminance(s.mean);
                                    ok = lum < c.maximum;
                                    detail = std::format("pixel ({}, {}) mean luminance {:.5f} (maximum {:.1e})", p.x, p.y, lum, c.maximum);
                                }
                            } else if (c.kind == "patch_zero") {
                                r.kind = RadianceExpectation::Kind::ZeroImage;
                                ok = EvaluateRadianceExpectation(r, camera, patchImages, detail);
                            } else if (c.kind == "patch_ratio") {
                                r.kind = RadianceExpectation::Kind::RatioGreaterThan;
                                ok = EvaluateRadianceExpectation(r, camera, patchImages, detail);
                            } else {
                                detail = "unknown check kind";
                            }
                        }
                        log::Info("replay check tick {} '{}' ({}): {} -> {}", c.tick, c.description, c.kind, detail, ok ? "PASS" : "FAIL");
                        if (!ok) ++problems;
                    }
                    log::Info("replay image checks evaluated at tick {}: {} (at other ticks, skipped: {})", tick, evaluated, imageChecks - static_cast<std::size_t>(evaluated));
                    if (!frozenReplay && motionFrames + 1 != renderer.TlasRebuildCount() && motionFrames != renderer.TlasRebuildCount()) {
                        log::Error("TLAS rebuilt {} times but the world changed transforms in {} frames", renderer.TlasRebuildCount(), motionFrames);
                        ++problems;
                    }
                }

                // World-state checks (evaluated at their ticks) and the rule-invariance hash (T14).
                if (stateChecks) {
                    for (const std::string& line : stateChecks->Lines()) log::Info("{}", line);
                    // A run stopped before the replay's end (--frames at an image check's tick) leaves the
                    // later state checks unevaluated on purpose; only checks inside the run count as missed.
                    const std::size_t beyond = stateChecks->PendingBeyond(simulation.Tick());
                    const std::size_t missed = stateChecks->Pending() - beyond;
                    if (missed != 0) log::Error("{} state check(s) never reached their tick", missed);
                    if (beyond != 0) log::Info("{} state check(s) lie beyond the stop tick {} and were not evaluated", beyond, simulation.Tick());
                    problems += static_cast<int>(stateChecks->Failed() + missed);
                }
                if (world && replay) {
                    log::Info("state hash {:016x} after {} ticks (objective {}, catches {}, restarts {})", world->StateHash(), simulation.Tick(),
                              world->Phase(), world->Catches(), world->Restarts());
                    if (options_.expectStateHash) {
                        const bool same = *options_.expectStateHash == world->StateHash();
                        log::Info("state hash expected {:016x} -> {}", *options_.expectStateHash, same ? "PASS" : "FAIL");
                        if (!same) ++problems;
                    }
                }

                if (options_.reloadTest) {
                    const bool ok = reloadCount == 1 && reloadFailures == 0 && renderer.HistoryResetCount() >= 2;
                    log::Info("reload test: {} reload(s), {} refusal(s), {} history reset(s), content hash {:016x} -> {}", reloadCount, reloadFailures,
                              renderer.HistoryResetCount(), sceneContentHash, ok ? "PASS" : "FAIL");
                    if (!ok) ++problems;
                }

                device->DrainInfoQueue();
                const std::size_t errors = device->InfoQueueErrorCount();
                const std::size_t warnings = device->InfoQueueWarningCount();
                log::Info("debug layer: {} error(s), {} warning(s){}", errors, warnings, device->DebugLayerEnabled() ? "" : " (debug layer off)");
                problems += static_cast<int>(errors);

                if (problems == 0) {
                    log::Info("VALIDATION PASSED ({} layout fields, {} spp)", probe.size(), images->sampleCount);
                    std::printf("VALIDATION PASSED\n");
                } else {
                    log::Error("VALIDATION FAILED: {} problem(s)", problems);
                    std::printf("VALIDATION FAILED: %d problem(s)\n", problems);
                    exitCode = kExitFailure;
                }
            }

            if (options_.resizeTest) {
                device->DrainInfoQueue();
                const bool ok = resizeTestPassed && resizeStep == std::size(kResizeSteps) && device->InfoQueueErrorCount() == 0;
                log::Info("RESIZE TEST {}", ok ? "PASSED" : "FAILED");
                std::printf("RESIZE TEST %s\n", ok ? "PASSED" : "FAILED");
                if (!ok) exitCode = kExitFailure;
            }
        }
    }

    device->DrainInfoQueue();
    if (device->InfoQueueErrorCount() != 0) {
        log::Error("{} debug-layer error(s) were reported during this run", device->InfoQueueErrorCount());
        exitCode = exitCode == kExitOk ? kExitFailure : exitCode;
    }
    device.reset();
    gfx::Device::ReportLiveObjects();
    return exitCode;
}

}  // namespace lc
