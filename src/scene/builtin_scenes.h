// Generated test scenes for the hardware-geometry milestone (spec §16: no asset download dependency).
#pragma once

#include "scene/camera.h"
#include "scene/scene.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lc {

// A pixel the validation mode checks: (u, v) in [0, 1] (u right, v down), the stable instance id it
// must hit (0 = must miss), and whether the hit must be front-facing.
struct HitExpectation {
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t expectedStableId = 0;
    bool expectFrontFace = true;
    std::string description;
};

struct SceneDescription {
    std::string name;
    Scene scene;
    Camera camera;
    std::vector<HitExpectation> expectations;
};

std::vector<std::string> BuiltinSceneNames();
std::optional<SceneDescription> BuildBuiltinScene(std::string_view name);

}  // namespace lc
