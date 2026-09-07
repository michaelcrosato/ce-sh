#include "app/application.h"

#include "app/environment_report.h"
#include "core/build_info.h"
#include "core/clock.h"
#include "core/error.h"
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
        if (!options_.headless) {
            WindowDesc wd;
            wd.title = std::format(L"Last Circuit [DIAGNOSTIC: {}] {}", Utf8ToWide(std::string(ViewModeName(options_.view))),
                                   Utf8ToWide(options_.scene));
            wd.clientWidth = options_.width;
            wd.clientHeight = options_.height;
            window = std::make_unique<Window>(wd);
            swapChain = std::make_unique<gfx::SwapChain>(*device, queue, window->Handle(), window->ClientWidth(), window->ClientHeight(), options_.vsync);
        }

        const std::uint32_t initialWidth = window ? window->ClientWidth() : options_.width;
        const std::uint32_t initialHeight = window ? window->ClientHeight() : options_.height;
        Renderer renderer(*device, queue, initialWidth, initialHeight);
        renderer.SetScene(desc->scene);

        std::uint32_t targetFrames = options_.frames;
        if (targetFrames == 0 && options_.headless) {
            targetFrames = options_.validate ? 2 : 1;
        }
        if (options_.resizeTest) {
            targetFrames = kFramesPerResizeStep * (static_cast<std::uint32_t>(std::size(kResizeSteps)) + 1);
        }
        log::Info("Rendering scene '{}' view '{}' at {}x{}{}{}", desc->name, ViewModeName(options_.view), initialWidth, initialHeight,
                  options_.headless ? " (headless)" : "", targetFrames ? std::format(" for {} frames", targetFrames) : "");

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
            if (now - lastReport >= std::chrono::seconds(2) || (targetFrames != 0 && frameIndex == targetFrames)) {
                const double avgCpu = cpuAccumFrames ? cpuAccumMs / cpuAccumFrames : 0.0;
                log::Info("frame {}: CPU {:.3f} ms avg over {} frames | GPU {}", frameIndex, avgCpu, cpuAccumFrames, TimingsText(renderer.LastTimings()));
                if (window) {
                    window->SetTitle(std::format(L"Last Circuit [DIAGNOSTIC: {}] {} {}x{} | GPU frame {:.2f} ms (trace {:.2f}) | CPU {:.2f} ms",
                                                 Utf8ToWide(std::string(ViewModeName(options_.view))), Utf8ToWide(desc->name), renderer.Width(),
                                                 renderer.Height(), FindTiming(renderer.LastTimings(), "frame_gpu"),
                                                 FindTiming(renderer.LastTimings(), "trace"), avgCpu));
                }
                cpuAccumMs = 0.0;
                cpuAccumFrames = 0;
                lastReport = now;
            }

            if (options_.resizeTest && window) {
                if (frameIndex % kFramesPerResizeStep == kFramesPerResizeStep - 1) {
                    // Verify the previous step, then request the next size.
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
            // Post-run work: capture, validation, resize verdict.
            std::optional<CaptureImages> images;
            if (options_.capture || options_.validate) {
                images = renderer.Readback();
            }

            if (options_.capture && images) {
                CaptureMetadata meta;
                meta.scene = desc->name;
                meta.view = std::string(ViewModeName(options_.view));
                meta.frameIndex = frameIndex > 0 ? frameIndex - 1 : 0;
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
                meta.timings = renderer.LastTimings();
                const std::string base = std::format("{}_{}_f{}", desc->name, meta.view, meta.frameIndex);
                if (!WriteCapture(*options_.capture, base, *images, meta)) {
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
                    const std::uint32_t x = std::min(images->width - 1, static_cast<std::uint32_t>(e.u * static_cast<float>(images->width)));
                    const std::uint32_t y = std::min(images->height - 1, static_cast<std::uint32_t>(e.v * static_cast<float>(images->height)));
                    const gpu::HitInfoTexel& t = images->HitAt(x, y);
                    const bool frontFace = (t.flags & gpu::kHitFlagFrontFace) != 0;
                    bool ok = true;
                    if (e.expectedStableId == 0) {
                        ok = t.stableId == gpu::kMissId;
                    } else {
                        ok = t.stableId == e.expectedStableId && frontFace == e.expectFrontFace;
                    }
                    log::Info("expectation '{}' at pixel ({}, {}): stable id {} front {} -> {}", e.description, x, y,
                              t.stableId == gpu::kMissId ? std::string("miss") : std::to_string(t.stableId), frontFace ? "yes" : "no",
                              ok ? "PASS" : "FAIL");
                    if (!ok) ++problems;
                }

                std::uint64_t hits = 0;
                std::uint64_t facingMismatches = 0;
                std::uint64_t grazing = 0;
                for (const gpu::HitInfoTexel& t : images->hitInfo) {
                    if (t.stableId == gpu::kMissId) continue;
                    ++hits;
                    if ((t.flags & gpu::kHitFlagGrazing) != 0) {
                        ++grazing;  // Facing is ill-defined at grazing incidence; excluded from the check.
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

                device->DrainInfoQueue();
                const std::size_t errors = device->InfoQueueErrorCount();
                const std::size_t warnings = device->InfoQueueWarningCount();
                log::Info("debug layer: {} error(s), {} warning(s){}", errors, warnings, device->DebugLayerEnabled() ? "" : " (debug layer off)");
                problems += static_cast<int>(errors);

                if (problems == 0) {
                    log::Info("VALIDATION PASSED ({} expectations, {} layout fields, {} hit pixels)", desc->expectations.size(), probe.size(), hits);
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
