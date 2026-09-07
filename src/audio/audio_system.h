// Device layer on miniaudio (pinned submodule): plays the director's commands through a small
// voice pool. Spatialisation is done here from the director's rules (ComputeMix), not by the
// library: each voice is a mono clip with a volume and a pan. The device is optional: without one
// (no output device, or --no-audio) every call is a no-op and the cues still flow.
#pragma once

#include "audio/clips.h"
#include "audio/director.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lc::audio {

struct Volumes {
    float master = 0.8f;
    float effects = 1.0f;
    float ambience = 1.0f;
};

// True when a solid lies between the two points (the game's collision solids).
using OcclusionQuery = std::function<bool(math::Vec3 listener, math::Vec3 source)>;

struct AudioStats {
    bool deviceReady = false;
    std::string deviceName;
    std::uint32_t sampleRate = 0;
    std::uint32_t activeVoices = 0;
    std::uint32_t droppedOneShots = 0;  // Voice pool exhausted.
};

class AudioSystem {
public:
    static constexpr std::uint32_t kVoices = 24;

    // enableDevice = false builds the clips and the rules but opens no device (tests, --no-audio).
    explicit AudioSystem(bool enableDevice);
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    void SetVolumes(const Volumes& volumes);
    void SetPaused(bool paused);  // Silence while the menu is open (the world is frozen).
    void SetMixSettings(const MixSettings& settings) { mix_ = settings; }

    // Applies the commands, then refreshes every voice's volume and pan for the listener.
    void Update(const Listener& listener, const std::vector<Command>& commands, const OcclusionQuery& occluded);
    // Text cues queued by the commands since the last call.
    std::vector<std::string> TakeCues();
    // Silence everything (a restart or a scene reload); loops restart from the director's next update.
    void StopAll();

    const AudioStats& Stats() const { return stats_; }
    const std::vector<Clip>& Clips() const { return clips_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<Clip> clips_;
    MixSettings mix_;
    Volumes volumes_;
    bool paused_ = false;
    AudioStats stats_;
    std::vector<std::string> cues_;
};

}  // namespace lc::audio
