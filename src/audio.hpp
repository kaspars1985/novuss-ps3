#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Novuss {
namespace Audio {

// Procedural sound engine ported from the WebAudio synthesis in reference/main.js.
// All voices are mixed on demand by the audio callback (SDL2 or PS3 sys_audio),
// so the engine itself owns no device and no thread.
class SoundEngine {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kMaxVoices = 32;

    SoundEngine();
    ~SoundEngine();

    SoundEngine(const SoundEngine&) = delete;
    SoundEngine& operator=(const SoundEngine&) = delete;

    // Enable/disable output. When disabled, trigger() is a no-op and mix*() writes silence.
    void setEnabled(bool enabled);
    bool enabled() const;

    // Queue a sound. `type` is one of: "hit", "cushion", "pocket", "cue", "off".
    // `power` matches the JS `power` argument (impulse magnitude / speed).
    void trigger(const std::string& type, double power);

    // Drop all active voices (e.g. on match reset).
    void stopAll();

    // Menu background music (looped IMA ADPCM stream).
    // Loads the BGM from `path` (or the default asset when empty). Returns false on failure.
    bool loadMenuMusic(const std::string& path = "");
    // Start/stop looping menu music. Fades in/out smoothly.
    void setMenuMusic(bool active);
    bool menuMusic() const;

    // Mix active voices into an interleaved stereo buffer.
    // outInterleavedLR must hold numFrames * 2 samples.
    void mixStereoFloat(float* outInterleavedLR, int numFrames);
    void mixStereoS16(int16_t* outInterleavedLR, int numFrames);

private:
    struct Voice;

    // Must be called with lock_ held.
    void addClickLocked(double freq, double q, double dur, double gain);
    void addThumpLocked(double gain);
    void mixLocked(float* out, int numFrames);

    // Decode one IMA ADPCM stereo block (advances bgmFramePos_ / bgmSubFrame_).
    // Must be called with lock_ held.
    void decodeBgmBlockLocked();
    // Mix the menu music into the interleaved stereo buffer. Must be called with lock_ held.
    void mixMenuMusicLocked(float* out, int numFrames);

    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    bool enabled_;
    std::vector<float> noise_;          // 0.2 s of white noise at kSampleRate
    std::vector<Voice> voices_;         // active voices, at most kMaxVoices
    uint32_t rngState_;

    // Menu background music (looped IMA ADPCM stream).
    bool menuMusicActive_ = false;
    float menuMusicGain_ = 0.0f;
    std::vector<uint8_t> bgmData_;
    uint32_t bgmTotalFrames_ = 0;
    uint32_t bgmFramePos_ = 0;
    int bgmValpredL_ = 0, bgmIndexL_ = 0;
    int bgmValpredR_ = 0, bgmIndexR_ = 0;
    int16_t bgmCurrL_ = 0, bgmCurrR_ = 0;
    int16_t bgmNextL_ = 0, bgmNextR_ = 0;
    int bgmSubFrame_ = 0;
};

} // namespace Audio
} // namespace Novuss
