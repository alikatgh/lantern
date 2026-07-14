// audio.cpp — lantern audio: 48 kHz stereo float mixer, 16 channels.
// WAVs load through SDL and are converted once at load time; the callback
// just mixes. No streaming (3DS-era sound banks are small); loops are
// sample-exact.
#include "audio.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace lt {

namespace {

constexpr int FREQ = 48000;
constexpr int CHANNELS = 16;

struct Sound {
    std::vector<float> samples; // interleaved stereo
};

struct Channel {
    int sound = -1;
    size_t pos = 0;
    float volume = 1.0f;
    bool loop = false;
    bool active = false;
};

struct Mixer {
    SDL_AudioDeviceID dev = 0;
    std::vector<Sound> sounds;
    Channel channels[CHANNELS];
    float master = 1.0f;
};
Mixer M;

void SDLCALL mixCallback(void*, Uint8* stream, int len) {
    float* out = (float*)stream;
    int frames = len / (int)(sizeof(float) * 2);
    std::memset(stream, 0, (size_t)len);
    for (Channel& ch : M.channels) {
        if (!ch.active) continue;
        const Sound& s = M.sounds[(size_t)ch.sound];
        if (s.samples.size() < 2) { // degenerate sound: never index into it
            ch.active = false;
            continue;
        }
        const float vol = ch.volume * M.master;
        for (int i = 0; i < frames; i++) {
            if (ch.pos + 1 >= s.samples.size()) {
                if (ch.loop) {
                    ch.pos = 0; // size >= 2 guaranteed above, safe to read
                } else {
                    ch.active = false;
                    break;
                }
            }
            out[i * 2] += s.samples[ch.pos] * vol;
            out[i * 2 + 1] += s.samples[ch.pos + 1] * vol;
            ch.pos += 2;
        }
    }
    // soft clip
    for (int i = 0; i < frames * 2; i++) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        if (out[i] < -1.0f) out[i] = -1.0f;
    }
}

} // namespace

bool audioInit() {
    SDL_AudioSpec want{}, have{};
    want.freq = FREQ;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = mixCallback;
    M.dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!M.dev) {
        std::fprintf(stderr, "audio: %s (continuing silent)\n",
                     SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(M.dev, 0);
    return true;
}

void audioShutdown() {
    if (M.dev) SDL_CloseAudioDevice(M.dev);
    M = Mixer{};
}

int audioLoad(const char* wavPath) {
    SDL_AudioSpec spec{};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (!SDL_LoadWAV(wavPath, &spec, &buf, &len)) {
        std::fprintf(stderr, "audio load: %s\n", SDL_GetError());
        return -1;
    }
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
                          AUDIO_F32SYS, 2, FREQ) < 0) {
        SDL_FreeWAV(buf);
        return -1;
    }
    Sound snd;
    if (cvt.needed) {
        cvt.len = (int)len;
        std::vector<Uint8> work((size_t)cvt.len * (size_t)cvt.len_mult);
        std::memcpy(work.data(), buf, len);
        cvt.buf = work.data();
        SDL_ConvertAudio(&cvt);
        snd.samples.assign((float*)work.data(),
                           (float*)(work.data() + cvt.len_cvt));
    } else {
        snd.samples.assign((float*)buf, (float*)(buf + len));
    }
    SDL_FreeWAV(buf);
    if (snd.samples.size() < 2) { // one stereo frame minimum, or reject
        std::fprintf(stderr, "audio load: %s has no audio data\n", wavPath);
        return -1;
    }
    if (M.dev) SDL_LockAudioDevice(M.dev);
    M.sounds.push_back(std::move(snd));
    int id = (int)M.sounds.size() - 1;
    if (M.dev) SDL_UnlockAudioDevice(M.dev);
    return id;
}

int audioPlay(int sound, float volume, bool loop) {
    if (sound < 0 || sound >= (int)M.sounds.size() || !M.dev) return -1;
    SDL_LockAudioDevice(M.dev);
    int chosen = -1;
    for (int i = 0; i < CHANNELS; i++) {
        if (!M.channels[i].active) {
            M.channels[i] = {sound, 0, volume, loop, true};
            chosen = i;
            break;
        }
    }
    SDL_UnlockAudioDevice(M.dev);
    return chosen;
}

void audioStop(int channel) {
    if (channel < 0 || channel >= CHANNELS || !M.dev) return;
    SDL_LockAudioDevice(M.dev);
    M.channels[channel].active = false;
    SDL_UnlockAudioDevice(M.dev);
}

void audioReset() { // free every loaded sound (hot-reload support)
    if (M.dev) SDL_LockAudioDevice(M.dev);
    for (Channel& ch : M.channels) ch.active = false;
    M.sounds.clear();
    if (M.dev) SDL_UnlockAudioDevice(M.dev);
}

void audioMaster(float volume) {
    if (volume < 0) volume = 0;
    if (volume > 1) volume = 1;
    // same locking discipline as every other mixer mutation — the callback
    // thread reads master mid-mix
    if (M.dev) SDL_LockAudioDevice(M.dev);
    M.master = volume;
    if (M.dev) SDL_UnlockAudioDevice(M.dev);
}

} // namespace lt
