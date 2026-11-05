#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace Novuss {
namespace Audio {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Lightweight RAII spinlock guard for std::atomic_flag.
struct SpinGuard {
    std::atomic_flag& f;
    explicit SpinGuard(std::atomic_flag& flag) : f(flag) {
        while (f.test_and_set(std::memory_order_acquire)) {}
    }
    ~SpinGuard() { f.clear(std::memory_order_release); }
    SpinGuard(const SpinGuard&) = delete;
    SpinGuard& operator=(const SpinGuard&) = delete;
};

// Biquad bandpass coefficients (Robert Bristow-Johnson cookbook, constant 0 dB peak gain).
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

BiquadCoeffs makeBandpass(double sampleRate, double freq, double q) {
    BiquadCoeffs c;
    const double w0 = 2.0 * kPi * freq / sampleRate;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * std::max(q, 1e-6));
    const double a0 = 1.0 + alpha;
    c.b0 = alpha / a0;
    c.b1 = 0.0;
    c.b2 = -alpha / a0;
    c.a1 = (-2.0 * cosw0) / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
}

// Exponential ramp helper: value at t=0 is `from`, at t=dur is `to`.
inline double expRamp(double from, double to, double t, double dur) {
    if (dur <= 0.0) return to;
    const double u = std::min(1.0, std::max(0.0, t / dur));
    // Exponential interpolation in log space (matches WebAudio exponentialRampToValueAtTime).
    return from * std::pow(to / from, u);
}

// Standard IMA ADPCM step and index tables.
static const int kImaStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int kImaIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

inline int decodeImaNibble(uint8_t code, int& valpred, int& index) {
    int step = kImaStepTable[index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    if (code & 8) valpred -= diff;
    else valpred += diff;
    if (valpred > 32767) valpred = 32767;
    else if (valpred < -32768) valpred = -32768;
    index += kImaIndexTable[code & 0x0F];
    if (index < 0) index = 0;
    else if (index > 88) index = 88;
    return valpred;
}

} // namespace

struct SoundEngine::Voice {
    enum class Kind { Click, Thump };

    Kind kind = Kind::Click;
    double t = 0.0;          // elapsed seconds
    double dur = 0.0;        // total lifetime seconds

    // Click (bandpass-filtered noise).
    BiquadCoeffs coeffs;
    double z1 = 0.0, z2 = 0.0;   // transposed direct form II state
    double gainStart = 0.0;
    double gainEnd = 0.0001;
    double gainDur = 0.0;
    size_t noisePos = 0;

    // Thump (sine with exponential frequency sweep).
    double phase = 0.0;
    double freqStart = 170.0;
    double freqEnd = 60.0;
    double freqDur = 0.14;
    double thumpGainStart = 0.0;
    double thumpGainEnd = 0.0001;
    double thumpGainDur = 0.16;
};

SoundEngine::SoundEngine()
    : enabled_(true), rngState_(0x9E3779B9u) {
    // Pre-generate 0.2 s of white noise, matching the JS noise buffer.
    const size_t n = static_cast<size_t>(kSampleRate * 0.2);
    noise_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        // xorshift32 -> [-1, 1)
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;
        const double u = static_cast<double>(rngState_) / 4294967296.0;
        noise_[i] = static_cast<float>(u * 2.0 - 1.0);
    }
    voices_.reserve(kMaxVoices);
}

SoundEngine::~SoundEngine() = default;

void SoundEngine::setEnabled(bool enabled) {
    SpinGuard guard(lock_);
    enabled_ = enabled;
    if (!enabled_) voices_.clear();
}

bool SoundEngine::enabled() const {
    SpinGuard guard(lock_);
    return enabled_;
}

void SoundEngine::stopAll() {
    SpinGuard guard(lock_);
    voices_.clear();
}

bool SoundEngine::loadMenuMusic(const std::string& path) {
    std::vector<std::string> candidates;
    if (!path.empty()) candidates.push_back(path);
    candidates.push_back("/dev_hdd0/game/NOVU00001/USRDIR/menu_bgm.adpcm");
    candidates.push_back("USRDIR/menu_bgm.adpcm");
    candidates.push_back("assets/menu_bgm.adpcm");
    FILE* f = nullptr;
    for (const auto& c : candidates) {
        f = fopen(c.c_str(), "rb");
        if (f) break;
    }
    if (!f) return false;
    uint8_t hdr[20];
    if (fread(hdr, 1, 20, f) != 20 || memcmp(hdr, "NBGM", 4) != 0) {
        fclose(f);
        return false;
    }
    // Explicit little-endian unpack of the frame count. Raw memcpy would
    // corrupt the value on big-endian targets (e.g. Cell PPU on PS3).
    uint32_t numFrames = static_cast<uint32_t>(hdr[16]) |
                        (static_cast<uint32_t>(hdr[17]) << 8) |
                        (static_cast<uint32_t>(hdr[18]) << 16) |
                        (static_cast<uint32_t>(hdr[19]) << 24);
    if (numFrames < 1000 || numFrames > 5 * 1024 * 1024) {
        fclose(f);
        return false;
    }
    std::vector<uint8_t> localData;
    try {
        localData.resize(numFrames);
    } catch (...) {
        fclose(f);
        return false;
    }
    size_t readCount = fread(localData.data(), 1, numFrames, f);
    fclose(f);
    if (readCount != numFrames) {
        return false;
    }

    {
        SpinGuard guard(lock_);
        bgmData_ = std::move(localData);
        bgmTotalFrames_ = numFrames;
        bgmFramePos_ = 0;
        bgmValpredL_ = bgmIndexL_ = 0;
        bgmValpredR_ = bgmIndexR_ = 0;
        bgmCurrL_ = bgmCurrR_ = 0;
        bgmNextL_ = bgmNextR_ = 0;
        bgmSubFrame_ = 0;
    }
    return true;
}

void SoundEngine::setMenuMusic(bool active) {
    SpinGuard guard(lock_);
    menuMusicActive_ = active;
}

bool SoundEngine::menuMusic() const {
    SpinGuard guard(lock_);
    return menuMusicActive_;
}

void SoundEngine::addClickLocked(double freq, double q, double dur, double gain) {
    if (voices_.size() >= static_cast<size_t>(kMaxVoices)) return;
    Voice v;
    v.kind = Voice::Kind::Click;
    v.t = 0.0;
    v.dur = dur + 0.02;   // matches src.stop(now + dur + 0.02)
    v.coeffs = makeBandpass(static_cast<double>(kSampleRate), freq, q);
    v.gainStart = gain;
    v.gainEnd = 0.0001;
    v.gainDur = dur;
    v.noisePos = 0;
    voices_.push_back(v);
}

void SoundEngine::addThumpLocked(double gain) {
    if (voices_.size() >= static_cast<size_t>(kMaxVoices)) return;
    Voice v;
    v.kind = Voice::Kind::Thump;
    v.t = 0.0;
    v.dur = 0.18;         // o.stop(now + 0.18)
    v.freqStart = 170.0;
    v.freqEnd = 60.0;
    v.freqDur = 0.14;
    v.thumpGainStart = gain;
    v.thumpGainEnd = 0.0001;
    v.thumpGainDur = 0.16;
    v.phase = 0.0;
    voices_.push_back(v);
}

void SoundEngine::trigger(const std::string& type, double power) {
    SpinGuard guard(lock_);
    if (!enabled_) return;

    if (type == "hit") {
        addClickLocked(2300.0, 2.5, 0.03, std::min(0.9, power / 3500.0));
    } else if (type == "cushion") {
        addClickLocked(650.0, 1.2, 0.05, std::min(0.6, power / 4500.0));
    } else if (type == "pocket") {
        addThumpLocked(0.4);
        addClickLocked(300.0, 1.0, 0.08, 0.15);
    } else if (type == "cue") {
        addClickLocked(3200.0, 3.0, 0.02, 0.25 + 0.4 * power);
    } else if (type == "off") {
        addClickLocked(900.0, 1.0, 0.12, 0.4);
    }
}

void SoundEngine::mixMenuMusicLocked(float* out, int numFrames) {
    if (!enabled_ || bgmData_.empty() || bgmTotalFrames_ == 0) return;

    // Menu: full volume (0.38). Gameplay: quiet ambient background (0.09).
    const float targetGain = menuMusicActive_ ? 0.38f : 0.09f;
    if (menuMusicGain_ < 0.0001f && targetGain < 0.0001f) return;

    for (int f = 0; f < numFrames; ++f) {
        menuMusicGain_ += (targetGain - menuMusicGain_) * 0.001f;
        float sampleL = 0.0f, sampleR = 0.0f;
        if (bgmSubFrame_ == 0) {
            bgmCurrL_ = bgmNextL_;
            bgmCurrR_ = bgmNextR_;
            if (bgmFramePos_ >= bgmTotalFrames_) {
                bgmFramePos_ = 0;
                bgmValpredL_ = bgmIndexL_ = 0;
                bgmValpredR_ = bgmIndexR_ = 0;
            }
            uint8_t byteVal = bgmData_[bgmFramePos_++];
            bgmNextL_ = decodeImaNibble(byteVal & 0x0F, bgmValpredL_, bgmIndexL_);
            bgmNextR_ = decodeImaNibble((byteVal >> 4) & 0x0F, bgmValpredR_, bgmIndexR_);
            sampleL = (bgmCurrL_ + bgmNextL_) * 0.5f;
            sampleR = (bgmCurrR_ + bgmNextR_) * 0.5f;
            bgmSubFrame_ = 1;
        } else {
            sampleL = bgmNextL_;
            sampleR = bgmNextR_;
            bgmSubFrame_ = 0;
        }
        out[f * 2 + 0] += (sampleL / 32768.0f) * menuMusicGain_;
        out[f * 2 + 1] += (sampleR / 32768.0f) * menuMusicGain_;
    }
}

void SoundEngine::mixLocked(float* out, int numFrames) {
    const double invRate = 1.0 / static_cast<double>(kSampleRate);
    const size_t noiseLen = noise_.size();

    for (int f = 0; f < numFrames; ++f) {
        double l = 0.0, r = 0.0;
        for (size_t i = 0; i < voices_.size();) {
            Voice& v = voices_[i];
            if (v.t >= v.dur) {
                voices_[i] = voices_.back();
                voices_.pop_back();
                continue;
            }

            double sample = 0.0;
            if (v.kind == Voice::Kind::Click) {
                const float x = noiseLen ? noise_[v.noisePos % noiseLen] : 0.0f;
                v.noisePos++;
                // Transposed direct form II biquad.
                const double y = v.coeffs.b0 * x + v.z1;
                v.z1 = v.coeffs.b1 * x - v.coeffs.a1 * y + v.z2;
                v.z2 = v.coeffs.b2 * x - v.coeffs.a2 * y;
                const double g = expRamp(v.gainStart, v.gainEnd, v.t, v.gainDur);
                sample = y * g;
            } else {
                const double freq = expRamp(v.freqStart, v.freqEnd, v.t, v.freqDur);
                v.phase += 2.0 * kPi * freq * invRate;
                if (v.phase > 2.0 * kPi) v.phase -= 2.0 * kPi;
                const double g = expRamp(v.thumpGainStart, v.thumpGainEnd, v.t, v.thumpGainDur);
                sample = std::sin(v.phase) * g;
            }

            l += sample;
            r += sample;
            v.t += invRate;
            ++i;
        }

        // Soft clip to avoid harsh distortion when many voices overlap.
        l = std::tanh(l);
        r = std::tanh(r);
        out[f * 2 + 0] = static_cast<float>(l);
        out[f * 2 + 1] = static_cast<float>(r);
    }

    mixMenuMusicLocked(out, numFrames);
}

void SoundEngine::mixStereoFloat(float* outInterleavedLR, int numFrames) {
    if (!outInterleavedLR || numFrames <= 0) return;
    SpinGuard guard(lock_);
    if (!enabled_ || (voices_.empty() && bgmData_.empty())) {
        std::memset(outInterleavedLR, 0, sizeof(float) * static_cast<size_t>(numFrames) * 2);
        return;
    }
    mixLocked(outInterleavedLR, numFrames);
}

void SoundEngine::mixStereoS16(int16_t* outInterleavedLR, int numFrames) {
    if (!outInterleavedLR || numFrames <= 0) return;
    SpinGuard guard(lock_);
    if (!enabled_ || (voices_.empty() && bgmData_.empty())) {
        std::memset(outInterleavedLR, 0, sizeof(int16_t) * static_cast<size_t>(numFrames) * 2);
        return;
    }

    // Mix into a small stack buffer in chunks to avoid a large allocation.
    constexpr int kChunk = 512;
    float buf[kChunk * 2];
    int remaining = numFrames;
    int offset = 0;
    while (remaining > 0) {
        const int n = std::min(remaining, kChunk);
        mixLocked(buf, n);
        for (int i = 0; i < n * 2; ++i) {
            float s = buf[i];
            if (s > 1.0f) s = 1.0f;
            else if (s < -1.0f) s = -1.0f;
            outInterleavedLR[offset + i] = static_cast<int16_t>(s * 32767.0f + (s >= 0.0f ? 0.5f : -0.5f));
        }
        offset += n * 2;
        remaining -= n;
    }
}

} // namespace Audio
} // namespace Novuss
