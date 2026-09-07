#include "lc_test.h"

#include "core/math/vec.h"
#include "scene/scene_file.h"
#include "scene/two_room_level.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string TwoRoomText() { return ReadFile(lc::AssetRoot() / lc::kTwoRoomSceneFile); }

// Replaces the first occurrence of `from` with `to` (the tests patch the golden document to make it invalid).
std::string Patch(std::string text, const std::string& from, const std::string& to) {
    const std::size_t pos = text.find(from);
    LC_REQUIRE(pos != std::string::npos);
    return text.replace(pos, from.size(), to);
}

bool HasErrorContaining(const lc::SceneFileResult& r, const std::string& needle) {
    for (const std::string& e : r.errors) {
        if (e.find(needle) != std::string::npos) return true;
    }
    return false;
}

const lc::Instance* FindInstance(const lc::Scene& s, const std::string& name) {
    for (const lc::Instance& i : s.Instances()) {
        if (i.name == name) return &i;
    }
    return nullptr;
}

}  // namespace

LC_TEST(scene_file_two_room_loads_and_matches_the_proof_layout) {
    const lc::SceneFileResult r = lc::LoadSceneFile(lc::kTwoRoomSceneFile, lc::AssetRoot());
    LC_REQUIRE(r.errors.empty());
    LC_REQUIRE(r.level.has_value());
    const lc::TwoRoomLevel& level = *r.level;
    const lc::Scene& s = level.description.scene;
    LC_CHECK_EQ(level.description.name, std::string("two_room"));
    LC_CHECK(r.contentHash != 0);
    LC_CHECK_EQ(level.contentHash, r.contentHash);
    // Materials: default grey + 15 from the file; the lamp face is the emitter the lamp toggles.
    LC_CHECK_EQ(s.Materials().size(), std::size_t{16});
    LC_CHECK_EQ(s.Materials()[level.lampMaterial].name, std::string("lamp_face"));
    LC_CHECK(s.Materials()[level.lampMaterial].emitterOn);
    // Circuit b is off: fixture_b stays a surface.
    bool fixtureBOff = false;
    for (const lc::Material& m : s.Materials()) {
        if (m.name == "fixture_b") fixtureBOff = m.type == lc::MaterialType::Emitter && !m.emitterOn;
    }
    LC_CHECK(fixtureBOff);
    LC_CHECK_EQ(level.circuits.size(), std::size_t{4});
    // Objects: 12 walls/floor/ceiling entries (two wall_opening objects give 3 instances each) + door + crate +
    // 3 emitter rectangles + sink + shelf + mirror + lamp housing + lamp face + threat body + head + the three
    // player body parts = 31 instances.
    LC_CHECK_EQ(s.Instances().size(), std::size_t{31});
    LC_CHECK(FindInstance(s, "floor") != nullptr);
    LC_CHECK_EQ(level.playerTorso.value, FindInstance(s, "player_torso")->id.value);
    LC_CHECK(FindInstance(s, "player_hand_r") != nullptr);
    LC_CHECK_EQ(level.hallFloorId.value, FindInstance(s, "floor")->id.value);
    LC_CHECK_EQ(level.door.id.value, FindInstance(s, "door")->id.value);
    LC_CHECK_NEAR(level.door.hinge.x, 4.025f, 1e-6f);  // Hinge line of the leaf: wall face + inset 0.005 + half thickness 0.02.
    LC_CHECK_NEAR(level.door.hinge.z, 0.75f, 1e-6f);
    LC_CHECK_EQ(level.mirror.value, FindInstance(s, "mirror")->id.value);
    LC_CHECK_EQ(level.threatBody.value, FindInstance(s, "threat_body")->id.value);
    LC_CHECK_EQ(level.lampHousing.value, FindInstance(s, "lamp_housing")->id.value);
    LC_CHECK_EQ(level.lampFace.value, FindInstance(s, "lamp_face")->id.value);
    // Sockets, path, markers.
    LC_CHECK_EQ(level.sockets.size(), std::size_t{2});
    LC_CHECK_EQ(level.floorSocket.name, std::string("floor_a"));
    LC_CHECK_EQ(level.shelfSocket.name, std::string("shelf"));
    LC_CHECK_NEAR(level.shelfSocket.position.x, 4.25f, 1e-6f);
    LC_CHECK_EQ(level.threatPath.size(), std::size_t{2});
    LC_CHECK_NEAR(level.threatSpeed, 1.2f, 1e-6f);
    LC_CHECK_NEAR(level.threatCheckPosition.x, 7.0f, 1e-6f);
    LC_CHECK_NEAR(level.playerStart.position.x, 2.5f, 1e-6f);
    LC_CHECK_NEAR(level.playerStart.yaw, -1.5707963f, 1e-6f);
    // The mirror aim: its normal bisects the directions to the check eye and the threat aim point,
    // exactly as the C++ builder derived it, and the check point lies on the reflecting face.
    const lc::math::Vec3 centre{6.4f, 1.5f, 12.0f};
    const lc::math::Vec3 eye = level.mirrorCheckCamera.position + lc::math::Vec3{0.0f, 1.6f, 0.0f};
    const lc::math::Vec3 aim = level.threatCheckPosition + lc::math::Vec3{0.0f, 0.9f, 0.0f};
    const lc::math::Vec3 expectedNormal = lc::math::Normalize(lc::math::Normalize(eye - centre) + lc::math::Normalize(aim - centre));
    LC_CHECK_NEAR(lc::math::Length(level.mirrorNormal + expectedNormal), 0.0f, 1e-5f);  // mirrorNormal = -normal (faces the room).
    LC_CHECK_NEAR(lc::math::Length(level.mirrorCheckPoint - (centre - expectedNormal * 0.01f)), 0.0f, 1e-5f);
    LC_CHECK_NEAR(std::fabs(lc::math::Dot(level.mirrorNormal, eye - level.mirrorCheckPoint)) > 0.0f ? 1.0f : 0.0f, 1.0f, 0.0f);
    // Colliders: static kit parts yes, the lamp face quad and emitter rectangles no.
    bool floorCollides = false;
    bool faceCollides = false;
    for (const lc::InstanceId id : level.colliders) {
        floorCollides = floorCollides || id.value == level.hallFloorId.value;
        faceCollides = faceCollides || id.value == level.lampFace.value;
    }
    LC_CHECK(floorCollides);
    LC_CHECK(!faceCollides);
    // Loading twice gives the same content hash and the same instance count (a reload is deterministic).
    const lc::SceneFileResult again = lc::LoadSceneFile(lc::kTwoRoomSceneFile, lc::AssetRoot());
    LC_REQUIRE(again.level.has_value());
    LC_CHECK_EQ(again.contentHash, r.contentHash);
    LC_CHECK_EQ(again.level->description.scene.ContentHash(), s.ContentHash());
}

LC_TEST(scene_file_rejects_every_class_of_problem_with_a_named_message) {
    const std::string golden = TwoRoomText();
    LC_REQUIRE(!golden.empty());

    auto expectError = [&](const std::string& text, const std::string& needle) {
        const lc::SceneFileResult r = lc::ParseSceneFile(text, "patched");
        LC_CHECK(!r.level.has_value());
        LC_CHECK(HasErrorContaining(r, needle));
        if (!HasErrorContaining(r, needle)) {
            for (const std::string& e : r.errors) std::printf("    error: %s\n", e.c_str());
        }
    };
    expectError(Patch(golden, "\"schema\": 1", "\"schema\": 2"), "schema");
    expectError(golden + "x", "JSON");
    expectError(Patch(golden, "\"id\": \"crate\", \"kind\": \"box\"", "\"id\": \"floor\", \"kind\": \"box\""), "duplicate id 'floor'");
    expectError(Patch(golden, "\"material\": \"crate\"}", "\"material\": \"missing\"}"), "material 'missing' does not exist");
    expectError(Patch(golden, "\"reflectance\": [0.6, 0.35, 0.15]", "\"reflectance\": [0.6, 1.35, 0.15]"), "outside [0, 1]");
    expectError(Patch(golden, "\"circuit\": \"b\"", "\"circuit\": \"nowhere\""), "circuit 'nowhere' does not exist");
    expectError(Patch(golden, "\"half\": [0.4, 0.4, 0.4], \"centre\": [0.8, 0.4, 3.0]", "\"half\": [0.4, 0.4, 0.4]"), "needs a 'centre'");
    expectError(Patch(golden, "\"max\": [8.15, 0.0, 12.3]", "\"max\": [8.15, -0.15, 12.3]"), "extent along y");
    expectError(Patch(golden, "\"centre\": 1.2, \"width\": 0.9", "\"centre\": 0.3, \"width\": 0.9"), "leaves no wall");
    expectError(Patch(golden, "\"facing\": [0.0, -1.0, 0.0], \"material\": \"fixture_a\"", "\"facing\": [0.0, -0.5, 0.5], \"material\": \"fixture_a\""), "axis directions");
    expectError(Patch(golden, "\"start\": \"player_start\"", "\"start\": \"nowhere\""), "marker 'nowhere' does not exist");
    expectError(Patch(golden, "\"kind\": \"exit\", \"marker\": \"exit\"", "\"kind\": \"portal\", \"marker\": \"exit\""), "unknown kind 'portal'");
    expectError(Patch(golden, "\"kind\": \"exit\", \"marker\": \"exit\"", "\"kind\": \"exit\", \"marker\": \"nowhere\""), "marker 'nowhere' does not exist");
    expectError(Patch(golden, "{\"id\": \"escape\", \"kind\": \"exit\", \"marker\": \"exit\", \"radius\": 0.8}",
                      "{\"id\": \"escape\", \"kind\": \"exit\", \"marker\": \"exit\", \"radius\": 0.8}, {\"id\": \"again\", \"kind\": \"exit\", \"marker\": \"exit\"}"),
                "only one exit");
    expectError(Patch(golden, "\"speed\": 1.2", "\"speed\": 0"), "'speed'");
    expectError(Patch(golden, "\"id\": \"floor\"", "\"id\": \"Floor\""), "lowercase");
}

LC_TEST(scene_file_limits_and_asset_root_containment) {
    lc::SceneFileLimits tight;
    tight.maxObjects = 3;
    const lc::SceneFileResult tooMany = lc::ParseSceneFile(TwoRoomText(), "limits", tight);
    LC_CHECK(!tooMany.level.has_value());
    LC_CHECK(HasErrorContaining(tooMany, "exceed the limit of 3"));

    lc::SceneFileLimits tiny;
    tiny.maxFileBytes = 16;
    LC_CHECK(HasErrorContaining(lc::ParseSceneFile(TwoRoomText(), "limits", tiny), "scene file limit"));
    LC_CHECK(HasErrorContaining(lc::LoadSceneFile(lc::kTwoRoomSceneFile, lc::AssetRoot(), tiny), "scene file limit"));

    // Paths must stay inside the asset root: a parent traversal and an absolute path elsewhere are refused.
    LC_CHECK(HasErrorContaining(lc::LoadSceneFile("../tests/replay/t05_mirror_threat.json", lc::AssetRoot()), "outside the asset root"));
    LC_CHECK(HasErrorContaining(lc::LoadSceneFile(std::filesystem::temp_directory_path() / "x.json", lc::AssetRoot()), "outside the asset root"));
    LC_CHECK(lc::PathWithinRoot(lc::AssetRoot() / "scenes" / "two_room.json", lc::AssetRoot()));
    LC_CHECK(!lc::PathWithinRoot(lc::AssetRoot() / ".." / "x.json", lc::AssetRoot()));
    LC_CHECK(HasErrorContaining(lc::LoadSceneFile("scenes/does_not_exist.json", lc::AssetRoot()), "cannot open"));
}
