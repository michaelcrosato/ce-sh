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
    bool overlay = false;       // Denoised mode: draw `view` on the display image (the linear output stays production radiance).
    bool cameraCut = false;     // The camera jumped (teleport, restart, reload): temporal history is invalid this frame.
    float frameDeltaMs = 0.0f;  // Wall-clock or replay period between this frame and the previous one (0 = unknown).
};

}  // namespace lc
