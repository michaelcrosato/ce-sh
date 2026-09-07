#include "game/settings.h"

#include "core/json_reader.h"
#include "core/json_writer.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

namespace lc::game {

namespace {

constexpr int kFileVersion = 1;

float ClampFinite(float value, float lo, float hi, float fallback) {
    if (!std::isfinite(value)) return fallback;
    return std::clamp(value, lo, hi);
}

bool ReadFloat(const json::Value& object, const char* key, float& out, float lo, float hi, std::vector<std::string>& problems) {
    const json::Value* v = object.Get(key);
    if (v == nullptr) return false;
    if (!v->IsNumber()) {
        problems.push_back(std::format("'{}' is not a number; the default stays", key));
        return false;
    }
    const double d = v->AsNumber();
    if (!std::isfinite(d) || d < lo || d > hi) {
        problems.push_back(std::format("'{}' = {} is outside {}..{}; clamped", key, d, lo, hi));
    }
    out = ClampFinite(static_cast<float>(d), lo, hi, out);
    return true;
}

bool ReadBool(const json::Value& object, const char* key, bool& out, std::vector<std::string>& problems) {
    const json::Value* v = object.Get(key);
    if (v == nullptr) return false;
    if (!v->IsBool()) {
        problems.push_back(std::format("'{}' is not true or false; the default stays", key));
        return false;
    }
    out = v->AsBool();
    return true;
}

}  // namespace

const char* ActionName(Action action) {
    switch (action) {
        case Action::Forward: return "Move forward";
        case Action::Back: return "Move back";
        case Action::Left: return "Move left";
        case Action::Right: return "Move right";
        case Action::Sprint: return "Sprint";
        case Action::Interact: return "Interact";
        case Action::Lamp: return "Lamp on / off";
    }
    return "?";
}

const char* ActionKey(Action action) {
    switch (action) {
        case Action::Forward: return "forward";
        case Action::Back: return "back";
        case Action::Left: return "left";
        case Action::Right: return "right";
        case Action::Sprint: return "sprint";
        case Action::Interact: return "interact";
        case Action::Lamp: return "lamp";
    }
    return "?";
}

Settings Clamped(Settings s) {
    const Settings d;
    s.mouseSensitivity = ClampFinite(s.mouseSensitivity, kSensitivityMin, kSensitivityMax, d.mouseSensitivity);
    s.horizontalFovDegrees = ClampFinite(s.horizontalFovDegrees, kFovMin, kFovMax, d.horizontalFovDegrees);
    s.exposure = ClampFinite(s.exposure, kExposureMin, kExposureMax, d.exposure);
    s.masterVolume = ClampFinite(s.masterVolume, 0.0f, 1.0f, d.masterVolume);
    s.effectsVolume = ClampFinite(s.effectsVolume, 0.0f, 1.0f, d.effectsVolume);
    s.ambienceVolume = ClampFinite(s.ambienceVolume, 0.0f, 1.0f, d.ambienceVolume);
    s.uiScale = ClampFinite(s.uiScale, kUiScaleMin, kUiScaleMax, d.uiScale);
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (!IsBindable(s.bindings.keys[i])) s.bindings.keys[i] = d.bindings.keys[i];
    }
    return s;
}

std::string KeyName(unsigned vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    if (vk >= 0x70 && vk <= 0x87) return std::format("F{}", vk - 0x70 + 1);
    if (vk >= 0x60 && vk <= 0x69) return std::format("Num {}", vk - 0x60);
    switch (vk) {
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case kKeyShift: return "Shift";
        case kKeyControl: return "Ctrl";
        case 0x12: return "Alt";
        case 0x14: return "Caps Lock";
        case kKeyEscape: return "Esc";
        case kKeySpace: return "Space";
        case 0x21: return "Page Up";
        case 0x22: return "Page Down";
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0x6A: return "Num *";
        case 0x6B: return "Num +";
        case 0x6D: return "Num -";
        case 0x6E: return "Num .";
        case 0x6F: return "Num /";
        case 0xA0: return "Left Shift";
        case 0xA1: return "Right Shift";
        case 0xA2: return "Left Ctrl";
        case 0xA3: return "Right Ctrl";
        case 0xA4: return "Left Alt";
        case 0xA5: return "Right Alt";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: return std::format("key 0x{:02X}", vk);
    }
}

bool IsBindable(unsigned vk) {
    if (vk < 0x08 || vk > 0xFE) return false;      // Mouse buttons and nothing.
    if (vk == kKeyEscape || vk == kKeyF1) return false;  // The menu and the panel stay fixed.
    if (vk == 0x14 || vk == 0x90 || vk == 0x91) return false;  // Caps, Num, Scroll lock.
    if (vk == 0x5B || vk == 0x5C || vk == 0x5D) return false;  // Windows keys and the menu key.
    if (vk == 0x2C || vk == 0x13) return false;    // Print Screen, Pause.
    return true;
}

std::vector<Action> Conflicts(const Bindings& b, Action action) {
    std::vector<Action> out;
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const Action other = static_cast<Action>(i);
        if (other != action && b[other] == b[action]) out.push_back(other);
    }
    return out;
}

std::string SettingsToJson(const Settings& s) {
    JsonWriter w;
    w.BeginObject();
    w.Field("version", static_cast<std::int64_t>(kFileVersion));
    w.Field("mouseSensitivity", s.mouseSensitivity);
    w.Field("invertY", s.invertY);
    w.Field("horizontalFovDegrees", s.horizontalFovDegrees);
    w.Field("exposure", s.exposure);
    w.Field("masterVolume", s.masterVolume);
    w.Field("effectsVolume", s.effectsVolume);
    w.Field("ambienceVolume", s.ambienceVolume);
    w.Field("textCues", s.textCues);
    w.Field("uiScale", s.uiScale);
    w.Key("bindings");
    w.BeginObject();
    for (std::size_t i = 0; i < kActionCount; ++i) {
        w.Field(ActionKey(static_cast<Action>(i)), static_cast<std::int64_t>(s.bindings.keys[i]));
    }
    w.EndObject();
    w.Key("bindingNames");  // For a person reading the file; ignored when loading.
    w.BeginObject();
    for (std::size_t i = 0; i < kActionCount; ++i) {
        w.Field(ActionKey(static_cast<Action>(i)), KeyName(s.bindings.keys[i]));
    }
    w.EndObject();
    w.EndObject();
    return w.Text();
}

std::optional<Settings> SettingsFromJson(std::string_view text, std::vector<std::string>& problems) {
    const json::ParseResult parsed = json::Parse(text);
    if (!parsed.value || !parsed.value->IsObject()) {
        problems.push_back(parsed.value ? std::string("the settings file is not a JSON object") : "the settings file is not valid JSON: " + parsed.error);
        return std::nullopt;
    }
    const json::Value& root = *parsed.value;
    Settings s;
    if (const json::Value* v = root.Get("version"); v != nullptr && v->IsNumber() && v->AsNumber() > kFileVersion) {
        problems.push_back(std::format("the settings file has version {} (this build reads version {}); unknown members are ignored", v->AsNumber(), kFileVersion));
    }
    ReadFloat(root, "mouseSensitivity", s.mouseSensitivity, kSensitivityMin, kSensitivityMax, problems);
    ReadBool(root, "invertY", s.invertY, problems);
    ReadFloat(root, "horizontalFovDegrees", s.horizontalFovDegrees, kFovMin, kFovMax, problems);
    ReadFloat(root, "exposure", s.exposure, kExposureMin, kExposureMax, problems);
    ReadFloat(root, "masterVolume", s.masterVolume, 0.0f, 1.0f, problems);
    ReadFloat(root, "effectsVolume", s.effectsVolume, 0.0f, 1.0f, problems);
    ReadFloat(root, "ambienceVolume", s.ambienceVolume, 0.0f, 1.0f, problems);
    ReadBool(root, "textCues", s.textCues, problems);
    ReadFloat(root, "uiScale", s.uiScale, kUiScaleMin, kUiScaleMax, problems);
    if (const json::Value* bindings = root.Get("bindings")) {
        if (!bindings->IsObject()) {
            problems.push_back("'bindings' is not an object; the default keys stay");
        } else {
            for (std::size_t i = 0; i < kActionCount; ++i) {
                const Action action = static_cast<Action>(i);
                const json::Value* v = bindings->Get(ActionKey(action));
                if (v == nullptr) continue;
                if (!v->IsNumber() || v->AsNumber() != std::floor(v->AsNumber())) {
                    problems.push_back(std::format("bindings.{} is not a whole number (a Windows virtual-key code); the default key stays", ActionKey(action)));
                    continue;
                }
                const double code = v->AsNumber();
                if (code < 0.0 || code > 255.0 || !IsBindable(static_cast<unsigned>(code))) {
                    problems.push_back(std::format("bindings.{} = {} cannot be bound; the default key stays", ActionKey(action), code));
                    continue;
                }
                s.bindings.keys[i] = static_cast<unsigned>(code);
            }
        }
    }
    return Clamped(s);
}

Settings LoadSettingsFile(const std::filesystem::path& path, std::vector<std::string>& problems) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return Settings{};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        problems.push_back(std::format("cannot read {}", path.string()));
        return Settings{};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::optional<Settings> s = SettingsFromJson(ss.str(), problems);
    return s ? *s : Settings{};
}

bool SaveSettingsFile(const std::filesystem::path& path, const Settings& s) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << SettingsToJson(s) << '\n';
    return static_cast<bool>(out);
}

InputFrame InputFromKeys(const bool* down, const bool* pressed, float mouseDx, float mouseDy, const Settings& s, bool consumeEdges) {
    const Bindings& b = s.bindings;
    InputFrame f;
    if (down[b[Action::Forward]]) f.moveZ += 1.0f;
    if (down[b[Action::Back]]) f.moveZ -= 1.0f;
    if (down[b[Action::Right]]) f.moveX += 1.0f;
    if (down[b[Action::Left]]) f.moveX -= 1.0f;
    f.sprint = down[b[Action::Sprint]];
    if (consumeEdges) {
        f.lookDx = -mouseDx * s.mouseSensitivity;                          // Mouse right turns right (toward +X at yaw 0).
        f.lookDy = (s.invertY ? mouseDy : -mouseDy) * s.mouseSensitivity;  // Mouse up looks up unless inverted.
        f.interactPressed = pressed[b[Action::Interact]];
        f.lampPressed = pressed[b[Action::Lamp]];
    }
    return f;
}

}  // namespace lc::game
