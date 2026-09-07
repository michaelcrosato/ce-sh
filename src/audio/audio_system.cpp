#include "audio/audio_system.h"

#include "core/log.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lc::audio {

using math::Vec3;

struct AudioSystem::Impl {
    struct Voice {
        ma_audio_buffer_ref ref{};
        ma_sound sound{};
        bool inUse = false;
        bool loop = false;
        std::string key;
        ClipId clip = ClipId::Count;
        Category category = Category::Effects;
        Vec3 position;
        float gain = 1.0f;
        bool positional = true;
        bool occludable = true;
    };

    bool ready = false;
    ma_engine engine{};
    ma_sound_group effects{};
    ma_sound_group ambience{};
    std::vector<Voice> voices;  // Fixed size: ma_sound objects must not move.

    void Release(Voice& v) {
        if (!v.inUse) return;
        ma_sound_uninit(&v.sound);
        ma_audio_buffer_ref_uninit(&v.ref);
        v.inUse = false;
        v.key.clear();
    }

    Voice* Acquire(const Clip& clip, bool loop, Category category) {
        for (Voice& v : voices) {
            if (v.inUse) continue;
            if (ma_audio_buffer_ref_init(ma_format_f32, 1, clip.samples.data(), clip.samples.size(), &v.ref) != MA_SUCCESS) {
                return nullptr;
            }
            const ma_uint32 flags = MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION | (loop ? MA_SOUND_FLAG_LOOPING : 0u);
            ma_sound_group* group = category == Category::Ambience ? &ambience : &effects;
            if (ma_sound_init_from_data_source(&engine, &v.ref, flags, group, &v.sound) != MA_SUCCESS) {
                ma_audio_buffer_ref_uninit(&v.ref);
                return nullptr;
            }
            ma_sound_set_pan_mode(&v.sound, ma_pan_mode_pan);
            v.inUse = true;
            v.loop = loop;
            v.clip = clip.id;
            v.category = category;
            return &v;
        }
        return nullptr;
    }

    Voice* FindLoop(const std::string& key) {
        for (Voice& v : voices) {
            if (v.inUse && v.loop && v.key == key) return &v;
        }
        return nullptr;
    }
};

AudioSystem::AudioSystem(bool enableDevice) : impl_(std::make_unique<Impl>()), clips_(GenerateAllClips()) {
    std::size_t totalSamples = 0;
    for (const Clip& c : clips_) totalSamples += c.samples.size();
    log::Info("audio: {} generated clips, {:.2f} s of mono 48 kHz audio", clips_.size(), static_cast<double>(totalSamples) / kSampleRate);
    if (!enableDevice) {
        log::Info("audio: no device requested; sounds are silent, cues still flow");
        return;
    }
    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;
    config.sampleRate = kSampleRate;
    config.listenerCount = 1;
    const ma_result r = ma_engine_init(&config, &impl_->engine);
    if (r != MA_SUCCESS) {
        log::Warn("audio: no output device ({}); playing silently", ma_result_description(r));
        return;
    }
    if (ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->effects) != MA_SUCCESS ||
        ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->ambience) != MA_SUCCESS) {
        log::Warn("audio: sound groups failed; playing silently");
        ma_engine_uninit(&impl_->engine);
        return;
    }
    impl_->voices.resize(kVoices);
    impl_->ready = true;
    stats_.deviceReady = true;
    stats_.sampleRate = ma_engine_get_sample_rate(&impl_->engine);
    char name[256] = {};
    if (ma_device* device = ma_engine_get_device(&impl_->engine)) {
        if (ma_device_get_name(device, ma_device_type_playback, name, sizeof(name), nullptr) == MA_SUCCESS) stats_.deviceName = name;
    }
    SetVolumes(volumes_);
    log::Info("audio: miniaudio {} on '{}' at {} Hz, {} voices", MA_VERSION_STRING, stats_.deviceName, stats_.sampleRate, kVoices);
}

AudioSystem::~AudioSystem() {
    if (!impl_->ready) return;
    for (auto& v : impl_->voices) impl_->Release(v);
    ma_sound_group_uninit(&impl_->ambience);
    ma_sound_group_uninit(&impl_->effects);
    ma_engine_uninit(&impl_->engine);
}

void AudioSystem::SetVolumes(const Volumes& volumes) {
    volumes_ = volumes;
    if (!impl_->ready) return;
    ma_engine_set_volume(&impl_->engine, paused_ ? 0.0f : std::clamp(volumes.master, 0.0f, 1.0f));
    ma_sound_group_set_volume(&impl_->effects, std::clamp(volumes.effects, 0.0f, 1.0f));
    ma_sound_group_set_volume(&impl_->ambience, std::clamp(volumes.ambience, 0.0f, 1.0f));
}

void AudioSystem::SetPaused(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    SetVolumes(volumes_);
}

void AudioSystem::StopAll() {
    if (!impl_->ready) return;
    for (auto& v : impl_->voices) impl_->Release(v);
}

void AudioSystem::Update(const Listener& listener, const std::vector<Command>& commands, const OcclusionQuery& occluded) {
    for (const Command& c : commands) {
        if (c.kind == Command::Kind::Cue) {
            cues_.push_back(c.text);
            continue;
        }
        if (!impl_->ready) continue;
        const Clip& clip = clips_[static_cast<std::size_t>(c.clip)];
        switch (c.kind) {
            case Command::Kind::StartLoop: {
                if (impl_->FindLoop(c.key) != nullptr) break;
                Impl::Voice* v = impl_->Acquire(clip, true, c.category);
                if (v == nullptr) {
                    log::Warn("audio: no free voice for loop '{}'", c.key);
                    break;
                }
                v->key = c.key;
                v->position = c.position;
                v->gain = c.gain;
                v->positional = c.positional;
                v->occludable = c.occludable;
                ma_sound_set_volume(&v->sound, 0.0f);  // The refresh below sets the real level before the first mix.
                ma_sound_start(&v->sound);
                break;
            }
            case Command::Kind::StopLoop:
                if (Impl::Voice* v = impl_->FindLoop(c.key)) impl_->Release(*v);
                break;
            case Command::Kind::MoveLoop:
                if (Impl::Voice* v = impl_->FindLoop(c.key)) {
                    v->position = c.position;
                    v->gain = c.gain;
                }
                break;
            case Command::Kind::PlayOneShot: {
                Impl::Voice* v = impl_->Acquire(clip, false, c.category);
                if (v == nullptr) {
                    ++stats_.droppedOneShots;
                    break;
                }
                v->position = c.position;
                v->gain = c.gain;
                v->positional = c.positional;
                v->occludable = c.occludable;
                const MixResult m = c.positional ? ComputeMix(listener, c.position, c.gain, c.occludable && occluded && occluded(listener.position, c.position), mix_)
                                                 : MixResult{c.gain, 0.0f, 0.0f, true};
                ma_sound_set_volume(&v->sound, m.gain);
                ma_sound_set_pan(&v->sound, m.pan);
                ma_sound_start(&v->sound);
                break;
            }
            case Command::Kind::Cue:
                break;
        }
    }
    if (!impl_->ready) return;

    // Refresh every live voice: distance, pan, and occlusion follow the listener each frame.
    std::uint32_t active = 0;
    for (auto& v : impl_->voices) {
        if (!v.inUse) continue;
        if (!v.loop && ma_sound_at_end(&v.sound)) {
            impl_->Release(v);
            continue;
        }
        ++active;
        if (!v.positional) {
            ma_sound_set_volume(&v.sound, v.gain);
            ma_sound_set_pan(&v.sound, 0.0f);
            continue;
        }
        const bool blocked = v.occludable && occluded && occluded(listener.position, v.position);
        const MixResult m = ComputeMix(listener, v.position, v.gain, blocked, mix_);
        ma_sound_set_volume(&v.sound, m.audible ? m.gain : 0.0f);
        ma_sound_set_pan(&v.sound, m.pan);
    }
    stats_.activeVoices = active;
}

std::vector<std::string> AudioSystem::TakeCues() {
    std::vector<std::string> out;
    out.swap(cues_);
    return out;
}

}  // namespace lc::audio
