#include "ui/ui.h"

#include "core/error.h"
#include "core/log.h"
#include "graphics/d3d12/device.h"
#include "graphics/d3d12/graphics_queue.h"
#include "platform/win_error.h"
#include "platform/window.h"

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace lc::ui {

namespace {

constexpr std::uint32_t kSrvDescriptors = 16;      // Font atlas plus room for a few textures.
constexpr int kFramesInFlightForUi = 3;            // >= gfx::kFramesInFlight: the backend cycles its buffers.
constexpr float kPromptFontSize = 22.0f;
constexpr float kBodyFontSize = 17.0f;
constexpr float kTitleFontSize = 30.0f;

ImVec2 DisplaySize() { return ImGui::GetIO().DisplaySize; }

// A non-interactive, borderless window pinned at a screen position (pivot in [0,1]).
bool BeginPinned(const char* name, const ImVec2& position, const ImVec2& pivot, float backgroundAlpha) {
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(backgroundAlpha);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoMove;
    return ImGui::Begin(name, nullptr, flags);
}

// "[E]" key badge followed by the action text.
void KeyLine(const char* key, const std::string& text) {
    const ImVec4 badge{0.95f, 0.85f, 0.45f, 1.0f};
    ImGui::TextColored(badge, "[%s]", key);
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextUnformatted(text.c_str());
}

}  // namespace

struct Ui::Impl {
    Window* window = nullptr;
    ID3D12Device* device = nullptr;
    gfx::ComPtr<ID3D12DescriptorHeap> srvHeap;
    gfx::ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT srvIncrement = 0;
    std::vector<std::uint32_t> freeSrv;  // Indices still available in srvHeap.
    ImGuiContext* context = nullptr;
    bool frameOpen = false;
    float dpiScale = 1.0f;
    int capturingAction = -1;  // The action whose key the menu waits for (-1: none).

    static void AllocateSrv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        auto* self = static_cast<Impl*>(info->UserData);
        if (self->freeSrv.empty()) {
            throw Error("UI descriptor heap exhausted");
        }
        const std::uint32_t index = self->freeSrv.back();
        self->freeSrv.pop_back();
        cpu->ptr = self->srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + static_cast<SIZE_T>(index) * self->srvIncrement;
        gpu->ptr = self->srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + static_cast<UINT64>(index) * self->srvIncrement;
    }

    static void FreeSrv(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        auto* self = static_cast<Impl*>(info->UserData);
        const SIZE_T offset = cpu.ptr - self->srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
        self->freeSrv.push_back(static_cast<std::uint32_t>(offset / self->srvIncrement));
    }
};

Ui::Ui(Window& window, gfx::Device& device, gfx::GraphicsQueue& queue, DXGI_FORMAT backBufferFormat) : impl_(std::make_unique<Impl>()) {
    impl_->window = &window;
    impl_->device = device.Get();

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = kSrvDescriptors;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    LC_CHECK_HR(device.Get()->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&impl_->srvHeap)));
    impl_->srvHeap->SetName(L"UI SRV heap");
    impl_->srvIncrement = device.Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (std::uint32_t i = kSrvDescriptors; i > 0; --i) impl_->freeSrv.push_back(i - 1);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = 1;
    LC_CHECK_HR(device.Get()->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&impl_->rtvHeap)));
    impl_->rtvHeap->SetName(L"UI RTV heap");

    IMGUI_CHECKVERSION();
    impl_->context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // No imgui.ini beside the executable; layout is fixed by code.
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.03f, 0.04f, 0.82f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.08f, 0.06f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.12f, 0.07f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.16f, 0.09f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.40f, 0.27f, 0.12f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.55f, 0.36f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.60f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.75f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.18f, 0.16f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.22f, 0.16f, 1.0f);
    impl_->dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(window.Handle());
    style.ScaleAllSizes(impl_->dpiScale);
    style.FontScaleDpi = impl_->dpiScale;

    if (!ImGui_ImplWin32_Init(window.Handle())) {
        throw Error("ImGui_ImplWin32_Init failed");
    }
    ImGui_ImplDX12_InitInfo info;
    info.Device = device.Get();
    info.CommandQueue = queue.Get();
    info.NumFramesInFlight = kFramesInFlightForUi;
    info.RTVFormat = backBufferFormat;
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.UserData = impl_.get();
    info.SrvDescriptorHeap = impl_->srvHeap.Get();
    info.SrvDescriptorAllocFn = &Impl::AllocateSrv;
    info.SrvDescriptorFreeFn = &Impl::FreeSrv;
    if (!ImGui_ImplDX12_Init(&info)) {
        ImGui_ImplWin32_Shutdown();
        throw Error("ImGui_ImplDX12_Init failed");
    }
    window.SetMessageHook([](HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        return ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam) != 0;
    });
    log::Info("UI: Dear ImGui {} ({}), DPI scale {:.2f}, {} SRV descriptors", IMGUI_VERSION, IMGUI_VERSION_NUM, impl_->dpiScale, kSrvDescriptors);
}

Ui::~Ui() {
    if (impl_->window) impl_->window->SetMessageHook(nullptr);
    if (impl_->frameOpen) ImGui::EndFrame();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(impl_->context);
}

void Ui::BeginFrame(float uiScale) {
    if (impl_->frameOpen) ImGui::EndFrame();  // A frame that was never rendered (minimised window).
    ImGui::GetStyle().FontScaleMain = std::clamp(uiScale, game::kUiScaleMin, game::kUiScaleMax);
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    impl_->frameOpen = true;
}

bool Ui::CapturingKey() const { return impl_->capturingAction >= 0; }

void Ui::DrawOverlay(const Overlay& overlay) {
    const ImVec2 size = DisplaySize();
    const float s = impl_->dpiScale;
    if (!overlay.objectiveLine.empty()) {
        if (BeginPinned("##objective", ImVec2(18.0f * s, 18.0f * s), ImVec2(0.0f, 0.0f), 0.55f)) {
            ImGui::PushFont(nullptr, kBodyFontSize);
            ImGui::TextDisabled("OBJECTIVE");
            ImGui::TextUnformatted(overlay.objectiveLine.c_str());
            ImGui::PopFont();
        }
        ImGui::End();
    }
    if (!overlay.prompt.empty() || !overlay.hint.empty()) {
        if (BeginPinned("##prompt", ImVec2(size.x * 0.5f, size.y - 48.0f * s), ImVec2(0.5f, 1.0f), 0.6f)) {
            ImGui::PushFont(nullptr, kPromptFontSize);
            if (!overlay.prompt.empty()) KeyLine(overlay.interactKey.c_str(), overlay.prompt);
            ImGui::PopFont();
            ImGui::PushFont(nullptr, kBodyFontSize);
            if (!overlay.hint.empty()) KeyLine(overlay.lampKey.c_str(), overlay.hint);
            ImGui::PopFont();
        }
        ImGui::End();
    }
    if (overlay.cueSeconds > 0.0f && !overlay.cue.empty()) {
        const float fade = std::clamp(overlay.cueSeconds / 0.5f, 0.0f, 1.0f);  // Fades over the last half second.
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
        if (BeginPinned("##cue", ImVec2(18.0f * s, 84.0f * s), ImVec2(0.0f, 0.0f), 0.5f)) {  // Under the objective, clear of the panel.
            ImGui::PushFont(nullptr, kBodyFontSize);
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.95f, 1.0f), "%s", overlay.cue.c_str());
            ImGui::PopFont();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    if (overlay.endCard) {
        if (BeginPinned("##end", ImVec2(size.x * 0.5f, size.y * 0.5f), ImVec2(0.5f, 0.5f), 0.85f)) {
            ImGui::PushFont(nullptr, kTitleFontSize);
            ImGui::TextUnformatted("YOU MADE IT OUT");
            ImGui::PopFont();
            ImGui::PushFont(nullptr, kBodyFontSize);
            ImGui::TextDisabled("The lamp is yours and the door is behind you. That is the whole proof.");
            ImGui::Spacing();
            KeyLine("Esc", "Menu: restart the route or quit");
            ImGui::PopFont();
        }
        ImGui::End();
    } else if (overlay.introCard) {
        if (BeginPinned("##intro", ImVec2(size.x * 0.5f, size.y * 0.5f), ImVec2(0.5f, 0.5f), 0.85f)) {
            ImGui::PushFont(nullptr, kTitleFontSize);
            ImGui::TextUnformatted("LAST CIRCUIT");
            ImGui::PopFont();
            ImGui::PushFont(nullptr, kBodyFontSize);
            ImGui::TextDisabled("The building is on its last circuit. Find the light, keep it, and get out.");
            ImGui::Spacing();
            if (overlay.controls.empty()) {
                KeyLine("W A S D", "Move");
                KeyLine("Mouse", "Look");
                KeyLine("Shift", "Sprint");
                KeyLine("E", "Interact: doors, the lamp, the fuse, their sockets");
                KeyLine("F", "Lamp on / off");
                KeyLine("Esc", "Pause menu and settings");
                KeyLine("F1", "Diagnostic panel");
            } else {
                for (const auto& [key, action] : overlay.controls) KeyLine(key.c_str(), action);
            }
            ImGui::Spacing();
            ImGui::TextDisabled("Move or look around to begin.");
            ImGui::PopFont();
        }
        ImGui::End();
    }
}

MenuAction Ui::DrawPauseMenu(Settings& settings, bool& showDiagnostics, const MenuInfo& info) {
    MenuAction action = MenuAction::None;
    const ImVec2 size = DisplaySize();
    const float s = impl_->dpiScale;
    ImGui::SetNextWindowPos(ImVec2(size.x * 0.5f, size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(size.x - 40.0f, 520.0f * s), 0.0f), ImGuiCond_Always);  // Height auto-fits.
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(size.x - 40.0f, size.y - 40.0f));  // Scrolls when the screen is small.
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Paused", nullptr, flags)) {
        const float buttonWidth = ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(nullptr, kBodyFontSize);
        if (ImGui::Button("Resume", ImVec2(buttonWidth, 0.0f))) action = MenuAction::Resume;
        if (ImGui::Button("Restart from the last checkpoint", ImVec2(buttonWidth, 0.0f))) action = MenuAction::Restart;
        if (ImGui::Button("Reload the scene file", ImVec2(buttonWidth, 0.0f))) action = MenuAction::Reload;
        if (ImGui::Button("Quit", ImVec2(buttonWidth, 0.0f))) action = MenuAction::Quit;
        ImGui::PopFont();

        if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushItemWidth(-150.0f * s);
            float sensitivity = settings.mouseSensitivity * 1000.0f;
            if (ImGui::SliderFloat("Mouse sensitivity", &sensitivity, game::kSensitivityMin * 1000.0f, game::kSensitivityMax * 1000.0f, "%.2f")) {
                settings.mouseSensitivity = sensitivity / 1000.0f;
            }
            ImGui::Checkbox("Invert vertical look", &settings.invertY);
            ImGui::SliderFloat("Field of view", &settings.horizontalFovDegrees, game::kFovMin, game::kFovMax, "%.0f deg (horizontal)");
            ImGui::SliderFloat("Brightness", &settings.exposure, game::kExposureMin, game::kExposureMax, "%.2fx exposure", ImGuiSliderFlags_Logarithmic);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Brightness is a display scale (%.2fx to %.2fx). What the machine can see never depends on it.", game::kExposureMin, game::kExposureMax);
            ImGui::PopTextWrapPos();
            ImGui::SliderFloat("Master volume", &settings.masterVolume, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Effects volume", &settings.effectsVolume, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Ambience volume", &settings.ambienceVolume, 0.0f, 1.0f, "%.2f");
            ImGui::Checkbox("Text cues for important sounds", &settings.textCues);
            ImGui::SliderFloat("Interface scale", &settings.uiScale, game::kUiScaleMin, game::kUiScaleMax, "%.2fx");
            ImGui::Checkbox("Diagnostic panel (F1)", &showDiagnostics);
            ImGui::PopItemWidth();
            if (!info.rendering.empty()) {
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", info.rendering.c_str());
                ImGui::PopTextWrapPos();
            }
        }

        if (ImGui::CollapsingHeader("Controls")) {
            // One row per action: the key's name is a button; pressing it waits for the next key. The
            // key edges come from the window's raw input record (D-042), consumed here.
            const ImVec4 warn{0.95f, 0.75f, 0.35f, 1.0f};
            if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_SizingFixedFit)) {
                for (std::size_t i = 0; i < game::kActionCount; ++i) {
                    const game::Action a = static_cast<game::Action>(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(game::ActionName(a));
                    ImGui::TableSetColumnIndex(1);
                    const bool listening = impl_->capturingAction == static_cast<int>(i);
                    const std::string label = listening ? "press a key..." : game::KeyName(settings.bindings[a]);
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Button(label.c_str(), ImVec2(150.0f * s, 0.0f))) impl_->capturingAction = listening ? -1 : static_cast<int>(i);
                    ImGui::PopID();
                    ImGui::TableSetColumnIndex(2);
                    const std::vector<game::Action> conflicts = game::Conflicts(settings.bindings, a);
                    if (!conflicts.empty()) {
                        std::string also;
                        for (const game::Action c : conflicts) also += (also.empty() ? "also " : ", ") + std::string(game::ActionName(c));
                        ImGui::TextColored(warn, "%s", also.c_str());
                    }
                }
                ImGui::EndTable();
            }
            if (impl_->capturingAction >= 0 && impl_->window != nullptr) {
                const RawInputState& raw = impl_->window->Input();
                for (unsigned vk = 0x08; vk < 0xFF; ++vk) {
                    if (!raw.keyPressed[vk]) continue;
                    impl_->window->ConsumeKeyPressed(vk);
                    if (vk == game::kKeyEscape) {
                        impl_->capturingAction = -1;  // Cancelled.
                    } else if (game::IsBindable(vk)) {
                        settings.bindings.keys[static_cast<std::size_t>(impl_->capturingAction)] = vk;
                        log::Info("bound {} to {}", game::ActionName(static_cast<game::Action>(impl_->capturingAction)), game::KeyName(vk));
                        impl_->capturingAction = -1;
                    }
                    break;
                }
            }
            if (ImGui::Button("Reset the keys to W A S D, Shift, E, F")) settings.bindings = game::Bindings{};
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Esc opens this menu and F1 the diagnostic panel; those two stay fixed. Mouse buttons cannot be bound.");
            ImGui::PopTextWrapPos();
        }

        if (ImGui::CollapsingHeader("About")) {
            ImGui::PushTextWrapPos(0.0f);
            if (!info.buildId.empty()) ImGui::TextUnformatted(info.buildId.c_str());
            ImGui::TextDisabled("This software contains source code provided by NVIDIA Corporation.");
            ImGui::TextDisabled("NVIDIA Real-Time Denoisers (NVIDIA RTX SDKs License), Dear ImGui (MIT), NVIDIA MathLib (MIT), miniaudio (MIT No Attribution).");
            if (!info.noticesPath.empty()) ImGui::TextDisabled("Full notices: %s", info.noticesPath.c_str());
            if (!info.settingsPath.empty()) ImGui::TextDisabled("Settings file: %s", info.settingsPath.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Esc resumes. Settings apply immediately and are saved.");
    }
    ImGui::End();
    return action;
}

void Ui::DrawDiagnostics(const Diagnostics& d) {
    const ImVec2 size = DisplaySize();
    const float s = impl_->dpiScale;
    ImGui::SetNextWindowPos(ImVec2(size.x - 14.0f * s, 14.0f * s), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##diagnostics", nullptr, flags)) {
        ImGui::TextDisabled("DIAGNOSTICS (F1)");
        ImGui::Text("%s | frame %llu | tick %llu", d.mode.c_str(), static_cast<unsigned long long>(d.frame), static_cast<unsigned long long>(d.tick));
        ImGui::Text("GPU %.2f ms   CPU %.2f ms", d.gpuMs, d.cpuMs);
        if (!d.passes.empty() && ImGui::BeginTable("passes", 2, ImGuiTableFlags_SizingFixedFit)) {
            for (const auto& [name, ms] : d.passes) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.3f ms", ms);
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
        ImGui::Text("history resets %llu   since reset %u   denoiser dispatches %u", static_cast<unsigned long long>(d.historyResets), d.framesSinceReset,
                    d.denoiserDispatches);
        ImGui::Text("door %s   lamp %s (%s)", d.door.c_str(), d.lampOn ? "on" : "off", d.lamp.c_str());
        ImGui::Text("threat %s", d.threat.c_str());
        ImGui::Text("objective %s", d.objective.c_str());
        ImGui::Separator();
        ImGui::Text("scene %s  hash %016llx", d.sceneFile.c_str(), static_cast<unsigned long long>(d.sceneHash));
        if (!d.lastReload.empty()) ImGui::Text("reload: %s", d.lastReload.c_str());
        if (!d.audio.empty()) ImGui::Text("audio %s", d.audio.c_str());
    }
    ImGui::End();
}

void Ui::Render(ID3D12GraphicsCommandList* list, ID3D12Resource* backBuffer) {
    if (!impl_->frameOpen) return;
    ImGui::Render();
    impl_->frameOpen = false;
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr || drawData->CmdListsCount == 0) return;
    // Texture lifecycle events (the font atlas grows when new glyph sizes appear): logged for the records.
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->Status == ImTextureStatus_WantCreate) {
            log::Info("UI: font atlas texture {}x{} created (draw data: {} lists, {} vertices)", tex->Width, tex->Height, drawData->CmdListsCount, drawData->TotalVtxCount);
        } else if (tex->Status == ImTextureStatus_WantUpdates) {
            log::Debug("UI: font atlas update {}x{} at ({}, {})", tex->UpdateRect.w, tex->UpdateRect.h, tex->UpdateRect.x, tex->UpdateRect.y);
        } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames == 0) {
            log::Info("UI: font atlas texture {}x{} retired", tex->Width, tex->Height);
        }
    }

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    impl_->device->CreateRenderTargetView(backBuffer, nullptr, rtv);
    D3D12_RESOURCE_BARRIER toTarget{};
    toTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toTarget.Transition.pResource = backBuffer;
    toTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1, &toTarget);
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {impl_->srvHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(drawData, list);
    D3D12_RESOURCE_BARRIER toPresent = toTarget;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    list->ResourceBarrier(1, &toPresent);
}

bool Ui::WantsMouse() const { return ImGui::GetIO().WantCaptureMouse; }
bool Ui::WantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

}  // namespace lc::ui
