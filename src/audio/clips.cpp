#include "audio/clips.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lc::audio {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kRate = static_cast<float>(kSampleRate);

// SplitMix64 stream: deterministic noise for every clip.
struct Noise {
    std::uint64_t state;
    explicit Noise(std::uint64_t seed) : state(seed) {}
    float Uniform() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<float>(z >> 40) * (1.0f / 16777216.0f);
    }
    float Signed() { return Uniform() * 2.0f - 1.0f; }
};

std::size_t Frames(float seconds) { return static_cast<std::size_t>(std::lround(seconds * kRate)); }

void ScaleToPeak(std::vector<float>& s, float peak) {
    float maxAbs = 0.0f;
    for (const float v : s) maxAbs = std::max(maxAbs, std::fabs(v));
    if (maxAbs <= 0.0f) return;
    const float k = peak / maxAbs;
    for (float& v : s) v *= k;
}

// One-pole low-pass, coefficient from the cutoff frequency.
struct LowPass {
    float a;
    float y = 0.0f;
    explicit LowPass(float cutoffHz) : a(1.0f - std::exp(-2.0f * kPi * cutoffHz / kRate)) {}
    float Step(float x) {
        y += a * (x - y);
        return y;
    }
};

// Steady tone with harmonics and a slow amplitude wobble; seamless when fundamental * seconds and
// wobbleHz * seconds are integers.
std::vector<float> Tone(float fundamental, const std::vector<float>& harmonics, float wobbleHz, float wobbleDepth, float seconds) {
    std::vector<float> s(Frames(seconds));
    for (std::size_t i = 0; i < s.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        float v = 0.0f;
        for (std::size_t k = 0; k < harmonics.size(); ++k) {
            v += harmonics[k] * std::sin(2.0f * kPi * fundamental * static_cast<float>(k + 1) * t);
        }
        const float wobble = 1.0f - wobbleDepth * (0.5f - 0.5f * std::cos(2.0f * kPi * wobbleHz * t));
        s[i] = v * wobble;
    }
    return s;
}

// Low-passed noise with an attack and an exponential decay.
std::vector<float> Burst(float seconds, float cutoffHz, float attackSeconds, float decayTau, std::uint64_t seed) {
    std::vector<float> s(Frames(seconds));
    Noise noise(seed);
    LowPass lp(cutoffHz);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float env = std::min(1.0f, attackSeconds > 0.0f ? t / attackSeconds : 1.0f) * std::exp(-t / decayTau);
        s[i] = lp.Step(noise.Signed()) * env;
    }
    return s;
}

// Decaying sine.
std::vector<float> Ping(float hz, float seconds, float decayTau, float startSeconds = 0.0f) {
    std::vector<float> s(Frames(seconds));
    for (std::size_t i = 0; i < s.size(); ++i) {
        const float t = static_cast<float>(i) / kRate - startSeconds;
        if (t < 0.0f) continue;
        s[i] = std::sin(2.0f * kPi * hz * t) * std::exp(-t / decayTau);
    }
    return s;
}

void Mix(std::vector<float>& into, const std::vector<float>& from, float gain) {
    if (into.size() < from.size()) into.resize(from.size(), 0.0f);
    for (std::size_t i = 0; i < from.size(); ++i) into[i] += from[i] * gain;
}

// Crossfades the tail into the head so a noise loop has no seam: keeps the first (n - fade) samples.
void MakeSeamless(std::vector<float>& s, float fadeSeconds) {
    const std::size_t fade = std::min(Frames(fadeSeconds), s.size() / 2);
    const std::size_t keep = s.size() - fade;
    for (std::size_t i = 0; i < fade; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(fade);
        s[i] = s[i] * t + s[keep + i] * (1.0f - t);
    }
    s.resize(keep);
}

// Frequency sweep with harmonics and a grainy amplitude (low-passed noise): the creak.
std::vector<float> Creak(float f0, float f1, float seconds, std::uint64_t seed) {
    std::vector<float> s(Frames(seconds));
    Noise noise(seed);
    LowPass grain(40.0f);
    float phase = 0.0f;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float u = t / seconds;
        const float f = f0 + (f1 - f0) * u;
        phase += 2.0f * kPi * f / kRate;
        const float g = 0.55f + 0.45f * grain.Step(noise.Signed()) * 4.0f;  // Grainy amplitude.
        const float env = std::sin(kPi * std::min(1.0f, u * 1.15f));       // Swell and fade.
        s[i] = (0.6f * std::sin(phase) + 0.3f * std::sin(2.0f * phase) + 0.15f * std::sin(3.0f * phase)) * std::clamp(g, 0.1f, 1.0f) * env;
    }
    return s;
}

}  // namespace

const char* ClipName(ClipId id) {
    switch (id) {
        case ClipId::FixtureHum: return "fixture_hum";
        case ClipId::EmergencyHum: return "emergency_hum";
        case ClipId::LampHum: return "lamp_hum";
        case ClipId::FanLoop: return "fan_loop";
        case ClipId::RoomTone: return "room_tone";
        case ClipId::ThreatMove: return "threat_move";
        case ClipId::DoorCreak: return "door_creak";
        case ClipId::DoorThud: return "door_thud";
        case ClipId::LampClick: return "lamp_click";
        case ClipId::LampHandle: return "lamp_handle";
        case ClipId::FootstepA: return "footstep_a";
        case ClipId::FootstepB: return "footstep_b";
        case ClipId::ExitChime: return "exit_chime";
        case ClipId::CatchSting: return "catch_sting";
        case ClipId::Count: break;
    }
    return "unknown";
}

Clip GenerateClip(ClipId id) {
    Clip c;
    c.id = id;
    c.name = ClipName(id);
    switch (id) {
        case ClipId::FixtureHum:
            c.loop = true;
            c.samples = Tone(100.0f, {1.0f, 0.5f, 0.25f, 0.12f, 0.06f}, 2.0f, 0.12f, 1.0f);
            ScaleToPeak(c.samples, 0.3f);
            c.provenance = "generated: 100 Hz tone with harmonics 1/0.5/0.25/0.12/0.06, 2 Hz amplitude wobble 12 %, 1.0 s seamless loop, peak 0.3";
            break;
        case ClipId::EmergencyHum:
            c.loop = true;
            c.samples = Tone(120.0f, {0.6f, 0.2f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.15f}, 3.0f, 0.2f, 1.0f);
            ScaleToPeak(c.samples, 0.25f);
            c.provenance = "generated: 120 Hz tone with harmonics 0.6/0.2/0.1 and an 8th at 0.15, 3 Hz wobble 20 %, 1.0 s seamless loop, peak 0.25";
            break;
        case ClipId::LampHum:
            c.loop = true;
            c.samples = Tone(200.0f, {0.5f, 0.15f, 0.05f}, 4.0f, 0.08f, 1.0f);
            ScaleToPeak(c.samples, 0.18f);
            c.provenance = "generated: 200 Hz tone with harmonics 0.5/0.15/0.05, 4 Hz wobble 8 %, 1.0 s seamless loop, peak 0.18";
            break;
        case ClipId::FanLoop: {
            c.loop = true;
            c.samples = Tone(40.0f, {1.0f, 0.3f}, 12.0f, 0.5f, 1.0f);
            std::vector<float> air = Burst(1.2f, 900.0f, 0.0f, 1e9f, 0x0FA1);
            MakeSeamless(air, 0.2f);
            Mix(c.samples, air, 0.4f);
            ScaleToPeak(c.samples, 0.3f);
            c.provenance = "generated: 40 Hz rumble (harmonics 1/0.3) with a 12 Hz blade-pass wobble 50 % plus 900 Hz low-passed noise 0.4, 1.0 s seamless loop, peak 0.3";
            break;
        }
        case ClipId::RoomTone:
            c.loop = true;
            c.samples = Burst(2.25f, 150.0f, 0.0f, 1e9f, 0x0A11);
            MakeSeamless(c.samples, 0.25f);
            ScaleToPeak(c.samples, 0.12f);
            c.provenance = "generated: white noise (SplitMix64 seed 0xA11) low-passed at 150 Hz, 2.0 s loop with a 0.25 s crossfade, peak 0.12";
            break;
        case ClipId::ThreatMove: {
            c.loop = true;
            c.samples = Tone(220.0f, {1.0f, 0.4f, 0.2f, 0.1f}, 8.0f, 0.4f, 0.5f);
            for (int tick = 0; tick < 2; ++tick) {
                std::vector<float> click = Burst(0.02f, 3000.0f, 0.0005f, 0.004f, 0x7E4 + static_cast<std::uint64_t>(tick));
                std::vector<float> placed(Frames(0.5f), 0.0f);
                const std::size_t at = Frames(0.25f * static_cast<float>(tick));
                for (std::size_t i = 0; i < click.size() && at + i < placed.size(); ++i) placed[at + i] = click[i];
                Mix(c.samples, placed, 0.9f);
            }
            ScaleToPeak(c.samples, 0.45f);
            c.provenance = "generated: 220 Hz motor tone (harmonics 1/0.4/0.2/0.1) with an 8 Hz tremolo 40 % and two 4 ms clicks per 0.5 s seamless loop, peak 0.45";
            break;
        }
        case ClipId::DoorCreak:
            c.samples = Creak(170.0f, 95.0f, 0.9f, 0xD00C);
            ScaleToPeak(c.samples, 0.6f);
            c.provenance = "generated: 170 -> 95 Hz sweep with harmonics 0.6/0.3/0.15, grainy amplitude from 40 Hz low-passed noise, sine envelope, 0.9 s, peak 0.6";
            break;
        case ClipId::DoorThud:
            c.samples = Ping(55.0f, 0.35f, 0.09f);
            Mix(c.samples, Burst(0.2f, 300.0f, 0.002f, 0.04f, 0x7D0D), 0.8f);
            ScaleToPeak(c.samples, 0.7f);
            c.provenance = "generated: 55 Hz sine decaying (tau 90 ms) plus 300 Hz low-passed noise (tau 40 ms), 0.35 s, peak 0.7";
            break;
        case ClipId::LampClick:
            c.samples = Burst(0.03f, 6000.0f, 0.0f, 0.003f, 0xC11C);
            Mix(c.samples, Ping(1500.0f, 0.05f, 0.012f), 0.5f);
            ScaleToPeak(c.samples, 0.6f);
            c.provenance = "generated: 3 ms noise click (6 kHz low-pass) plus a 1.5 kHz sine decaying (tau 12 ms), 50 ms, peak 0.6";
            break;
        case ClipId::LampHandle:
            c.samples = Ping(400.0f, 0.25f, 0.05f);
            Mix(c.samples, Burst(0.12f, 2500.0f, 0.001f, 0.02f, 0x4A9D), 0.7f);
            Mix(c.samples, Burst(0.2f, 2000.0f, 0.001f, 0.015f, 0x4A9E), 0.5f);
            ScaleToPeak(c.samples, 0.55f);
            c.provenance = "generated: 400 Hz knock (tau 50 ms) plus two short noise clicks (2.5 kHz / 2 kHz low-pass), 0.25 s, peak 0.55";
            break;
        case ClipId::FootstepA:
            c.samples = Burst(0.18f, 500.0f, 0.005f, 0.06f, 0xF007A);
            ScaleToPeak(c.samples, 0.5f);
            c.provenance = "generated: noise burst low-passed at 500 Hz, 5 ms attack, 60 ms decay, 0.18 s, peak 0.5 (seed 0xF007A)";
            break;
        case ClipId::FootstepB:
            c.samples = Burst(0.18f, 420.0f, 0.004f, 0.07f, 0xF007B);
            ScaleToPeak(c.samples, 0.5f);
            c.provenance = "generated: noise burst low-passed at 420 Hz, 4 ms attack, 70 ms decay, 0.18 s, peak 0.5 (seed 0xF007B)";
            break;
        case ClipId::ExitChime:
            c.samples = Ping(660.0f, 1.3f, 0.45f);
            Mix(c.samples, Ping(990.0f, 1.3f, 0.5f, 0.18f), 0.8f);
            ScaleToPeak(c.samples, 0.5f);
            c.provenance = "generated: 660 Hz and 990 Hz sines (the second 180 ms later) decaying with tau 0.45-0.5 s, 1.3 s, peak 0.5";
            break;
        case ClipId::CatchSting:
            c.samples = Creak(420.0f, 60.0f, 0.6f, 0x5716);
            Mix(c.samples, Burst(0.6f, 800.0f, 0.0f, 0.25f, 0x5717), 0.6f);
            ScaleToPeak(c.samples, 0.8f);
            c.provenance = "generated: 420 -> 60 Hz sweep with harmonics and grain plus 800 Hz low-passed noise (tau 0.25 s), 0.6 s, peak 0.8";
            break;
        case ClipId::Count:
            break;
    }
    return c;
}

std::vector<Clip> GenerateAllClips() {
    std::vector<Clip> clips;
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(ClipId::Count); ++i) {
        clips.push_back(GenerateClip(static_cast<ClipId>(i)));
    }
    return clips;
}

}  // namespace lc::audio
