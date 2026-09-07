// On-screen interface (spec §14 "a clear prompt", §15 cues, §16 diagnostic panel, §21 restart):
// Dear ImGui drawn into the swap-chain image after the present copy, so nothing here touches the
// path-tracer textures or the production radiance. Diagnostic colours stay out of the linear output.
#pragma once

#include "game/settings.h"
#include "graphics/d3d12/d3d12_common.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lc {
class Window;
namespace gfx {
class Device;
class GraphicsQueue;
}  // namespace gfx
}  // namespace lc

namespace lc::ui {

// Player-facing settings (spec §14: sensitivity, inverted look, field of view, the key bindings;
// §12: the brightness range; §15: volumes and cues): the game layer's struct, which the settings
// file stores (src/game/settings.h).
using Settings = game::Settings;

// What the pause menu states about this build and this run (spec §24: unsupported features are
// explained, not offered as switches; §20 M7: build identification and license notices).
struct MenuInfo {
    std::string buildId;       // "Last Circuit 0.1.0 build <commit> (Release)".
    std::string rendering;     // The rendering path in words, with what this build does not offer.
    std::string settingsPath;  // Where the settings are saved.
    std::string noticesPath;   // The third-party notices file beside the executable.
};

struct Diagnostics {
    std::string mode;
    double gpuMs = 0.0;
    double cpuMs = 0.0;
    std::uint64_t frame = 0;
    std::uint64_t tick = 0;
    std::uint64_t historyResets = 0;
    std::uint32_t framesSinceReset = 0;
    std::uint32_t denoiserDispatches = 0;
    std::string door;
    bool lampOn = false;
    std::string lamp;
    std::string objective;
    std::string threat;
    std::string sceneFile;
    std::uint64_t sceneHash = 0;
    std::string lastReload;
    std::string audio;
    std::vector<std::pair<std::string, double>> passes;  // GPU pass name -> milliseconds.
};

enum class MenuAction { None, Resume, Restart, Reload, Quit };

struct Overlay {
    std::string prompt;         // Interaction prompt at the bottom centre ("E  Open door").
    std::string hint;           // Secondary control hint ("F  Lamp off").
    std::string interactKey = "E";  // The bound keys shown with the prompt and the hint.
    std::string lampKey = "F";
    std::string cue;            // Text cue for a sound event (shown while cueSeconds > 0).
    float cueSeconds = 0.0f;
    std::string objectiveLine;  // Current objective at the top left.
    bool introCard = false;     // Controls card during the introduction.
    bool endCard = false;       // The route is complete.
    std::vector<std::pair<std::string, std::string>> controls;  // The controls card: (key, action) from the bindings.
};

class Ui {
public:
    Ui(Window& window, gfx::Device& device, gfx::GraphicsQueue& queue, DXGI_FORMAT backBufferFormat);
    ~Ui();
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    // Once per frame, before any Draw call. The interface scale is the settings' (accessibility).
    void BeginFrame(float uiScale = 1.0f);
    void DrawOverlay(const Overlay& overlay);
    // The pause menu with its settings, the key bindings, and the About section; returns the action
    // the player chose this frame.
    MenuAction DrawPauseMenu(Settings& settings, bool& showDiagnostics, const MenuInfo& info);
    // True while the menu waits for a key press to bind (Escape then cancels instead of resuming).
    bool CapturingKey() const;
    void DrawDiagnostics(const Diagnostics& diagnostics);
    // Records the interface into the back buffer (PRESENT -> RENDER_TARGET -> PRESENT). Once per
    // frame after the draws; a frame without Render discards the draws.
    void Render(ID3D12GraphicsCommandList* list, ID3D12Resource* backBuffer);

    bool WantsMouse() const;
    bool WantsKeyboard() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lc::ui
