// Frame capture: lossless display PNG, linear PFM, and JSON metadata, all from the running program.
#pragma once

#include "graphics/d3d12/timestamp_queries.h"
#include "render/renderer.h"

#include <filesystem>
#include <string>
#include <vector>

namespace lc {

struct CaptureMetadata {
    std::string scene;
    std::string view;
    std::uint32_t frameIndex = 0;
    std::uint32_t sampleIndex = 0;
    std::uint32_t seed = 0;
    std::uint32_t pathDepth = 1;      // Camera rays only in M1.
    float exposure = 1.0f;            // Fixed; diagnostic views are not exposed.
    std::uint32_t renderWidth = 0;
    std::uint32_t renderHeight = 0;
    std::uint32_t outputWidth = 0;
    std::uint32_t outputHeight = 0;
    std::string reconstruction = "none";
    std::string adapter;
    std::string driver;
    std::string buildCommit;
    bool buildDirty = false;
    std::string buildConfig;
    std::string timestamp;
    std::uint64_t triangleInstances = 0;
    std::uint32_t instanceCount = 0;
    std::vector<gfx::TimerResult> timings;
};

// Writes <dir>/<baseName>.png, .pfm, and .json. Returns false when any file failed.
bool WriteCapture(const std::filesystem::path& dir, const std::string& baseName, const CaptureImages& images,
                  const CaptureMetadata& meta);

}  // namespace lc
