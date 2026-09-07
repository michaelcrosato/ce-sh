// Player settings and key bindings (spec §14: rebindable actions, sensitivity, inverted look, the
// field-of-view convention; §12: the brightness control within a documented range; §15: text cues),
// their JSON file, and the mapping from key states to a simulation input. No window or platform
// types: the CPU tests cover all of it.
#pragma once

#include "game/input.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lc::game {

// Virtual-key codes used without <windows.h> (the values are Windows' VK_* constants).
inline constexpr unsigned kKeyShift = 0x10;
inline constexpr unsigned kKeyControl = 0x11;
inline constexpr unsigned kKeySpace = 0x20;
inline constexpr unsigned kKeyEscape = 0x1B;
inline constexpr unsigned kKeyF1 = 0x70;

enum class Action { Forward, Back, Left, Right, Sprint, Interact, Lamp };
inline constexpr std::size_t kActionCount = 7;
const char* ActionName(Action action);  // "Move forward", ...
const char* ActionKey(Action action);   // The JSON member: "forward", ...

struct Bindings {
    unsigned keys[kActionCount] = {'W', 'S', 'A', 'D', kKeyShift, 'E', 'F'};
    unsigned& operator[](Action a) { return keys[static_cast<std::size_t>(a)]; }
    unsigned operator[](Action a) const { return keys[static_cast<std::size_t>(a)]; }
    bool operator==(const Bindings&) const = default;
};

struct Settings {
    float mouseSensitivity = 0.0022f;   // Radians per raw mouse count.
    bool invertY = false;
    float horizontalFovDegrees = 90.0f; // Horizontal at the current aspect ratio (the convention stated in the menu).
    float exposure = 4.0f;              // Display scale only (spec §12): never a gameplay input (T14).
    float masterVolume = 0.8f;
    float effectsVolume = 1.0f;
    float ambienceVolume = 1.0f;
    bool textCues = true;               // Text cues for important sounds (accessibility, spec §15).
    float uiScale = 1.0f;               // Interface text scale (accessibility).
    Bindings bindings;
    bool operator==(const Settings&) const = default;
};

// The documented ranges (the menu states them; a file cannot leave them).
inline constexpr float kSensitivityMin = 0.0005f, kSensitivityMax = 0.01f;
inline constexpr float kFovMin = 60.0f, kFovMax = 110.0f;
inline constexpr float kExposureMin = 0.25f, kExposureMax = 8.0f;
inline constexpr float kUiScaleMin = 0.8f, kUiScaleMax = 1.6f;

// Every value inside its range; volumes within [0, 1]; unbindable keys back to their defaults.
Settings Clamped(Settings s);

// A short name for a virtual key ("W", "Shift", "Space", "F1", "Up"); "key 0x.." when unknown.
std::string KeyName(unsigned virtualKey);
// A key a person may bind: a keyboard key that is not Escape (the menu), F1 (the panel), a lock key,
// or a Windows key.
bool IsBindable(unsigned virtualKey);
// The other actions bound to the same key as `action` (the menu's conflict note).
std::vector<Action> Conflicts(const Bindings& b, Action action);

std::string SettingsToJson(const Settings& s);
// Missing members keep their defaults; a mistyped member or an unbindable key is listed in
// `problems` and keeps its default. Empty only when the text is not a JSON object.
std::optional<Settings> SettingsFromJson(std::string_view text, std::vector<std::string>& problems);

// A missing file is the defaults with no problem; an unreadable or invalid file is the defaults
// with the problems listed. Save creates the parent directory.
Settings LoadSettingsFile(const std::filesystem::path& path, std::vector<std::string>& problems);
bool SaveSettingsFile(const std::filesystem::path& path, const Settings& s);

// Key states to a simulation input through the bindings. `down` and `pressed` are indexed by
// virtual key (256 entries); the mouse deltas and the press edges count only when `consumeEdges`
// is set (the first tick of a frame reads them).
InputFrame InputFromKeys(const bool* down, const bool* pressed, float mouseDx, float mouseDy, const Settings& s, bool consumeEdges);

}  // namespace lc::game
