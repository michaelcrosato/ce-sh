#include "app/application.h"

#include "app/environment_report.h"
#include "core/build_info.h"
#include "core/clock.h"
#include "core/error.h"
#include "core/json_writer.h"
#include "core/log.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/graphics_queue.h"
#include "graphics/d3d12/swap_chain.h"
#include "platform/files.h"
#include "platform/window.h"
#include "render/capture.h"
#include "render/renderer.h"
#include "scene/builtin_scenes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
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
    }
    return RenderMode::Raw;
}

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

double ChannelAverage(const double v[3]) { return (v[0] + v[1] + v[2]) / 3.0; }

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
    return RunRender();
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
    // Scene first: a bad scene name is a usage error and needs no GPU.
    std::optional<SceneDescription> desc = BuildBuiltinScene(options_.scene);
    if (!desc) {
        std::string names;
        for (const std::string& n : BuiltinSceneNames()) names += (names.empty() ? "" : ", ") + n;
        log::Error("unknown scene '{}'; built-in scenes: {}", options_.scene, names);
        return kExitUsage;
    }
    desc->camera.horizontalFovRadians = math::DegreesToRadians(options_.horizontalFovDegrees);
    const RenderMode mode = ToRenderMode(options_.mode);

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
                                                                     : std::format("{}", RenderModeName(mode));
        if (!options_.headless) {
            WindowDesc wd;
            wd.title = std::format(L"Last Circuit [{}] {}", Utf8ToWide(modeLabel), Utf8ToWide(options_.scene));
            wd.clientWidth = options_.width;
            wd.clientHeight = options_.height;
            window = std::make_unique<Window>(wd);
            swapChain = std::make_unique<gfx::SwapChain>(*device, queue, window->Handle(), window->ClientWidth(), window->ClientHeight(), options_.vsync);
        }

        const std::uint32_t initialWidth = window ? window->ClientWidth() : options_.width;
        const std::uint32_t initialHeight = window ? window->ClientHeight() : options_.height;
        Renderer renderer(*device, queue, initialWidth, initialHeight);
        renderer.SetScene(desc->scene);
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

        std::uint32_t targetFrames = options_.frames;
        if (targetFrames == 0 && options_.headless && mode != RenderMode::Reference) {
            targetFrames = options_.validate ? 2 : 1;
        }
        if (options_.resizeTest) {
            targetFrames = kFramesPerResizeStep * (static_cast<std::uint32_t>(std::size(kResizeSteps)) + 1);
        }
        const bool stopAtTarget = mode == RenderMode::Reference && (options_.headless || options_.validate || options_.capture || options_.stats);
        log::Info("Rendering scene '{}' mode {}{} at {}x{}{}{}{}", desc->name, RenderModeName(mode),
                  mode == RenderMode::Diagnostic ? std::format(" view '{}'", ViewModeName(options_.view))
                                                 : std::format(" strategy {} max-hits {} seed {}", StrategyName(integrator.strategy), integrator.maxHits, integrator.seed),
                  initialWidth, initialHeight, options_.headless ? " (headless)" : "",
                  targetFrames ? std::format(" for {} frames", targetFrames) : "",
                  mode == RenderMode::Reference ? std::format(" until {} spp", options_.spp) : "");
        if (mode != RenderMode::Diagnostic) {
            log::Info("Emitters: {} active; exposure {}; jitter {}", renderer.SceneData() ? "(computed per frame)" : "none", integrator.exposure,
                      integrator.jitter ? "on" : "off");
        }

        std::uint32_t frameIndex = 0;
        std::uint32_t resizeStep = 0;
        bool resizeTestPassed = options_.resizeTest;
        bool deviceRemoved = false;
        bool aborted = false;
        double cpuAccumMs = 0.0;
        std::uint32_t cpuAccumFrames = 0;
        auto lastReport = std::chrono::steady_clock::now();

        try {
        while (true) {
            const auto frameStart = std::chrono::steady_clock::now();

            if (window) {
                window->PumpMessages();
                const WindowEvents& ev = window->Events();
                if (ev.closeRequested || ev.escapePressed) {
                    log::Info("Window close requested");
                    window->ClearEvents();
                    break;
                }
                if (ev.focusLost) log::Debug("Focus lost");
                if (ev.focusGained) log::Debug("Focus gained");
                if (ev.resized || swapChain->Width() != window->ClientWidth() || swapChain->Height() != window->ClientHeight()) {
                    if (!window->IsMinimized() && window->ClientWidth() > 0 && window->ClientHeight() > 0) {
                        swapChain->Resize(window->ClientWidth(), window->ClientHeight());
                        renderer.Resize(window->ClientWidth(), window->ClientHeight());
                    }
                }
                window->ClearEvents();
                if (window->IsMinimized() || window->ClientWidth() == 0 || window->ClientHeight() == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Never render to a zero-sized target.
                    continue;
                }
            }

            RenderSnapshot snapshot;
            snapshot.scene = &desc->scene;
            snapshot.camera = desc->camera;
            snapshot.frameIndex = frameIndex;
            snapshot.view = options_.view;

            renderer.BeginFrame();
            renderer.RecordTrace(snapshot);
            if (swapChain) {
                renderer.RecordCopyToBackBuffer(swapChain->CurrentBackBuffer(), swapChain->Width(), swapChain->Height());
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
            desc->scene.CommitRenderedFrame();
            ++frameIndex;

            const double cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
            cpuAccumMs += cpuMs;
            ++cpuAccumFrames;
            const auto now = std::chrono::steady_clock::now();
            const bool targetReached = stopAtTarget && renderer.SampleIndex() >= options_.spp;
            if (now - lastReport >= std::chrono::seconds(2) || (targetFrames != 0 && frameIndex == targetFrames) || targetReached) {
                const double avgCpu = cpuAccumFrames ? cpuAccumMs / cpuAccumFrames : 0.0;
                log::Info("frame {}: CPU {:.3f} ms avg over {} frames | GPU {}{}", frameIndex, avgCpu, cpuAccumFrames, TimingsText(renderer.LastTimings()),
                          mode == RenderMode::Reference ? std::format(" | {} spp accumulated", renderer.SampleIndex()) : "");
                if (window) {
                    window->SetTitle(std::format(L"Last Circuit [{}] {} {}x{} | GPU frame {:.2f} ms | CPU {:.2f} ms | {} spp",
                                                 Utf8ToWide(modeLabel), Utf8ToWide(desc->name), renderer.Width(), renderer.Height(),
                                                 FindTiming(renderer.LastTimings(), "frame_gpu"), avgCpu,
                                                 mode == RenderMode::Diagnostic ? 0u : std::max<std::uint32_t>(1, renderer.SampleIndex())));
                }
                cpuAccumMs = 0.0;
                cpuAccumFrames = 0;
                lastReport = now;
            }

            if (options_.resizeTest && window) {
                if (frameIndex % kFramesPerResizeStep == kFramesPerResizeStep - 1) {
                    if (resizeStep > 0) {
                        const ResizeStep& s = kResizeSteps[resizeStep - 1];
                        const bool followed = renderer.Width() == window->ClientWidth() && renderer.Height() == window->ClientHeight() &&
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

        if (aborted || deviceRemoved) {
            queue.DrainForShutdown();
            exitCode = kExitFailure;
        } else {
            queue.WaitIdle();
            renderer.CollectFinalTimings();
            log::Info("final frame GPU timings: {}", TimingsText(renderer.LastTimings()));

            std::optional<CaptureImages> images;
            if (options_.capture || options_.validate || options_.stats) {
                images = renderer.Readback();
            }

            if (options_.capture && images) {
                CaptureMetadata meta;
                meta.scene = desc->name;
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
                meta.outputWidth = swapChain ? swapChain->Width() : renderer.Width();
                meta.outputHeight = swapChain ? swapChain->Height() : renderer.Height();
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
                const std::string base = mode == RenderMode::Diagnostic
                                             ? std::format("{}_{}_f{}", desc->name, meta.view, meta.frameIndex)
                                             : std::format("{}_{}_{}_spp{}", desc->name, RenderModeName(mode), StrategyName(integrator.strategy), images->sampleCount);
                if (!WriteCapture(*options_.capture, base, *images, meta)) {
                    exitCode = kExitFailure;
                }
            }

            if (options_.stats && images) {
                JsonWriter j;
                j.BeginObject();
                j.Field("scene", desc->name);
                j.Field("mode", RenderModeName(mode));
                j.Field("strategy", StrategyName(integrator.strategy));
                j.Field("maxHits", integrator.maxHits);
                j.Field("seed", integrator.seed);
                j.Field("samplesPerPixel", images->sampleCount);
                j.Key("patches");
                j.BeginObject();
                for (const StatsPatch& patch : desc->statsPatches) {
                    const PixelPoint p = ProjectPoint(desc->camera, images->width, images->height, patch.point);
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

                for (const HitExpectation& e : desc->expectations) {
                    std::uint32_t x = 0;
                    std::uint32_t y = 0;
                    if (e.worldPoint) {
                        const PixelPoint p = ProjectPoint(desc->camera, images->width, images->height, *e.worldPoint);
                        if (!p.visible) {
                            log::Error("expectation '{}': point is not on screen", e.description);
                            ++problems;
                            continue;
                        }
                        x = p.x;
                        y = p.y;
                    } else {
                        x = std::min(images->width - 1, static_cast<std::uint32_t>(e.u * static_cast<float>(images->width)));
                        y = std::min(images->height - 1, static_cast<std::uint32_t>(e.v * static_cast<float>(images->height)));
                    }
                    const gpu::HitInfoTexel& t = images->HitAt(x, y);
                    const bool frontFace = (t.flags & gpu::kHitFlagFrontFace) != 0;
                    bool ok = true;
                    if (e.expectedStableId == 0) {
                        ok = t.stableId == gpu::kMissId;
                    } else {
                        ok = t.stableId == e.expectedStableId && (e.worldPoint || frontFace == e.expectFrontFace);
                    }
                    log::Info("expectation '{}' at pixel ({}, {}): stable id {}{} front {} -> {}", e.description, x, y,
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
                    for (const RadianceExpectation& r : desc->radianceExpectations) {
                        bool ok = false;
                        std::string detail;
                        switch (r.kind) {
                            case RadianceExpectation::Kind::ZeroImage: {
                                float maxAbs = 0.0f;
                                for (const float v : images->linear.pixels) maxAbs = std::max(maxAbs, std::fabs(v));
                                ok = maxAbs <= r.absoluteTolerance;
                                detail = std::format("max |radiance| {:.3e} (tolerance {:.1e})", maxAbs, r.absoluteTolerance);
                                break;
                            }
                            case RadianceExpectation::Kind::PositivePatch: {
                                const PixelPoint p = ProjectPoint(desc->camera, images->width, images->height, r.point);
                                if (!p.visible) {
                                    detail = "patch is not on screen";
                                    break;
                                }
                                const PatchStats s = ComputePatchStats(*images, p.x, p.y, r.halfSize);
                                const double lum = 0.2126 * s.mean[0] + 0.7152 * s.mean[1] + 0.0722 * s.mean[2];
                                ok = lum > r.minimum;
                                detail = std::format("pixel ({}, {}) mean luminance {:.5f} (minimum {:.1e})", p.x, p.y, lum, r.minimum);
                                break;
                            }
                            case RadianceExpectation::Kind::AnalyticPatch: {
                                const PixelPoint p = ProjectPoint(desc->camera, images->width, images->height, r.point);
                                if (!p.visible) {
                                    detail = "patch is not on screen";
                                    break;
                                }
                                const PatchStats s = ComputePatchStats(*images, p.x, p.y, r.halfSize);
                                const double mean = ChannelAverage(s.mean);
                                const double se = ChannelAverage(s.standardError);
                                const double tolerance = std::max(static_cast<double>(r.relativeTolerance) * r.expected, 3.0 * se);
                                ok = std::fabs(mean - r.expected) <= tolerance;
                                detail = std::format("pixel ({}, {}) mean {:.5f} expected {:.5f} (diff {:+.2f} %, se {:.2e}, tolerance {:.5f})", p.x, p.y, mean,
                                                     r.expected, (mean - r.expected) / r.expected * 100.0, se, tolerance);
                                break;
                            }
                            case RadianceExpectation::Kind::RatioGreaterThan: {
                                const PixelPoint a = ProjectPoint(desc->camera, images->width, images->height, r.point);
                                const PixelPoint b = ProjectPoint(desc->camera, images->width, images->height, r.otherPoint);
                                if (!a.visible || !b.visible) {
                                    detail = "a patch is not on screen";
                                    break;
                                }
                                const PatchStats sa = ComputePatchStats(*images, a.x, a.y, r.halfSize);
                                const PatchStats sb = ComputePatchStats(*images, b.x, b.y, r.halfSize);
                                const double ra = sa.mean[0] / std::max(1e-6, sa.mean[1] + sa.mean[2]);
                                const double rb = sb.mean[0] / std::max(1e-6, sb.mean[1] + sb.mean[2]);
                                ok = ra > r.ratioFactor * rb;
                                detail = std::format("R/(G+B) {:.4f} vs {:.4f} (required factor {:.2f})", ra, rb, r.ratioFactor);
                                break;
                            }
                        }
                        log::Info("radiance expectation '{}': {} -> {}", r.description, detail, ok ? "PASS" : "FAIL");
                        if (!ok) ++problems;
                    }
                }

                device->DrainInfoQueue();
                const std::size_t errors = device->InfoQueueErrorCount();
                const std::size_t warnings = device->InfoQueueWarningCount();
                log::Info("debug layer: {} error(s), {} warning(s){}", errors, warnings, device->DebugLayerEnabled() ? "" : " (debug layer off)");
                problems += static_cast<int>(errors);

                if (problems == 0) {
                    log::Info("VALIDATION PASSED ({} hit expectations, {} radiance expectations, {} layout fields, {} spp)", desc->expectations.size(),
                              desc->radianceExpectations.size(), probe.size(), images->sampleCount);
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
