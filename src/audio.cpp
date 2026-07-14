// audio.cpp — lantern audio: 48 kHz stereo float mixer, 16 channels.
// Platform-free: WAVs are parsed and resampled by OUR OWN loader (RIFF
// walk, PCM 8/16/24/32 or float32, mono/stereo, linear resample), and the
// platform backend owns the output device, pulling mixerRender() from its
// callback thread. All mixer state is mutex-guarded. No streaming
// (3DS-era sound banks are small); loops are sample-exact.
#include "audio.hpp"
#include "platform.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace lt {

namespace {

constexpr int FREQ = 48000;
constexpr int CHANNELS = 16;

struct Sound {
    std::vector<float> samples; // interleaved stereo, 48 kHz
};

struct Channel {
    int sound = -1;
    size_t pos = 0;
    float volume = 1.0f;
    bool loop = false;
    bool active = false;
};

struct Mixer {
    bool device = false;
    std::vector<Sound> sounds;
    Channel channels[CHANNELS];
    float master = 1.0f;
};
Mixer M;
std::mutex mixMutex; // guards sounds/channels/master against the callback

uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Parse a WAV into interleaved stereo float at 48 kHz. Handles the formats
// game tools actually emit: PCM 8/16/24/32-bit and IEEE float32, 1-2
// channels, any sample rate (linear resample).
bool parseWav(const char* path, std::vector<float>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "audio load: cannot open %s\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long fsize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (fsize < 12 || fsize > 64 * 1024 * 1024) { // sound banks are small
        std::fclose(f);
        std::fprintf(stderr, "audio load: %s bad size\n", path);
        return false;
    }
    std::vector<uint8_t> d((size_t)fsize);
    size_t got = std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    if (got != d.size() || std::memcmp(d.data(), "RIFF", 4) != 0 ||
        std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
        std::fprintf(stderr, "audio load: %s is not a WAV\n", path);
        return false;
    }

    int fmt = 0, chans = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size()) { // chunk walk; chunks pad to even
        uint32_t sz = le32(&d[pos + 4]);
        size_t body = pos + 8;
        if (sz > d.size() - body) break; // truncated chunk: stop
        if (std::memcmp(&d[pos], "fmt ", 4) == 0 && sz >= 16) {
            fmt = le16(&d[body]);
            chans = le16(&d[body + 2]);
            rate = le32(&d[body + 4]);
            bits = le16(&d[body + 14]);
        } else if (std::memcmp(&d[pos], "data", 4) == 0) {
            data = &d[body];
            dataLen = sz;
        }
        pos = body + sz + (sz & 1);
    }
    const bool pcm =
        fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool f32 = fmt == 3 && bits == 32;
    if ((!pcm && !f32) || chans < 1 || chans > 2 || rate == 0 || !data) {
        std::fprintf(stderr,
                     "audio load: %s unsupported (fmt %d, %d ch, %d bit)\n",
                     path, fmt, chans, bits);
        return false;
    }

    const size_t bytesPer = (size_t)bits / 8;
    const size_t frames = dataLen / (bytesPer * (size_t)chans);
    if (frames == 0) return false;
    auto sampleAt = [&](size_t frame, int ch) -> float {
        const uint8_t* p =
            data + (frame * (size_t)chans + (size_t)ch) * bytesPer;
        switch (bits) {
            case 8: return ((int)p[0] - 128) / 128.0f; // 8-bit WAV: unsigned
            case 16: return (int16_t)le16(p) / 32768.0f;
            case 24: {
                int32_t v = (int32_t)((uint32_t)p[0] << 8 |
                                      (uint32_t)p[1] << 16 |
                                      (uint32_t)p[2] << 24) >>
                            8;
                return (float)v / 8388608.0f;
            }
            default: // 32
                if (f32) {
                    float v;
                    std::memcpy(&v, p, 4);
                    return v;
                }
                return (float)((double)(int32_t)le32(p) / 2147483648.0);
        }
    };

    // decode + resample to 48 kHz stereo in one pass (linear interpolation)
    const size_t outFrames =
        rate == FREQ ? frames : (size_t)((uint64_t)frames * FREQ / rate);
    out.clear();
    out.reserve(outFrames * 2);
    const double step = (double)rate / FREQ;
    for (size_t i = 0; i < outFrames; i++) {
        double src = (double)i * step;
        size_t i0 = (size_t)src;
        if (i0 > frames - 1) i0 = frames - 1;
        size_t i1 = i0 + 1 < frames ? i0 + 1 : i0;
        float t = (float)(src - (double)i0);
        for (int c = 0; c < 2; c++) {
            int sc = chans == 2 ? c : 0; // mono duplicates into both ears
            float a = sampleAt(i0, sc), b = sampleAt(i1, sc);
            out.push_back(a + (b - a) * t);
        }
    }
    return true;
}

} // namespace

void mixerRender(float* out, int frames) {
    std::memset(out, 0, (size_t)frames * 2 * sizeof(float));
    std::lock_guard<std::mutex> lock(mixMutex);
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

bool audioInit() {
    M.device = platAudioStart(); // failure = silent engine, not fatal
    return M.device;
}

void audioShutdown() {
    if (M.device) platAudioStop(); // callback stops before state dies
    std::lock_guard<std::mutex> lock(mixMutex);
    M = Mixer{};
}

int audioLoad(const char* wavPath) {
    Sound snd;
    if (!parseWav(wavPath, snd.samples)) return -1;
    if (snd.samples.size() < 2) { // one stereo frame minimum, or reject
        std::fprintf(stderr, "audio load: %s has no audio data\n", wavPath);
        return -1;
    }
    std::lock_guard<std::mutex> lock(mixMutex);
    M.sounds.push_back(std::move(snd));
    return (int)M.sounds.size() - 1;
}

int audioPlay(int sound, float volume, bool loop) {
    std::lock_guard<std::mutex> lock(mixMutex);
    if (sound < 0 || sound >= (int)M.sounds.size() || !M.device) return -1;
    for (int i = 0; i < CHANNELS; i++) {
        if (!M.channels[i].active) {
            M.channels[i] = {sound, 0, volume, loop, true};
            return i;
        }
    }
    return -1;
}

void audioStop(int channel) {
    if (channel < 0 || channel >= CHANNELS) return;
    std::lock_guard<std::mutex> lock(mixMutex);
    M.channels[channel].active = false;
}

void audioReset() { // free every loaded sound (hot-reload support)
    std::lock_guard<std::mutex> lock(mixMutex);
    for (Channel& ch : M.channels) ch.active = false;
    M.sounds.clear();
}

void audioMaster(float volume) {
    if (volume < 0) volume = 0;
    if (volume > 1) volume = 1;
    // same locking discipline as every other mixer mutation — the callback
    // thread reads master mid-mix
    std::lock_guard<std::mutex> lock(mixMutex);
    M.master = volume;
}

} // namespace lt
