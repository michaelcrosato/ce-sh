#include "lc_test.h"

#include "app/options.h"
#include "core/cli.h"

#include <string>
#include <vector>

namespace {

lc::ArgParser MakeParser() {
    lc::ArgParser p;
    p.AddFlag("validate", "flag");
    p.AddStringOption("scene", "scene", "rt_triangle");
    p.AddIntOption("frames", "frames", 0);
    p.AddFloatOption("fov", "fov", 90.0f);
    return p;
}

std::vector<std::string> Args(std::initializer_list<const char*> list) { return {list.begin(), list.end()}; }

}  // namespace

LC_TEST(cli_unknown_option_is_an_error) {
    auto p = MakeParser();
    const auto err = p.Parse(Args({"--bogus"}));
    LC_REQUIRE(err.has_value());
    LC_CHECK(err->find("--bogus") != std::string::npos);
}

LC_TEST(cli_accepts_space_and_equals_forms) {
    auto p = MakeParser();
    LC_CHECK(!p.Parse(Args({"--frames", "3"})).has_value());
    LC_CHECK_EQ(p.GetInt("frames"), 3);
    LC_CHECK(!p.Parse(Args({"--frames=7", "--scene=rt_boxes"})).has_value());
    LC_CHECK_EQ(p.GetInt("frames"), 7);
    LC_CHECK_EQ(p.GetString("scene"), std::string("rt_boxes"));
}

LC_TEST(cli_rejects_malformed_and_missing_values) {
    auto p = MakeParser();
    LC_CHECK(p.Parse(Args({"--frames", "x"})).has_value());
    LC_CHECK(p.Parse(Args({"--frames", "3.5"})).has_value());
    LC_CHECK(p.Parse(Args({"--scene"})).has_value());
    LC_CHECK(p.Parse(Args({"--scene", "--validate"})).has_value());
    LC_CHECK(p.Parse(Args({"--validate=1"})).has_value());
    LC_CHECK(p.Parse(Args({"--frames", "1", "--frames", "2"})).has_value());
    LC_CHECK(p.Parse(Args({"stray"})).has_value());
}

LC_TEST(cli_flags_and_defaults) {
    auto p = MakeParser();
    LC_CHECK(!p.Parse(Args({"--validate", "--fov", "75.5"})).has_value());
    LC_CHECK(p.Has("validate"));
    LC_CHECK(!p.Has("scene"));
    LC_CHECK_EQ(p.GetString("scene"), std::string("rt_triangle"));
    LC_CHECK_EQ(p.GetInt("frames"), 0);
    LC_CHECK_NEAR(p.GetFloat("fov"), 75.5f, 1e-6);
    LC_CHECK(p.Usage("prog", "desc").find("--validate") != std::string::npos);
}

LC_TEST(app_options_parse_scene_view_and_flags) {
    const auto parsed = lc::ParseAppOptions(Args({"--scene", "rt_boxes", "--view", "ids", "--headless", "--frames", "2"}));
    LC_REQUIRE(parsed.options.has_value());
    LC_CHECK_EQ(parsed.options->scene, std::string("rt_boxes"));
    LC_CHECK(parsed.options->view == lc::ViewMode::InstanceIds);
    LC_CHECK(parsed.options->headless);
    LC_CHECK(!parsed.options->validate);
    LC_CHECK_EQ(parsed.options->frames, 2u);
    LC_CHECK_EQ(parsed.options->width, 1280u);
    LC_CHECK_EQ(parsed.options->height, 720u);
    LC_CHECK(!parsed.options->capture.has_value());
}

LC_TEST(app_options_reject_bad_view_and_size) {
    const auto badView = lc::ParseAppOptions(Args({"--view", "nonsense"}));
    LC_CHECK(!badView.options.has_value());
    LC_CHECK(badView.error.find("normals") != std::string::npos);

    const auto badSize = lc::ParseAppOptions(Args({"--width", "2"}));
    LC_CHECK(!badSize.options.has_value());

    const auto badVsync = lc::ParseAppOptions(Args({"--vsync", "maybe"}));
    LC_CHECK(!badVsync.options.has_value());

    const auto conflict = lc::ParseAppOptions(Args({"--resize-test", "--headless"}));
    LC_CHECK(!conflict.options.has_value());
}

LC_TEST(app_options_paths_and_gpu_validation_imply_debug_layer) {
    const auto parsed = lc::ParseAppOptions(Args({"--capture", "artifacts/x", "--gpu-validation", "--debug-layer", "off"}));
    LC_REQUIRE(parsed.options.has_value());
    LC_CHECK(parsed.options->capture.has_value());
    LC_CHECK(parsed.options->gpuValidation);
    LC_CHECK(parsed.options->debugLayer);
}
