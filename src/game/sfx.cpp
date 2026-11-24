#include "game/sfx.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace sp {
namespace {

constexpr int kRate = 44100;
constexpr float kTau = 6.2831853f;

struct Noise {
    uint32_t s = 0x1234567u;
    float next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;
    }
};

template <class F>
Sound synth(float seconds, F sample) {
    const int n = static_cast<int>(seconds * kRate);
    auto* data = static_cast<int16_t*>(std::malloc(sizeof(int16_t) * n));
    for (int i = 0; i < n; ++i) {
        float v = sample(static_cast<float>(i) / kRate, static_cast<float>(i) / n);
        v = v > 1 ? 1 : (v < -1 ? -1 : v);
        data[i] = static_cast<int16_t>(v * 32000);
    }
    Wave w{static_cast<unsigned int>(n), kRate, 16, 1, data};
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

}  // namespace

void SoundBank::init() {
    InitAudioDevice();
    ready_ = IsAudioDeviceReady();
    if (!ready_) return;
    Noise nz;
    float lp = 0;

    sounds_[static_cast<int>(Sfx::HitLight)] = synth(0.14f, [&](float t, float u) {
        const float env = std::exp(-u * 7);
        const float thump = std::sin(kTau * (220 - 120 * u) * t);
        return (0.55f * thump + 0.5f * nz.next() * std::exp(-u * 18)) * env * 0.8f;
    });
    sounds_[static_cast<int>(Sfx::HitHeavy)] = synth(0.32f, [&](float t, float u) {
        const float env = std::exp(-u * 5);
        const float thump = std::sin(kTau * (130 - 80 * u) * t);
        lp += (nz.next() - lp) * 0.35f;
        return (0.8f * thump + 0.7f * lp * std::exp(-u * 9) + 0.25f * nz.next() * std::exp(-u * 30)) * env;
    });
    sounds_[static_cast<int>(Sfx::Block)] = synth(0.09f, [&](float t, float u) {
        const float env = std::exp(-u * 10);
        return (0.5f * std::sin(kTau * 1400 * t) * std::exp(-u * 20) + 0.4f * nz.next() + 0.3f * std::sin(kTau * 300 * t)) *
               env * 0.6f;
    });
    lp = 0;
    sounds_[static_cast<int>(Sfx::Whoosh)] = synth(0.16f, [&](float, float u) {
        lp += (nz.next() - lp) * (0.08f + 0.25f * u);
        const float env = std::sin(3.14159f * u);
        return lp * env * 1.6f;
    });
    sounds_[static_cast<int>(Sfx::Jump)] = synth(0.09f, [&](float t, float u) {
        return std::sin(kTau * (300 + 500 * u) * t) * (1 - u) * 0.25f;
    });
    sounds_[static_cast<int>(Sfx::Land)] = synth(0.08f, [&](float t, float u) {
        return (std::sin(kTau * 90 * t) * 0.6f + nz.next() * 0.2f) * (1 - u) * 0.5f;
    });
    sounds_[static_cast<int>(Sfx::Thud)] = synth(0.35f, [&](float t, float u) {
        lp += (nz.next() - lp) * 0.2f;
        return (std::sin(kTau * (70 - 30 * u) * t) * 0.9f + lp * 0.6f) * std::exp(-u * 6);
    });
    sounds_[static_cast<int>(Sfx::KO)] = synth(1.1f, [&](float t, float u) {
        lp += (nz.next() - lp) * 0.1f;
        const float boom = std::sin(kTau * (90 - 55 * u) * t) * std::exp(-u * 3);
        const float ring = std::sin(kTau * 660 * t) * std::exp(-u * 6) * 0.15f;
        return boom + lp * std::exp(-u * 4) + ring;
    });
    sounds_[static_cast<int>(Sfx::Menu)] = synth(0.05f, [&](float t, float u) {
        return (std::fmod(t * 880, 1.0f) < 0.5f ? 0.18f : -0.18f) * (1 - u);
    });
    sounds_[static_cast<int>(Sfx::Select)] = synth(0.18f, [&](float t, float u) {
        const float f = u < 0.4f ? 660 : 990;
        return (std::fmod(t * f, 1.0f) < 0.5f ? 0.18f : -0.18f) * (1 - u);
    });
}

void SoundBank::shutdown() {
    if (!ready_) return;
    for (Sound& s : sounds_) UnloadSound(s);
    CloseAudioDevice();
    ready_ = false;
}

void SoundBank::play(Sfx s, float volume, float pitch) {
    if (!ready_) return;
    Sound& snd = sounds_[static_cast<int>(s)];
    SetSoundVolume(snd, volume);
    SetSoundPitch(snd, pitch);
    PlaySound(snd);
}

}  // namespace sp
