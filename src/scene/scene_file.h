// Versioned JSON scene files (spec §3, §16). A file describes materials, circuits, objects built
// from the generated kit, interaction sockets, waypoint paths, markers, the entities of the
// two-room proof, and (from M5) objective events. Parsing and validation come first and report
// every problem; the level is built only from a document with no problems, so a reload can never
// replace a working scene with a broken one. No baked lighting corrections exist in the format.
#pragma once

#include "scene/two_room_level.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lc {

// Schema 2 (M6): lists of doors and items, sockets that name the items they accept, circuits
// powered by an item in a socket, the fan object kind, and the objective as an ordered step list.
inline constexpr std::uint32_t kSceneSchemaVersion = 2;

// "Reasonable input-size limits" (spec §16). A file exceeding any of them is rejected with the limit named.
struct SceneFileLimits {
    std::uint64_t maxFileBytes = 4ull * 1024 * 1024;
    std::uint32_t maxObjects = 4096;
    std::uint32_t maxMaterials = 256;
    std::uint32_t maxCircuits = 64;
    std::uint32_t maxSockets = 64;
    std::uint32_t maxPaths = 32;
    std::uint32_t maxPathPoints = 256;
    std::uint32_t maxMarkers = 128;
    std::uint32_t maxDoors = 32;
    std::uint32_t maxItems = 16;
    std::uint32_t maxSteps = 32;
    std::uint32_t maxFanBlades = 12;
    float minDimension = 1e-4f;      // Smallest slab/box/quad/opening dimension in metres.
    float maxDimension = 1000.0f;
    float maxCoordinate = 10000.0f;  // Absolute bound of every position component.
    float maxRadiance = 1e6f;
};

struct SceneFileResult {
    std::optional<TwoRoomLevel> level;  // Present only when `errors` is empty.
    std::vector<std::string> errors;    // Every problem found; each names the list, identifier, or field.
    std::uint64_t contentHash = 0;      // FNV-1a of the document text (0 when the file could not be read).
    std::string sourceName;

    bool Ok() const { return level.has_value() && errors.empty(); }
    std::string ErrorText() const;      // One line per problem.
};

// Parses and validates the document text and builds the level when it is valid.
SceneFileResult ParseSceneFile(std::string_view text, std::string_view sourceName, const SceneFileLimits& limits = {});

// Resolves `path` against `assetRoot` (relative paths), refuses paths that leave the root after
// normalization, enforces the size limit, reads, and parses.
SceneFileResult LoadSceneFile(const std::filesystem::path& path, const std::filesystem::path& assetRoot,
                              const SceneFileLimits& limits = {});

// The approved asset root: an explicit override (the application sets <executable dir>/assets), else
// the LC_ASSET_ROOT compile definition (the CPU tests), else "assets" in the working directory.
void SetAssetRoot(std::filesystem::path root);
std::filesystem::path AssetRoot();

// True when `path`, normalized, lies inside `root` (normalized). Both may be relative to the working directory.
bool PathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root);

// FNV-1a over bytes (the same function the benchmark report uses for shaders and scenes).
std::uint64_t Fnv1a64(std::string_view bytes);

}  // namespace lc
