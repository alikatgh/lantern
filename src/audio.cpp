// Native audio: bounded WAV decoding off the callback; a fixed 16-slot mixer.
// No neural inference, network, streaming, allocation or I/O in mixerRender.
#include "audio.hpp"
#include "platform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <mutex>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace lt {
namespace {
constexpr int FREQ = 48000, CHANNELS = 16, ASSETS = 256;
constexpr size_t SOURCE_LIMIT = 64 * 1024 * 1024;
constexpr size_t SOUND_LIMIT = 64 * 1024 * 1024;
constexpr size_t DECODED_LIMIT = 128 * 1024 * 1024;
constexpr int MUSIC = 1, SPEECH = 2;

bool gainOK(float v) { return std::isfinite(v) && v >= 0 && v <= 1; }
bool timeOK(float v) { return std::isfinite(v) && v >= 0 && v <= 60; }
bool busOK(int bus) { return bus >= 0 && bus < 3; }
uint32_t duration(float seconds) {
    return seconds == 0 ? 0 : std::max(1L, std::lround(double(seconds) * FREQ));
}
enum class Curve { Linear, In, Out };
struct Ramp {
    double value = 1, target = 1, step = 0, start = 1;
    uint32_t total = 0;
    Curve curve = Curve::Linear;
    uint32_t remaining = 0;
    void set(float to, uint32_t frames, Curve shape = Curve::Linear) {
        start = value; total = frames; curve = shape;
        target = to;
        remaining = frames;
        if (!frames) { value = target; step = 0; }
        else step = (target - value) / frames;
    }
    float advance() {
        if (remaining && --remaining == 0) value = target;
        else if (remaining) {
            if (curve == Curve::Linear) value += step;
            else {
                constexpr double HALF_PI = 1.57079632679489661923;
                double t = double(total - remaining) / total;
                value = curve == Curve::In ?
                    start + (target - start) * std::sin(t * HALF_PI) :
                    target + (start - target) * std::cos(t * HALF_PI);
            }
        }
        return float(value);
    }
};
struct Sound {
    int id = -1;
    std::vector<float> samples; // stereo 48 kHz
};
struct Channel {
    int sound = -1; // internal asset slot; never reused while active
    int voice = 0; // 0 = legacy channel, positive = new voice
    int bus = -1; // legacy channels bypass the additive buses and ducking
    size_t pos = 0;
    Ramp gain;
    bool loop = false, active = false, paused = false, stopping = false;
};
struct Mixer {
    bool device = false;
    std::array<Sound, ASSETS> sounds;
    std::array<Channel, CHANNELS> channels;
    std::array<Ramp, 3> buses;
    Ramp duck;
    float duckLevel = 1, master = 1;
    uint32_t attack = 0, release = 0;
    size_t decodedBytes = 0;
    uint64_t epoch = 0;
};
Mixer M;
// ID counters deliberately live outside resettable mixer state. Exhaustion
// fails closed; stale handles cannot address a later voice or unloaded sound.
uint64_t nextVoice = 1, nextSound = 0;
std::mutex mixMutex, lifecycleMutex;

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (uint16_t(p[1]) << 8)); }

bool parseWav(const char* path, size_t outputLimit, std::vector<float>& out) {
    if (!path || !*path) return false;
    // Nonblocking open + regular-file check avoids hanging on FIFOs/devices.
    int fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 12 ||
        uint64_t(st.st_size) > SOURCE_LIMIT) {
        ::close(fd); return false;
    }
    FILE* f = ::fdopen(fd, "rb");
    if (!f) { ::close(fd); return false; }
    // Close the file even if allocating the bounded input buffer fails.
    std::vector<uint8_t> d;
    try { d.resize(size_t(st.st_size)); }
    catch (const std::bad_alloc&) { std::fclose(f); return false; }
    size_t got = std::fread(d.data(), 1, d.size(), f);
    std::fclose(f);
    if (got != d.size() || std::memcmp(d.data(), "RIFF", 4) ||
        std::memcmp(d.data() + 8, "WAVE", 4) ||
        uint64_t(le32(d.data() + 4)) + 8 != d.size()) return false;

    int fmt = 0, chans = 0, bits = 0;
    uint32_t rate = 0, byteRate = 0;
    uint16_t align = 0;
    const uint8_t* data = nullptr;
    size_t dataLen = 0, pos = 12;
    bool haveFmt = false;
    while (pos < d.size()) {
        if (d.size() - pos < 8) return false;
        uint32_t sz = le32(&d[pos + 4]);
        size_t body = pos + 8;
        if (sz > d.size() - body) return false;
        if (!std::memcmp(&d[pos], "fmt ", 4)) {
            if (haveFmt || sz < 16) return false;
            haveFmt = true;
            fmt = le16(&d[body]); chans = le16(&d[body + 2]);
            rate = le32(&d[body + 4]); byteRate = le32(&d[body + 8]);
            align = le16(&d[body + 12]); bits = le16(&d[body + 14]);
        } else if (!std::memcmp(&d[pos], "data", 4)) {
            if (data) return false;
            data = &d[body]; dataLen = sz;
        }
        pos = body + sz;
        if (sz & 1) { if (pos == d.size()) return false; ++pos; }
    }
    const bool pcm = fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool f32 = fmt == 3 && bits == 32;
    if ((!pcm && !f32) || chans < 1 || chans > 2 || rate < 8000 ||
        rate > 192000 || !data || align != (bits / 8) * chans ||
        byteRate != uint64_t(rate) * align || !dataLen || dataLen % align) return false;
    const size_t bytesPer = size_t(bits) / 8, frames = dataLen / align;
    const uint64_t outFrames = uint64_t(frames) * FREQ / rate;
    if (!outFrames || outFrames > outputLimit / (2 * sizeof(float))) return false;
    // Reject all nonfinite/out-of-domain float samples, including inputs which
    // downsampling would skip. Such values must never poison the output device.
    if (f32) for (size_t i = 0; i < dataLen; i += 4) {
        uint32_t u = le32(data + i); float v;
        std::memcpy(&v, &u, 4);
        if (!std::isfinite(v) || v < -1 || v > 1) return false;
    }
    auto sampleAt = [&](size_t frame, int ch) -> float {
        const uint8_t* p = data + (frame * size_t(chans) + size_t(ch)) * bytesPer;
        switch (bits) {
            case 8: return (int(p[0]) - 128) / 128.0f;
            case 16: {
                int32_t v = le16(p); if (v >= 32768) v -= 65536;
                return v / 32768.0f;
            }
            case 24: {
                int32_t v = int32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                                    (uint32_t(p[2]) << 16));
                if (v >= 8388608) v -= 16777216;
                return float(v) / 8388608.0f;
            }
            default: {
                uint32_t u = le32(p);
                if (f32) { float v; std::memcpy(&v, &u, 4); return v; }
                int64_t v = u; if (u >= 2147483648U) v -= 4294967296LL;
                return float(double(v) / 2147483648.0);
            }
        }
    };
    out.resize(size_t(outFrames) * 2); // checked before allocating
    const double step = double(rate) / FREQ;
    for (size_t i = 0; i < size_t(outFrames); ++i) {
        double src = double(i) * step;
        size_t i0 = std::min(size_t(src), frames - 1), i1 = std::min(i0 + 1, frames - 1);
        float t = float(src - double(i0));
        for (int c = 0; c < 2; ++c) {
            int sc = chans == 2 ? c : 0;
            float a = sampleAt(i0, sc), b = sampleAt(i1, sc);
            out[i * 2 + size_t(c)] = a + (b - a) * t;
        }
    }
    return true;
}
int soundSlot(int sound) {
    if (sound < 0) return -1;
    for (int i = 0; i < ASSETS; ++i) if (M.sounds[i].id == sound) return i;
    return -1;
}
Channel* voiceSlot(int voice) {
    if (voice <= 0) return nullptr;
    for (Channel& c : M.channels) if (c.active && c.voice == voice) return &c;
    return nullptr;
}
int startVoice(int slot, int bus, float volume, bool loop, uint32_t fade,
               Curve curve = Curve::Linear) {
    if (!M.device || slot < 0 || nextVoice > uint64_t(std::numeric_limits<int>::max())) return -1;
    for (Channel& c : M.channels) if (!c.active) {
        c = Channel{};
        c.sound = slot; c.voice = int(nextVoice++); c.bus = bus;
        c.loop = loop; c.active = true;
        if (fade) c.gain.value = 0;
        c.gain.set(volume, fade, curve);
        return c.voice;
    }
    return -1;
}
void stopVoice(Channel& c, uint32_t fade, Curve curve = Curve::Linear) {
    if (!fade) { c.active = false; return; }
    c.gain.set(0, fade, curve); c.stopping = true;
}
} // namespace

void mixerRender(float* out, int frames) {
    if (!out || frames <= 0) return;
    // int loops below never multiply a caller-controlled frame count by two.
    if (size_t(frames) > std::numeric_limits<size_t>::max() / (2 * sizeof(float))) return;
    std::memset(out, 0, size_t(frames) * 2 * sizeof(float));
    std::lock_guard<std::mutex> lock(mixMutex);
    for (int i = 0; i < frames; ++i) {
        float buses[3]; for (int b = 0; b < 3; ++b) buses[b] = M.buses[b].advance();
        float gains[CHANNELS]{};
        bool speech = false;
        for (int n = 0; n < CHANNELS; ++n) {
            Channel& c = M.channels[n];
            if (!c.active || c.paused) continue;
            const Sound& s = M.sounds[c.sound];
            // Legacy channels historically retire when the next sample is
            // requested, even if the previous callback ended at the boundary.
            if (c.pos == s.samples.size()) {
                if (c.loop) c.pos = 0;
                else { c.active = false; continue; }
            }
            gains[n] = c.gain.advance();
            if (c.stopping && !c.gain.remaining && c.gain.value == 0) {
                c.active = false; continue;
            }
            if (c.bus == SPEECH && gains[n] > 0 && buses[SPEECH] > 0 && M.master > 0 &&
                (s.samples[c.pos] != 0 || s.samples[c.pos + 1] != 0)) speech = true;
        }
        float target = speech ? M.duckLevel : 1;
        if (target != M.duck.target) M.duck.set(target, speech ? M.attack : M.release);
        const float duck = M.duck.advance();
        for (int n = 0; n < CHANNELS; ++n) {
            Channel& c = M.channels[n];
            if (!c.active || c.paused) continue;
            const Sound& s = M.sounds[c.sound];
            float volume = gains[n] * M.master;
            if (c.bus >= 0) volume *= buses[c.bus] * (c.bus == MUSIC ? duck : 1);
            out[size_t(i) * 2] += s.samples[c.pos] * volume;
            out[size_t(i) * 2 + 1] += s.samples[c.pos + 1] * volume;
            c.pos += 2;
            if (c.pos == s.samples.size() && c.voice > 0) {
                if (c.loop) c.pos = 0;
                else c.active = false;
            }
        }
        out[size_t(i) * 2] = std::clamp(out[size_t(i) * 2], -1.0f, 1.0f);
        out[size_t(i) * 2 + 1] = std::clamp(out[size_t(i) * 2 + 1], -1.0f, 1.0f);
    }
}

bool audioInit() {
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex);
    { std::lock_guard<std::mutex> lock(mixMutex); if (M.device) return true; }
    bool device = platAudioStart(); // callback may run here, without deadlock
    std::lock_guard<std::mutex> lock(mixMutex);
    M.device = device;
    return device;
}
void audioShutdown() {
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex);
    bool device;
    { std::lock_guard<std::mutex> lock(mixMutex); device = M.device; M.device = false; }
    if (device) platAudioStop(); // platform joins callback before state reset
    audioReset();
    std::lock_guard<std::mutex> lock(mixMutex); M.master = 1;
}
int audioLoad(const char* path) {
    // Serialize decodes to bound transient memory, but never block the callback
    // on disk I/O. A reset during decoding invalidates publication of that load.
    std::lock_guard<std::mutex> loading(lifecycleMutex);
    uint64_t epoch; size_t limit;
    {
        std::lock_guard<std::mutex> lock(mixMutex);
        if (nextSound > uint64_t(std::numeric_limits<int>::max())) return -1;
        bool free = false; for (const Sound& s : M.sounds) if (s.id < 0) free = true;
        if (!free || M.decodedBytes >= DECODED_LIMIT) return -1;
        epoch = M.epoch; limit = std::min(SOUND_LIMIT, DECODED_LIMIT - M.decodedBytes);
    }
    Sound sound;
    try { if (!parseWav(path, limit, sound.samples)) return -1; }
    catch (const std::bad_alloc&) { return -1; }
    std::lock_guard<std::mutex> lock(mixMutex);
    if (epoch != M.epoch) return -1;
    for (Sound& s : M.sounds) if (s.id < 0) {
        sound.id = int(nextSound++);
        M.decodedBytes += sound.samples.size() * sizeof(float);
        s = std::move(sound); return s.id;
    }
    return -1;
}
bool audioUnload(int sound) {
    Sound retired;
    {
        std::lock_guard<std::mutex> lock(mixMutex);
        int slot = soundSlot(sound); if (slot < 0) return false;
        for (const Channel& c : M.channels) if (c.active && c.sound == slot) return false;
        M.decodedBytes -= M.sounds[slot].samples.size() * sizeof(float);
        retired = std::move(M.sounds[slot]); M.sounds[slot] = Sound{};
    } // allocation is freed on the caller, outside the callback and mixer lock
    return true;
}
int audioPlay(int sound, float volume, bool loop) {
    // Preserve legacy amplification/inversion. Bound extreme finite values so
    // summing all 16 channels cannot overflow the float callback buffer.
    if (!std::isfinite(volume) || std::abs(volume) > std::numeric_limits<float>::max() / CHANNELS) return -1;
    std::lock_guard<std::mutex> lock(mixMutex);
    int slot = soundSlot(sound); if (slot < 0 || !M.device) return -1;
    for (int i = 0; i < CHANNELS; ++i) if (!M.channels[i].active) {
        Channel& c = M.channels[i]; c = Channel{};
        c.sound = slot; c.gain.set(volume, 0); c.loop = loop; c.active = true;
        return i;
    }
    return -1;
}
void audioChannelVolume(int channel, float volume) {
    if (channel < 0 || channel >= CHANNELS || !std::isfinite(volume)) return;
    std::lock_guard<std::mutex> lock(mixMutex);
    Channel& c = M.channels[channel];
    if (c.voice == 0) c.gain.set(std::clamp(volume, 0.0f, 1.0f), 0);
}
void audioStop(int channel) {
    if (channel < 0 || channel >= CHANNELS) return;
    std::lock_guard<std::mutex> lock(mixMutex);
    if (M.channels[channel].voice == 0) M.channels[channel].active = false;
}
void audioMaster(float volume) {
    if (!std::isfinite(volume)) return;
    std::lock_guard<std::mutex> lock(mixMutex); M.master = std::clamp(volume, 0.0f, 1.0f);
}
int audioVoicePlay(int sound, int bus, float volume, bool loop, float fade) {
    if (!busOK(bus) || !gainOK(volume) || !timeOK(fade)) return -1;
    std::lock_guard<std::mutex> lock(mixMutex);
    return startVoice(soundSlot(sound), bus, volume, loop, duration(fade));
}
bool audioVoiceStop(int voice, float fade) {
    if (!timeOK(fade)) return false;
    std::lock_guard<std::mutex> lock(mixMutex);
    Channel* c = voiceSlot(voice); if (!c) return false;
    stopVoice(*c, duration(fade)); return true;
}
bool audioVoicePause(int voice, bool paused) {
    std::lock_guard<std::mutex> lock(mixMutex);
    Channel* c = voiceSlot(voice); if (!c) return false;
    c->paused = paused; return true;
}
bool audioVoiceActive(int voice) {
    std::lock_guard<std::mutex> lock(mixMutex); return voiceSlot(voice) != nullptr;
}
int audioMusic(int sound, float volume, bool loop, float crossfade) {
    if (!gainOK(volume) || !timeOK(crossfade)) return -1;
    std::lock_guard<std::mutex> lock(mixMutex);
    uint32_t frames = duration(crossfade);
    // Start first: saturation/invalid assets must leave existing music intact.
    int voice = startVoice(soundSlot(sound), MUSIC, volume, loop, frames, Curve::In);
    if (voice < 0) return -1;
    for (Channel& c : M.channels) if (c.active && c.bus == MUSIC && c.voice != voice)
        stopVoice(c, c.paused ? 0 : frames, Curve::Out);
    return voice;
}
bool audioBusVolume(int bus, float volume, float ramp) {
    if (!busOK(bus) || !gainOK(volume) || !timeOK(ramp)) return false;
    std::lock_guard<std::mutex> lock(mixMutex); M.buses[bus].set(volume, duration(ramp));
    return true;
}
bool audioDucking(float level, float attack, float release) {
    if (!gainOK(level) || !timeOK(attack) || !timeOK(release)) return false;
    std::lock_guard<std::mutex> lock(mixMutex);
    M.duckLevel = level; M.attack = duration(attack); M.release = duration(release);
    return true;
}
void audioReset() {
    std::array<Sound, ASSETS> retired;
    {
        std::lock_guard<std::mutex> lock(mixMutex);
        retired = std::move(M.sounds); M.sounds = {};
        M.channels = {}; M.buses = {}; M.duck = {};
        M.duckLevel = 1; M.attack = M.release = 0; M.decodedBytes = 0;
        ++M.epoch;
    } // all asset deallocation happens on this control thread
}
} // namespace lt
