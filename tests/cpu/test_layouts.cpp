#include "lc_test.h"

#include "render/gpu_layouts.h"
#include "render/view_mode.h"

#include <cstddef>

LC_TEST(layouts_match_the_hlsl_contract) {
    LC_CHECK_EQ(sizeof(lc::gpu::FrameConstants), std::size_t{256});
    LC_CHECK_EQ(offsetof(lc::gpu::FrameConstants, cameraPosition), std::size_t{192});
    LC_CHECK_EQ(offsetof(lc::gpu::FrameConstants, renderSize), std::size_t{208});
    LC_CHECK_EQ(offsetof(lc::gpu::FrameConstants, seed), std::size_t{252});
    LC_CHECK_EQ(sizeof(lc::gpu::InstanceRecord), std::size_t{112});
    LC_CHECK_EQ(offsetof(lc::gpu::InstanceRecord, meshIndex), std::size_t{96});
    LC_CHECK_EQ(sizeof(lc::gpu::MeshRecord), std::size_t{16});
    LC_CHECK_EQ(sizeof(lc::gpu::HitInfoTexel), std::size_t{16});
}

LC_TEST(view_modes_round_trip_by_name) {
    LC_CHECK(lc::ViewModeFromName("ids") == lc::ViewMode::InstanceIds);
    LC_CHECK(lc::ViewModeFromName("normals") == lc::ViewMode::Normals);
    LC_CHECK(!lc::ViewModeFromName("bogus").has_value());
    LC_CHECK_EQ(lc::ViewModeName(lc::ViewMode::FrontFace), std::string_view("facing"));
    LC_CHECK_EQ(static_cast<std::uint32_t>(lc::ViewMode::PrimitiveIds), 5u);
}
