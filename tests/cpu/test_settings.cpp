// Spec §14 (rebindable actions, sensitivity, inverted look, the field-of-view convention), §12 (the
// brightness range), §15 (text cues), §20 M7 (settings that persist): the settings file and the
// mapping from key states through the bindings to a simulation input.
#include "lc_test.h"

#include "game/settings.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using lc::game::Action;
using lc::game::Bindings;
using lc::game::Settings;

namespace {

Settings Modified() {
    Settings s;
    s.mouseSensitivity = 0.004f;
    s.invertY = true;
    s.horizontalFovDegrees = 75.0f;
    s.exposure = 2.5f;
    s.masterVolume = 0.5f;
    s.effectsVolume = 0.25f;
    s.ambienceVolume = 0.75f;
    s.textCues = false;
    s.uiScale = 1.25f;
    s.bindings[Action::Forward] = 'I';
    s.bindings[Action::Back] = 'K';
    s.bindings[Action::Left] = 'J';
    s.bindings[Action::Right] = 'L';
    s.bindings[Action::Sprint] = lc::game::kKeyControl;
    s.bindings[Action::Interact] = lc::game::kKeySpace;
    s.bindings[Action::Lamp] = 'Q';
    return s;
}

}  // namespace

LC_TEST(settings_round_trip_through_json_and_keep_defaults_for_missing_members) {
    std::vector<std::string> problems;
    const auto defaults = lc::game::SettingsFromJson(lc::game::SettingsToJson(Settings{}), problems);
    LC_REQUIRE(defaults.has_value());
    LC_CHECK(*defaults == Settings{});
    LC_CHECK(problems.empty());
    const Settings modified = Modified();
    const auto back = lc::game::SettingsFromJson(lc::game::SettingsToJson(modified), problems);
    LC_REQUIRE(back.has_value());
    LC_CHECK(*back == modified);
    LC_CHECK(problems.empty());
    // A partial file keeps the defaults of what it does not mention.
    const auto partial = lc::game::SettingsFromJson("{\"exposure\": 1.5, \"bindings\": {\"lamp\": 81}}", problems);
    LC_REQUIRE(partial.has_value());
    LC_CHECK_NEAR(partial->exposure, 1.5f, 1e-6f);
    LC_CHECK_EQ(partial->bindings[Action::Lamp], unsigned('Q'));
    LC_CHECK_EQ(partial->bindings[Action::Forward], unsigned('W'));
    LC_CHECK(partial->textCues);
    LC_CHECK(problems.empty());
}

LC_TEST(settings_values_stay_inside_their_documented_ranges) {
    Settings s = Modified();
    s.exposure = 100.0f;
    s.uiScale = 0.1f;
    s.horizontalFovDegrees = 200.0f;
    s.mouseSensitivity = -1.0f;
    s.masterVolume = 3.0f;
    s.bindings[Action::Interact] = lc::game::kKeyEscape;  // Not bindable: back to the default.
    const Settings c = lc::game::Clamped(s);
    LC_CHECK_NEAR(c.exposure, lc::game::kExposureMax, 1e-6f);
    LC_CHECK_NEAR(c.uiScale, lc::game::kUiScaleMin, 1e-6f);
    LC_CHECK_NEAR(c.horizontalFovDegrees, lc::game::kFovMax, 1e-6f);
    LC_CHECK_NEAR(c.mouseSensitivity, lc::game::kSensitivityMin, 1e-6f);
    LC_CHECK_NEAR(c.masterVolume, 1.0f, 1e-6f);
    LC_CHECK_EQ(c.bindings[Action::Interact], unsigned('E'));
    // The same through a file: out-of-range numbers are clamped and reported; a mistyped member and
    // an unbindable key keep their defaults and are reported; unknown members are ignored.
    std::vector<std::string> problems;
    const auto s2 = lc::game::SettingsFromJson("{\"exposure\": 50, \"invertY\": \"yes\", \"bindings\": {\"sprint\": 27, \"lamp\": \"F\"}, \"unknown\": 1}", problems);
    LC_REQUIRE(s2.has_value());
    LC_CHECK_NEAR(s2->exposure, lc::game::kExposureMax, 1e-6f);
    LC_CHECK(!s2->invertY);
    LC_CHECK_EQ(s2->bindings[Action::Sprint], lc::game::kKeyShift);
    LC_CHECK_EQ(s2->bindings[Action::Lamp], unsigned('F'));
    LC_CHECK_EQ(problems.size(), std::size_t{4});
    // Not JSON at all: nothing, with the reason.
    problems.clear();
    LC_CHECK(!lc::game::SettingsFromJson("not json", problems).has_value());
    LC_CHECK_EQ(problems.size(), std::size_t{1});
}

LC_TEST(settings_file_missing_broken_and_saved) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "lc_settings_test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const std::filesystem::path file = dir / "nested" / "settings.json";
    std::vector<std::string> problems;
    LC_CHECK(lc::game::LoadSettingsFile(file, problems) == Settings{});  // Missing: the defaults, no problem.
    LC_CHECK(problems.empty());
    LC_CHECK(lc::game::SaveSettingsFile(file, Modified()));  // Creates the directories.
    LC_CHECK(lc::game::LoadSettingsFile(file, problems) == Modified());
    LC_CHECK(problems.empty());
    std::FILE* f = nullptr;
    fopen_s(&f, file.string().c_str(), "wb");
    LC_REQUIRE(f != nullptr);
    std::fputs("{ broken", f);
    std::fclose(f);
    LC_CHECK(lc::game::LoadSettingsFile(file, problems) == Settings{});  // Broken: the defaults, one problem.
    LC_CHECK_EQ(problems.size(), std::size_t{1});
    std::filesystem::remove_all(dir, ec);
}

LC_TEST(key_names_bindable_keys_and_conflicts) {
    LC_CHECK_EQ(lc::game::KeyName('W'), std::string("W"));
    LC_CHECK_EQ(lc::game::KeyName('7'), std::string("7"));
    LC_CHECK_EQ(lc::game::KeyName(lc::game::kKeyShift), std::string("Shift"));
    LC_CHECK_EQ(lc::game::KeyName(lc::game::kKeySpace), std::string("Space"));
    LC_CHECK_EQ(lc::game::KeyName(lc::game::kKeyF1), std::string("F1"));
    LC_CHECK_EQ(lc::game::KeyName(0x26), std::string("Up"));
    LC_CHECK_EQ(lc::game::KeyName(0x65), std::string("Num 5"));
    LC_CHECK_EQ(lc::game::KeyName(0xFE), std::string("key 0xFE"));
    LC_CHECK(lc::game::IsBindable('Q'));
    LC_CHECK(lc::game::IsBindable(lc::game::kKeySpace));
    LC_CHECK(!lc::game::IsBindable(lc::game::kKeyEscape));
    LC_CHECK(!lc::game::IsBindable(lc::game::kKeyF1));
    LC_CHECK(!lc::game::IsBindable(0x01));  // A mouse button.
    LC_CHECK(!lc::game::IsBindable(0x5B));  // The Windows key.
    Bindings b;
    LC_CHECK(lc::game::Conflicts(b, Action::Interact).empty());
    b[Action::Interact] = 'F';
    const auto conflicts = lc::game::Conflicts(b, Action::Interact);
    LC_REQUIRE(conflicts.size() == 1u);
    LC_CHECK(conflicts[0] == Action::Lamp);
    LC_CHECK(lc::game::Conflicts(b, Action::Lamp).size() == 1u);
    LC_CHECK_EQ(std::string(lc::game::ActionName(Action::Lamp)), std::string("Lamp on / off"));
    LC_CHECK_EQ(std::string(lc::game::ActionKey(Action::Sprint)), std::string("sprint"));
}

LC_TEST(input_follows_the_bindings_and_the_look_settings) {
    bool down[256] = {};
    bool pressed[256] = {};
    Settings s = Modified();  // Forward on I, interact on Space, lamp on Q, inverted look.
    down['I'] = true;
    down['L'] = true;
    down[lc::game::kKeyControl] = true;
    pressed[lc::game::kKeySpace] = true;
    pressed['Q'] = true;
    const lc::game::InputFrame f = lc::game::InputFromKeys(down, pressed, 10.0f, -5.0f, s, true);
    LC_CHECK_NEAR(f.moveZ, 1.0f, 1e-6f);
    LC_CHECK_NEAR(f.moveX, 1.0f, 1e-6f);
    LC_CHECK(f.sprint);
    LC_CHECK(f.interactPressed);
    LC_CHECK(f.lampPressed);
    LC_CHECK_NEAR(f.lookDx, -10.0f * s.mouseSensitivity, 1e-7f);
    LC_CHECK_NEAR(f.lookDy, -5.0f * s.mouseSensitivity, 1e-7f);  // Inverted: mouse up (negative dy) looks down.
    // The default keys do nothing under these bindings; edges and mouse deltas count only when consumed.
    bool wasd[256] = {};
    wasd['W'] = true;
    bool edges[256] = {};
    edges['E'] = true;
    const lc::game::InputFrame none = lc::game::InputFromKeys(wasd, edges, 0.0f, 0.0f, s, true);
    LC_CHECK(none.IsNeutral());
    const lc::game::InputFrame later = lc::game::InputFromKeys(down, pressed, 10.0f, -5.0f, s, false);
    LC_CHECK_NEAR(later.moveZ, 1.0f, 1e-6f);
    LC_CHECK(!later.interactPressed && !later.lampPressed);
    LC_CHECK_NEAR(later.lookDx, 0.0f, 1e-7f);
    s.invertY = false;
    LC_CHECK_NEAR(lc::game::InputFromKeys(down, pressed, 0.0f, -5.0f, s, true).lookDy, 5.0f * s.mouseSensitivity, 1e-7f);
}
