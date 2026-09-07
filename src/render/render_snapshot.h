// What the renderer consumes each frame: a consistent view of the scene and camera.
#pragma once

#include "render/view_mode.h"
#include "scene/camera.h"
#include "scene/scene.h"

#include <cstdint>

namespace lc {

struct RenderSnapshot {
    const Scene* scene = nullptr;
    Camera camera;
    std::uint32_t frameIndex = 0;
    ViewMode view = ViewMode::Normals;
};

}  // namespace lc
