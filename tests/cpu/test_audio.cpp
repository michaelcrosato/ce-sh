// Sound rules without a device (spec §15): attenuation, stereo placement, occlusion, generated
// clips with provenance, and events that follow the world state.
#include "audio/audio_system.h"
#include "audio/clips.h"
#include "audio/director.h"
#include "lc_test.h"

#include <algorithm>
#include <cmath>

using namespace lc;
using namespace lc::audio;
using math::Vec3;

namespace {

std::size_t CountKind(const std::vector<Command>& cmds, Command::Kind kind) {
    return static_cast<std::size_t>(std::count_if(cmds.begin(), cmds.end(), [&](const Command& c) { return c.kind == kind; }));
}

std::size_t CountClip(const std::vector<Command>& cmds, ClipId clip, Command::Kind kind) {
    return static_cast<std::size_t>(std::count_if(cmds.begin(), cmds.end(), [&](const Command& c) { return c.kind == kind && c.clip == clip; }));
}

const Command* FindKey(const std::vector<Command>& cmds, Command::Kind kind, const std::string& key) {
    for (const Command& c : cmds) {
        if (c.kind == kind && c.key == key) return &c;
    }
    return nullptr;
}

bool HasCue(const std::vector<Command>& cmds, const std::string& text) {
    return std::any_of(cmds.begin(), cmds.end(), [&](const Command& c) { return c.kind == Command::Kind::Cue && c.text == text; });
}

WorldSnapshot Base() {
    WorldSnapshot s;
    s.playerFeet = {2.5f, 0.0f, 1.2f};
    s.doorPosition = {4.0f, 1.0f, 1.2f};
    s.lampPosition = {3.0f, 0.05f, 0.9f};
    s.threatPosition = {6.0f, 0.0f, 3.0f};
    s.fixtures = {{"fixture_a", {2.0f, 2.79f, 2.0f}, true, ClipId::FixtureHum},
                  {"hall_emergency", {6.8f, 2.79f, 5.0f}, true, ClipId::EmergencyHum},
                  {"fixture_b", {6.0f, 2.79f, 10.0f}, false, ClipId::FixtureHum},
                  {"lamp_face", {3.0f, 0.15f, 0.8f}, true, ClipId::LampHum}};
    return s;
}

}  // namespace

LC_TEST(audio_mix_attenuates_with_the_inverse_square_and_clamps_near_the_listener) {
    Listener l;
    l.position = {0.0f, 1.6f, 0.0f};
    MixSettings settings;
    const MixResult near = ComputeMix(l, {0.0f, 1.6f, -0.5f}, 1.0f, false, settings);
    LC_CHECK_NEAR(near.gain, 1.0f, 1e-6);
    const MixResult one = ComputeMix(l, {0.0f, 1.6f, -1.0f}, 1.0f, false, settings);
    LC_CHECK_NEAR(one.gain, 1.0f, 1e-6);
    const MixResult two = ComputeMix(l, {0.0f, 1.6f, -2.0f}, 1.0f, false, settings);
    LC_CHECK_NEAR(two.gain, 0.25f, 1e-6);
    const MixResult four = ComputeMix(l, {0.0f, 1.6f, -4.0f}, 0.5f, false, settings);
    LC_CHECK_NEAR(four.gain, 0.5f / 16.0f, 1e-6);
    LC_CHECK(four.audible);
    const MixResult far = ComputeMix(l, {0.0f, 1.6f, -31.0f}, 1.0f, false, settings);
    LC_CHECK(!far.audible);
    LC_CHECK_NEAR(far.gain, 0.0f, 1e-9);
}

LC_TEST(audio_mix_pans_from_the_listener_right_axis_and_occlusion_applies_the_documented_factor) {
    Listener l;
    l.position = {0.0f, 1.6f, 0.0f};
    l.forward = {0.0f, 0.0f, -1.0f};
    l.right = {1.0f, 0.0f, 0.0f};
    MixSettings settings;
    LC_CHECK_NEAR(ComputeMix(l, {3.0f, 1.6f, 0.0f}, 1.0f, false, settings).pan, 1.0f, 1e-6);
    LC_CHECK_NEAR(ComputeMix(l, {-3.0f, 1.6f, 0.0f}, 1.0f, false, settings).pan, -1.0f, 1e-6);
    LC_CHECK_NEAR(ComputeMix(l, {0.0f, 1.6f, -3.0f}, 1.0f, false, settings).pan, 0.0f, 1e-6);
    LC_CHECK_NEAR(ComputeMix(l, {3.0f, 1.6f, -3.0f}, 1.0f, false, settings).pan, std::sqrt(0.5f), 1e-5);
    // Turning the listener turns the pan: facing +X, a source at -Z is on the left.
    l.forward = {1.0f, 0.0f, 0.0f};
    l.right = {0.0f, 0.0f, -1.0f};
    LC_CHECK(ComputeMix(l, {0.0f, 1.6f, 3.0f}, 1.0f, false, settings).pan < -0.99f);
    // Occlusion.
    const MixResult open = ComputeMix(l, {2.0f, 1.6f, 0.0f}, 1.0f, false, settings);
    const MixResult blocked = ComputeMix(l, {2.0f, 1.6f, 0.0f}, 1.0f, true, settings);
    LC_CHECK_NEAR(blocked.gain, open.gain * settings.occlusionFactor, 1e-6);
    LC_CHECK_NEAR(blocked.pan, open.pan, 1e-6);
    // A source at the listener has no side.
    LC_CHECK_NEAR(ComputeMix(l, l.position, 1.0f, false, settings).pan, 0.0f, 1e-6);
}

LC_TEST(audio_clips_are_generated_deterministically_with_bounded_levels_and_provenance) {
    const std::vector<Clip> clips = GenerateAllClips();
    LC_CHECK_EQ(clips.size(), static_cast<std::size_t>(ClipId::Count));
    for (const Clip& c : clips) {
        LC_CHECK(!c.samples.empty());
        LC_CHECK(!c.provenance.empty());
        LC_CHECK(c.provenance.rfind("generated:", 0) == 0);
        float peak = 0.0f;
        bool finite = true;
        for (const float v : c.samples) {
            finite = finite && std::isfinite(v);
            peak = std::max(peak, std::fabs(v));
        }
        LC_CHECK(finite);
        LC_CHECK(peak <= 1.0f);
        LC_CHECK(peak > 0.05f);
        LC_CHECK(c.samples.size() <= 3 * kSampleRate);  // Every clip is under three seconds.
    }
    // Deterministic.
    const Clip a = GenerateClip(ClipId::DoorCreak);
    const Clip b = GenerateClip(ClipId::DoorCreak);
    LC_CHECK(a.samples == b.samples);
    // Loops: the hum's first and last samples meet (an integer number of periods).
    const Clip hum = GenerateClip(ClipId::FixtureHum);
    LC_CHECK(hum.loop);
    LC_CHECK_EQ(hum.samples.size(), static_cast<std::size_t>(kSampleRate));
    LC_CHECK(std::fabs(hum.samples.front() - hum.samples.back()) < 0.02f);
    const Clip tone = GenerateClip(ClipId::RoomTone);
    LC_CHECK(tone.loop);
    LC_CHECK_EQ(tone.samples.size(), static_cast<std::size_t>(2 * kSampleRate));
    // One-shots are not loops and the two footsteps differ.
    LC_CHECK(!GenerateClip(ClipId::LampClick).loop);
    LC_CHECK(GenerateClip(ClipId::FootstepA).samples != GenerateClip(ClipId::FootstepB).samples);
}

LC_TEST(audio_director_starts_hums_for_lit_fixtures_and_stops_them_when_a_circuit_goes_off) {
    Director d;
    WorldSnapshot s = Base();
    const std::vector<Command> first = d.Update(s, 1.0f / 60.0f);
    LC_CHECK(FindKey(first, Command::Kind::StartLoop, "room_tone") != nullptr);
    LC_CHECK(FindKey(first, Command::Kind::StartLoop, "hum:fixture_a") != nullptr);
    LC_CHECK(FindKey(first, Command::Kind::StartLoop, "hum:hall_emergency") != nullptr);
    LC_CHECK(FindKey(first, Command::Kind::StartLoop, "hum:lamp_face") != nullptr);
    LC_CHECK(FindKey(first, Command::Kind::StartLoop, "hum:fixture_b") == nullptr);  // Circuit b is off.
    LC_CHECK_EQ(FindKey(first, Command::Kind::StartLoop, "hum:hall_emergency")->clip, ClipId::EmergencyHum);
    LC_CHECK(FindKey(first, Command::Kind::StartLoop, "room_tone")->positional == false);
    LC_CHECK(FindKey(first, Command::Kind::StartLoop, "hum:fixture_a")->category == Category::Ambience);
    LC_CHECK_EQ(CountKind(first, Command::Kind::PlayOneShot), 0u);  // Nothing happened yet.

    // Steady state: no new commands for static fixtures.
    const std::vector<Command> steady = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountKind(steady, Command::Kind::StartLoop), 0u);
    LC_CHECK_EQ(CountKind(steady, Command::Kind::StopLoop), 0u);
    LC_CHECK_EQ(CountKind(steady, Command::Kind::MoveLoop), 0u);

    // Circuit a off, circuit b on.
    s.fixtures[0].on = false;
    s.fixtures[2].on = true;
    const std::vector<Command> switched = d.Update(s, 1.0f / 60.0f);
    LC_CHECK(FindKey(switched, Command::Kind::StopLoop, "hum:fixture_a") != nullptr);
    LC_CHECK(FindKey(switched, Command::Kind::StartLoop, "hum:fixture_b") != nullptr);
    LC_CHECK_EQ(CountKind(switched, Command::Kind::StopLoop), 1u);

    // A carried lamp moves its hum with the fixture transform.
    s.fixtures[3].position = {3.5f, 1.2f, 0.9f};
    const std::vector<Command> moved = d.Update(s, 1.0f / 60.0f);
    const Command* move = FindKey(moved, Command::Kind::MoveLoop, "hum:lamp_face");
    LC_REQUIRE(move != nullptr);
    LC_CHECK_NEAR(move->position.x, 3.5f, 1e-6);
    LC_CHECK_NEAR(move->position.y, 1.2f, 1e-6);
}

LC_TEST(audio_director_door_lamp_and_footstep_events_follow_the_world_state) {
    Director d;
    WorldSnapshot s = Base();
    (void)d.Update(s, 1.0f / 60.0f);

    // The door starts opening: creak and cue; reaches open: soft thud, no "shuts" cue.
    s.doorMoving = true;
    s.doorClosed = false;
    std::vector<Command> c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountClip(c, ClipId::DoorCreak, Command::Kind::PlayOneShot), 1u);
    LC_CHECK(HasCue(c, "The door creaks"));
    c = d.Update(s, 1.0f / 60.0f);  // Still moving: nothing new.
    LC_CHECK_EQ(CountKind(c, Command::Kind::PlayOneShot), 0u);
    s.doorMoving = false;
    s.doorOpen = true;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountClip(c, ClipId::DoorThud, Command::Kind::PlayOneShot), 1u);
    LC_CHECK(!HasCue(c, "The door shuts"));
    // Closing back: creak, then the shut cue.
    s.doorMoving = true;
    s.doorOpen = false;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountClip(c, ClipId::DoorCreak, Command::Kind::PlayOneShot), 1u);
    s.doorMoving = false;
    s.doorClosed = true;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountClip(c, ClipId::DoorThud, Command::Kind::PlayOneShot), 1u);
    LC_CHECK(HasCue(c, "The door shuts"));
    // Door sounds are positional at the door and occludable.
    const Command* creak = nullptr;
    for (const Command& cmd : c) {
        if (cmd.clip == ClipId::DoorThud) creak = &cmd;
    }
    LC_REQUIRE(creak != nullptr);
    LC_CHECK(creak->positional && creak->occludable);
    LC_CHECK_NEAR(creak->position.x, 4.0f, 1e-6);

    // Lamp: switch and handling.
    s.lampOn = false;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountClip(c, ClipId::LampClick, Command::Kind::PlayOneShot), 1u);
    LC_CHECK(HasCue(c, "Click: the lamp is off"));
    s.lampHeld = true;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountClip(c, ClipId::LampHandle, Command::Kind::PlayOneShot), 1u);
    LC_CHECK_EQ(CountClip(c, ClipId::LampClick, Command::Kind::PlayOneShot), 0u);

    // Footsteps: every 0.62 m, alternating variants, never occluded.
    std::size_t steps = 0;
    std::size_t variantA = 0;
    for (int i = 0; i < 100; ++i) {
        s.playerFeet.x += 0.02f;  // 2.0 m in total.
        c = d.Update(s, 1.0f / 60.0f);
        for (const Command& cmd : c) {
            if (cmd.kind == Command::Kind::PlayOneShot && (cmd.clip == ClipId::FootstepA || cmd.clip == ClipId::FootstepB)) {
                ++steps;
                if (cmd.clip == ClipId::FootstepA) ++variantA;
                LC_CHECK(!cmd.occludable);
            }
        }
    }
    LC_CHECK_EQ(steps, 3u);  // 2.0 / 0.62 = 3 whole steps.
    LC_CHECK_EQ(variantA, 2u);
    // Standing still: no steps.
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountKind(c, Command::Kind::PlayOneShot), 0u);
}

LC_TEST(audio_director_threat_loop_runs_only_while_it_moves_and_cues_when_near) {
    Director d;
    WorldSnapshot s = Base();
    (void)d.Update(s, 1.0f / 60.0f);
    // Stationary threat: no loop.
    std::vector<Command> c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK(FindKey(c, Command::Kind::StartLoop, "threat") == nullptr);
    // Moving 10 m away: the loop starts, no cue.
    s.threatPosition = {6.0f, 0.0f, 13.0f};
    c = d.Update(s, 1.0f / 60.0f);
    s.threatPosition.z -= 0.02f;
    c = d.Update(s, 1.0f / 60.0f);
    const Command* start = FindKey(c, Command::Kind::StartLoop, "threat");
    if (start == nullptr) start = FindKey(c, Command::Kind::MoveLoop, "threat");
    LC_REQUIRE(start != nullptr);
    LC_CHECK_NEAR(start->position.y, 1.0f, 1e-6);  // The body, not the feet.
    LC_CHECK(!HasCue(c, "A machine whirs nearby"));
    // Moving within 6 m behind a wall (occluded, farther than 3 m): no cue.
    s.threatPosition = {6.5f, 0.0f, 3.0f};  // 4.4 m from the player.
    s.threatOccluded = true;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK(!HasCue(c, "A machine whirs nearby"));
    // The same distance with a clear line: the cue, then not again within the period.
    s.threatOccluded = false;
    s.threatPosition.x -= 0.02f;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK(HasCue(c, "A machine whirs nearby"));
    s.threatPosition.x -= 0.02f;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK(!HasCue(c, "A machine whirs nearby"));
    LC_CHECK(FindKey(c, Command::Kind::MoveLoop, "threat") != nullptr);
    // After the period it cues again while still moving nearby; through a wall only within 3 m.
    s.threatPosition.x -= 0.02f;
    c = d.Update(s, Director::kThreatCuePeriod);
    LC_CHECK(HasCue(c, "A machine whirs nearby"));
    s.threatOccluded = true;
    s.threatPosition = {4.0f, 0.0f, 3.0f};  // 2.3 m away.
    c = d.Update(s, Director::kThreatCuePeriod);
    LC_CHECK(HasCue(c, "A machine whirs nearby"));
    // A frame without a new position keeps the loop (ticks are sparser than frames)...
    c = d.Update(s, 1.0f / 144.0f);
    LC_CHECK(FindKey(c, Command::Kind::StopLoop, "threat") == nullptr);
    // ...but a stop longer than the hold stops it (a stopped machine is silent).
    c = d.Update(s, Director::kMoveHold + 0.01f);
    LC_CHECK(FindKey(c, Command::Kind::StopLoop, "threat") != nullptr);
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK(FindKey(c, Command::Kind::StopLoop, "threat") == nullptr);
    // Objective events.
    s.objectiveChime = true;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountClip(c, ClipId::ExitChime, Command::Kind::PlayOneShot), 1u);
    LC_CHECK(HasCue(c, "A chime"));
    s.objectiveChime = false;
    s.catchSting = true;
    c = d.Update(s, 1.0f / 60.0f);
    LC_CHECK_EQ(CountClip(c, ClipId::CatchSting, Command::Kind::PlayOneShot), 1u);
    LC_CHECK(HasCue(c, "Caught: back to the last checkpoint"));
}

LC_TEST(audio_system_without_a_device_keeps_the_cues_and_drops_nothing_else) {
    AudioSystem audio(false);
    LC_CHECK(!audio.Stats().deviceReady);
    LC_CHECK_EQ(audio.Clips().size(), static_cast<std::size_t>(ClipId::Count));
    Director d;
    WorldSnapshot s = Base();
    Listener l;
    l.position = s.playerFeet + Vec3{0.0f, 1.6f, 0.0f};
    audio.Update(l, d.Update(s, 1.0f / 60.0f), nullptr);
    s.doorMoving = true;
    s.doorClosed = false;
    audio.Update(l, d.Update(s, 1.0f / 60.0f), [](Vec3, Vec3) { return true; });
    const std::vector<std::string> cues = audio.TakeCues();
    LC_REQUIRE(cues.size() == 1u);
    LC_CHECK_EQ(cues[0], std::string("The door creaks"));
    LC_CHECK(audio.TakeCues().empty());
    LC_CHECK_EQ(audio.Stats().activeVoices, 0u);
    LC_CHECK_EQ(audio.Stats().droppedOneShots, 0u);
}
