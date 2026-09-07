// Generated test scenes (spec §16: no asset download dependency). Diagnostic scenes prove geometry
// and identifiers; lighting scenes carry radiance expectations for T03, T04, T07, T08, and T09.
#pragma once

#include "scene/camera.h"
#include "scene/expectations.h"
#include "scene/scene.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lc {

struct SceneDescription {
    std::string name;
    Scene scene;
    Camera camera;
    std::vector<HitExpectation> expectations;
    std::vector<RadianceExpectation> radianceExpectations;
    std::vector<StatsPatch> statsPatches;
    bool needsLighting = false;  // False for the M1 diagnostic scenes.
};

std::vector<std::string> BuiltinSceneNames();
std::optional<SceneDescription> BuildBuiltinScene(std::string_view name);

}  // namespace lc
