// Real mixer, C ABI and Wick registry; only OS presentation/input/device are
// replaced. Private source inclusion permits terminal-ID fault injection,
// without adding test hooks to the shipping ABI or waiting for 2^31 voices.
#include "../src/audio.cpp"
#include "../src/wick_host.cpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
thread_local bool callbackAudit = false;
std::atomic<size_t> callbackAllocations{0}, callbackFrees{0};
}
void* operator new(std::size_t n) {
    if (callbackAudit) ++callbackAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept {
    if (callbackAudit && p) ++callbackFrees;
    std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {
int assertions = 0, starts = 0, stops = 0;
bool deviceAvailable = true;
int renderOutputW = 1200, renderOutputH = 720;
void expect(bool yes, const std::string& message) {
    ++assertions;
    if (!yes) throw std::runtime_error(message);
}
void near(float got, float wanted, const std::string& message) {
    expect(std::isfinite(got) && std::abs(got - wanted) < 0.000002f,
           message + ": got " + std::to_string(got) + " expected " + std::to_string(wanted));
}
void put(std::vector<uint8_t>& bytes, size_t p, uint32_t n, int count) {
    for (int i = 0; i < count; ++i) bytes[p + size_t(i)] = uint8_t(n >> (i * 8));
}
std::vector<uint8_t> wav(const std::vector<float>& samples, uint32_t rate = 48000,
                         int channels = 2, int bits = 32, int format = 3) {
    size_t bytesPer = size_t(bits / 8);
    size_t length = samples.size() * bytesPer;
    std::vector<uint8_t> bytes(44 + length + (length & 1), 0);
    std::memcpy(bytes.data(), "RIFF", 4); put(bytes, 4, uint32_t(bytes.size() - 8), 4);
    std::memcpy(bytes.data() + 8, "WAVEfmt ", 8); put(bytes, 16, 16, 4);
    put(bytes, 20, format, 2); put(bytes, 22, channels, 2); put(bytes, 24, rate, 4);
    put(bytes, 28, rate * channels * bits / 8, 4); put(bytes, 32, channels * bits / 8, 2);
    put(bytes, 34, bits, 2); std::memcpy(bytes.data() + 36, "data", 4);
    put(bytes, 40, uint32_t(length), 4);
    for (size_t i = 0; i < samples.size(); ++i) {
        uint32_t value = 0;
        if (format == 3) std::memcpy(&value, &samples[i], 4);
        else if (bits == 8) value = uint32_t(std::lround(samples[i] * 128 + 128));
        else if (bits == 16) value = uint32_t(int32_t(std::lround(samples[i] * 32768)));
        else if (bits == 24) value = uint32_t(int32_t(std::lround(samples[i] * 8388608)));
        else value = uint32_t(int64_t(std::llround(double(samples[i]) * 2147483648.0)));
        put(bytes, 44 + i * bytesPer, value, int(bytesPer));
    }
    return bytes;
}
void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    expect(bool(f), "test WAV written");
}
std::vector<float> render(int frames) {
    std::vector<float> result(size_t(frames) * 2, -99);
    callbackAudit = true;
    lt::mixerRender(result.data(), frames);
    callbackAudit = false;
    expect(callbackAllocations == 0 && callbackFrees == 0, "callback neither allocates nor frees assets");
    return result;
}
void reset() { lt::audioReset(); lt_master_volume(1); }

void mixerChecks(const std::filesystem::path& dir) {
    write(dir / "constant.wav", wav({0.25f, 0.25f, 0.25f, 0.25f}));
    write(dir / "sequence.wav", wav({0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f}));
    write(dir / "left.wav", wav({0.25f, 0}));
    write(dir / "right.wav", wav({0, 0.25f}));
    write(dir / "silence.wav", wav({0, 0}));
    expect(lt::audioInit(), "stub device starts");
    expect(lt::audioInit() && starts == 1, "repeated init is idempotent");
    int sound = lt_sound_load((dir / "constant.wav").c_str());
    expect(sound >= 0, "float WAV loads through C ABI");
    int legacy = lt_sound_play(sound, 0.5f, 1);
    expect(legacy == 0, "legacy channel numbering retained");
    expect(lt_bus_volume(LT_BUS_SFX, 0, 0) && lt_bus_volume(LT_BUS_MUSIC, 0, 0), "mute new buses");
    expect(lt_ducking(0, 0, 0), "configure duck");
    lt_master_volume(0.5f);
    near(render(1)[0], 0.0625f, "legacy bypasses bus/duck and retains volume/master math");
    lt_channel_volume(legacy, 1);
    near(render(1)[0], 0.125f, "legacy channel volume updates");
    lt_sound_stop(legacy); near(render(1)[0], 0, "legacy stop");
    legacy = lt_sound_play(sound, 1, 0);
    for (int i = 1; i < 16; ++i) expect(lt_sound_play(sound, 0, 1) == i, "legacy pool slots");
    render(2);
    expect(lt_sound_play(sound, 0, 1) == -1, "legacy exact-end channel remains reserved until next sample");
    render(1); expect(lt_sound_play(sound, 0, 1) == legacy, "legacy next sample retires the completed channel");
    reset(); sound = lt_sound_load((dir / "constant.wav").c_str());
    int sfx = lt_voice_play(sound, LT_BUS_SFX, 1, 1, 0);
    int music = lt_voice_play(sound, LT_BUS_MUSIC, 1, 1, 0);
    int speech = lt_voice_play(sound, LT_BUS_SPEECH, 1, 1, 0);
    expect(sfx > 0 && music > sfx && speech > music, "monotonic stable handles");
    expect(lt_bus_volume(LT_BUS_MUSIC, 0, 0) && lt_bus_volume(LT_BUS_SPEECH, 0, 0), "bus isolation");
    near(render(1)[0], 0.25f, "muted music and speech leave sfx");
    expect(lt_bus_volume(LT_BUS_SFX, 0, 4.0f / 48000), "bounded bus ramp accepted");
    auto b = render(4);
    for (int i = 0; i < 4; ++i) near(b[size_t(i) * 2], 0.25f * (3 - i) / 4, "bus ramps on exact samples");
    reset(); sound = lt_sound_load((dir / "constant.wav").c_str());
    int fade = lt_voice_play(sound, LT_BUS_SFX, 1, 1, 4.0f / 48000);
    auto f = render(4);
    for (int i = 0; i < 4; ++i) near(f[size_t(i) * 2], 0.25f * (i + 1) / 4, "linear fade in");
    expect(lt_voice_stop(fade, 4.0f / 48000), "stop with fade");
    f = render(4);
    for (int i = 0; i < 4; ++i) near(f[size_t(i) * 2], 0.25f * (3 - i) / 4, "linear fade out");
    expect(!lt_voice_active(fade), "faded voice completes on final zero frame");
    int seq = lt_sound_load((dir / "sequence.wav").c_str());
    int v = lt_voice_play(seq, LT_BUS_SFX, 1, 0, 0);
    near(render(1)[0], 0.125f, "sequence first position");
    expect(lt_voice_pause(v, 1) && lt_voice_active(v), "paused voice remains active");
    expect(!lt_sound_unload(seq), "paused data cannot be freed");
    for (float n : render(7)) near(n, 0, "paused voice emits silence");
    expect(lt_voice_pause(v, 0), "resume voice");
    auto resumed = render(2);
    near(resumed[0], 0.375f, "resume retains exact frame"); near(resumed[2], 0.625f, "resume continues");
    expect(!lt_voice_active(v), "natural completion retires immediately");
    int looping = lt_voice_play(seq, LT_BUS_SFX, 1, 1, 0);
    auto looped = render(5);
    for (int i = 0; i < 5; ++i) near(looped[size_t(i) * 2], 0.125f + 0.25f * (i % 3), "loop wraps on exact frame");
    lt_voice_stop(looping, 0);
    expect(lt_sound_unload(seq), "completed data unloads");
    int replacement = lt_voice_play(sound, LT_BUS_SFX, 1, 1, 0);
    expect(replacement > v && !lt_voice_stop(v, 0) && !lt_voice_pause(v, 1), "stale voice cannot affect reused slot");
    expect(lt_voice_active(replacement) && !lt_sound_unload(sound), "active asset remains owned");
    lt_sound_stop(0); lt_channel_volume(0, 0);
    near(render(1)[0], 0.25f, "legacy channel controls cannot hijack new voice");
    expect(lt_voice_stop(replacement, 0) && lt_sound_unload(sound), "explicit stop releases unload eligibility");
    expect(!lt_sound_unload(sound) && lt_voice_play(sound, 0, 1, 0, 0) < 0, "unloaded sound is invalid");
    int newer = lt_sound_load((dir / "constant.wav").c_str());
    expect(newer > sound, "unloaded sound IDs never alias replacements");
    int beforeReset = lt_voice_play(newer, 0, 1, 1, 0);
    reset(); newer = lt_sound_load((dir / "constant.wav").c_str());
    int afterReset = lt_voice_play(newer, 0, 1, 1, 0);
    expect(afterReset > beforeReset && !lt_voice_stop(beforeReset, 0), "reset does not reuse stable handles");
    reset(); int left = lt_sound_load((dir / "left.wav").c_str());
    int right = lt_sound_load((dir / "right.wav").c_str());
    int old = lt_music(left, 1, 1, 0);
    near(render(1)[0], 0.25f, "old music starts");
    int incoming = lt_music(right, 1, 1, 4.0f / 48000);
    expect(incoming > old, "crossfade starts atomically");
    auto cross = render(4);
    for (int i = 0; i < 4; ++i) {
        const float l = cross[size_t(i) * 2], r = cross[size_t(i) * 2 + 1];
        near(l * l + r * r, 0.0625f, "equal-power crossfade preserves independent stereo energy");
    }
    near(cross[2], 0.25f / std::sqrt(2.0f), "outgoing midpoint power");
    near(cross[3], 0.25f / std::sqrt(2.0f), "incoming midpoint power");
    expect(!lt_voice_active(old) && lt_voice_active(incoming), "old music retired at crossfade end");
    expect(lt_voice_pause(incoming, 1), "pause outgoing music");
    int third = lt_music(left, 1, 1, 0);
    expect(third > incoming && !lt_voice_active(incoming), "replacement retires paused outgoing music immediately");
    reset(); left = lt_sound_load((dir / "left.wav").c_str());
    right = lt_sound_load((dir / "right.wav").c_str());
    lt_music(left, 1, 1, 0);
    int talk = lt_voice_play(right, LT_BUS_SPEECH, 1, 1, 0);
    expect(lt_ducking(0.2f, 4.0f / 48000, 4.0f / 48000), "duck configuration");
    auto duck = render(4);
    for (int i = 0; i < 4; ++i) {
        near(duck[size_t(i) * 2], 0.25f * (1 - 0.2f * (i + 1)), "duck attack on sample clock");
        near(duck[size_t(i) * 2 + 1], 0.25f, "speech does not duck itself");
    }
    lt_voice_pause(talk, 1); duck = render(4);
    for (int i = 0; i < 4; ++i) near(duck[size_t(i) * 2], 0.25f * (0.2f + 0.2f * (i + 1)), "paused speech releases duck");
    lt_voice_pause(talk, 0); render(4);
    lt_bus_volume(LT_BUS_SPEECH, 0, 0); near(render(4)[6], 0.25f, "muted speech releases duck");
    lt_bus_volume(LT_BUS_SPEECH, 1, 0); render(4);
    lt_voice_stop(talk, 0); near(render(4)[6], 0.25f, "stopped speech releases duck");
    int silence = lt_sound_load((dir / "silence.wav").c_str());
    lt_voice_play(silence, LT_BUS_SPEECH, 1, 1, 0);
    near(render(4)[6], 0.25f, "silent samples do not duck music");
    // Separate audible lines share one attenuation rather than multiplying it;
    // zero-volume lines and naturally completed speech do not sustain ducking.
    reset(); left = lt_sound_load((dir / "left.wav").c_str());
    right = lt_sound_load((dir / "right.wav").c_str());
    lt_music(left, 1, 1, 0); lt_ducking(0.5f, 0, 0);
    int zeroVoice = lt_voice_play(right, LT_BUS_SPEECH, 0, 1, 0);
    near(render(1)[0], 0.25f, "zero-gain speech does not duck");
    int one = lt_voice_play(right, LT_BUS_SPEECH, 0.25f, 1, 0);
    int two = lt_voice_play(right, LT_BUS_SPEECH, 0.25f, 1, 0);
    near(render(1)[0], 0.125f, "multiple speech voices share one attenuation");
    lt_voice_stop(one, 0); lt_voice_stop(two, 0); lt_voice_stop(zeroVoice, 0);
    int shortLine = lt_voice_play(right, LT_BUS_SPEECH, 1, 0, 0);
    near(render(1)[0], 0.125f, "one-frame speech ducks its audible frame");
    expect(!lt_voice_active(shortLine), "one-frame speech completes");
    near(render(1)[0], 0.25f, "natural speech completion releases ducking");
    reset(); sound = lt_sound_load((dir / "constant.wav").c_str());
    v = lt_voice_play(sound, 0, 1, 1, 4.0f / 48000);
    near(render(1)[0], 0.0625f, "fade first sample");
    lt_voice_pause(v, 1); render(100); lt_voice_pause(v, 0);
    near(render(1)[0], 0.125f, "paused voice freezes its fade clock");
    lt_voice_pause(v, 1); lt_voice_stop(v, 4.0f / 48000); render(4);
    expect(lt_voice_active(v), "fade-stop of paused voice waits for resume");
    lt_voice_pause(v, 0); render(4); expect(!lt_voice_active(v), "resumed fade-stop completes");
    reset(); sound = lt_sound_load((dir / "constant.wav").c_str());
    int keep = lt_music(sound, 0.25f, 1, 0);
    for (int i = 1; i < 16; ++i) expect(lt_voice_play(sound, 0, 0, 1, 0) > 0, "pool slot accepted");
    expect(lt_voice_play(sound, 0, 1, 1, 0) == -1 && lt_music(sound, 1, 1, 1) == -1,
           "saturated play/crossfade fails");
    expect(lt_voice_active(keep), "failed crossfade retains existing music");
    near(render(1)[0], 0.0625f, "failed crossfade leaves existing level unchanged");
    reset(); sound = lt_sound_load((dir / "constant.wav").c_str());
    float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (float bad : {nan, inf, -1.0f, 1.1f}) {
        expect(lt_voice_play(sound, 0, bad, 0, 0) == -1, "invalid gain fails");
        expect(!lt_bus_volume(0, bad, 0) && !lt_ducking(bad, 0, 0), "invalid bus/duck gain fails");
    }
    for (float bad : {nan, inf, -1.0f, 60.1f}) {
        expect(lt_voice_play(sound, 0, 1, 0, bad) == -1 && lt_music(sound, 1, 1, bad) == -1, "invalid fade fails");
        expect(!lt_bus_volume(0, 1, bad) && !lt_ducking(1, bad, 0), "invalid ramp fails");
    }
    expect(lt_voice_play(sound, 3, 1, 0, 0) == -1 && !lt_bus_volume(-1, 1, 0), "invalid bus fails");
    expect(lt_voice_play(sound, 0, 1, 2, 0) == -1 && lt_music(sound, 1, -1, 0) == -1, "C flags fail before conversion");
    v = lt_voice_play(sound, 0, 1, 1, 0);
    expect(!lt_voice_pause(v, 2), "invalid pause flag leaves voice playing");
    lt_master_volume(nan); lt_channel_volume(0, nan);
    near(render(1)[0], 0.25f, "invalid legacy controls cannot poison output");
    lt::audioShutdown(); expect(stops == 1 && !lt_voice_active(v), "shutdown stops device and voices");
    deviceAvailable = false; expect(!lt::audioInit(), "device failure is reported");
    sound = lt_sound_load((dir / "constant.wav").c_str());
    expect(sound >= 0 && lt_voice_play(sound, 0, 1, 0, 0) == -1 && lt_sound_play(sound, 1, 0) == -1,
           "silent device allows assets but never fake playback handles");
    near(render(1)[0], 0, "silent device output");
    lt::audioShutdown(); deviceAvailable = true; expect(lt::audioInit(), "restart device");
}

void decoderChecks(const std::filesystem::path& dir) {
    reset();
    for (int bits : {8, 16, 24, 32}) {
        write(dir / "pcm.wav", wav({0.25f, -0.25f}, 48000, 1, bits, 1));
        int sound = lt_sound_load((dir / "pcm.wav").c_str());
        expect(sound >= 0, "supported PCM loads");
        lt_voice_play(sound, 0, 1, 0, 0); auto data = render(2);
        near(data[0], 0.25f, "PCM positive"); near(data[1], 0.25f, "mono duplicates");
        near(data[2], -0.25f, "PCM negative"); expect(lt_sound_unload(sound), "PCM unload");
    }
    write(dir / "resample.wav", wav({0, 0.5f}, 24000, 1));
    int id = lt_sound_load((dir / "resample.wav").c_str());
    lt_voice_play(id, 0, 1, 0, 0); auto data = render(4);
    for (int i = 0; i < 4; ++i) near(data[size_t(i) * 2], i == 0 ? 0 : i == 1 ? 0.25f : 0.5f, "linear resampling");
    auto bad = [&](std::vector<uint8_t> bytes, const std::string& name) {
        write(dir / "bad.wav", bytes);
        expect(lt_sound_load((dir / "bad.wav").c_str()) == -1, name);
    };
    auto valid = wav({0.25f, 0.25f});
    auto bytes = valid; bytes[0] = 'X'; bad(bytes, "non-WAV rejected");
    bytes = valid; bytes.pop_back(); bad(bytes, "truncated RIFF rejected");
    bytes = valid; put(bytes, 40, 0xFFFFFFFF, 4); bad(bytes, "oversized chunk rejected");
    for (uint32_t rate : {0U, 1U, 7999U, 192001U, 0xFFFFFFFFU}) bad(wav({0.25f}, rate, 1), "extreme rate rejected before resample allocation");
    for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1.01f})
        bad(wav({0, value}, 192000, 1), "nonfinite/out-of-domain skipped samples rejected");
    bytes = valid; put(bytes, 32, 1, 2); bad(bytes, "incorrect block alignment rejected");
    bytes = valid; put(bytes, 28, 0, 4); bad(bytes, "incorrect byte rate rejected");
    bytes = valid; put(bytes, 22, 3, 2); bad(bytes, "unsupported channels rejected");
    bad(wav({}, 48000, 1), "empty samples rejected");
    bytes = wav({0}, 48000, 1, 8, 1); bytes.pop_back(); put(bytes, 4, uint32_t(bytes.size() - 8), 4);
    bad(bytes, "missing odd-chunk padding rejected");
    expect(lt_sound_load(nullptr) == -1 && lt_sound_load("") == -1, "null/empty paths rejected");
    auto fifo = dir / "fifo.wav";
    expect(::mkfifo(fifo.c_str(), 0600) == 0 && lt_sound_load(fifo.c_str()) == -1, "FIFO rejected without blocking");
    auto huge = dir / "oversized.wav";
    { std::ofstream f(huge, std::ios::binary); f.seekp(64 * 1024 * 1024); f.put(0); }
    expect(lt_sound_load(huge.c_str()) == -1, "oversized sparse source rejected before read/allocation");
    // A ~1.4 MiB PCM source would expand past the 64 MiB decoded asset cap.
    auto expanded = wav(std::vector<float>(1398102, 0), 8000, 1, 8, 1);
    bad(expanded, "decoded output limit checked before allocating expanded samples");
    // Two 48 MiB decoded assets fit; a third fails the aggregate limit. Releasing
    // one makes that same load possible, demonstrating real byte accounting.
    write(dir / "bank.wav", wav(std::vector<float>(1048576, 0), 8000, 1, 8, 1));
    reset(); int bankA = lt_sound_load((dir / "bank.wav").c_str());
    int bankB = lt_sound_load((dir / "bank.wav").c_str());
    expect(bankA >= 0 && bankB >= 0 && lt_sound_load((dir / "bank.wav").c_str()) == -1, "aggregate decoded memory bounded");
    expect(lt_sound_unload(bankA), "release decoded memory");
    int bankC = lt_sound_load((dir / "bank.wav").c_str());
    expect(bankC > bankB, "unload restores aggregate headroom"); reset();
    for (int i = 0; i < 256; ++i) expect(lt_sound_load((dir / "constant.wav").c_str()) >= 0, "bounded asset slot accepted");
    expect(lt_sound_load((dir / "constant.wav").c_str()) == -1, "asset pool bounded"); reset();
}

void wickChecks(const std::filesystem::path& dir) {
    g_dir = dir.string();
    auto run = [&](const std::string& source, const std::string& error = "") {
        auto vm = std::unique_ptr<VM, decltype(&wick::destroy)>(wick::create(), wick::destroy);
        std::string err; expect(registerLT(vm.get(), err), "register real audio signatures");
        bool ok = wick::load(vm.get(), source, "audio.wick", err);
        expect(error.empty() ? ok : !ok && err.find(error) != std::string::npos,
               "Wick host validation: " + source + " -> " + err);
    };
    reset();
    run(R"(
check(lt.SFX == 0 and lt.MUSIC == 1 and lt.SPEECH == 2, "named buses")
check(lt.W == 400 and lt.H == 240 and lt.render_width() == 0 and lt.render_height() == 0 and not lt.render_native(), "physical rendering info before boot")
check(lt.try_load_sound("missing.wav") == -1, "optional missing media")
check(lt.try_load_sound("bad.wav") == -1, "optional invalid media")
let sound = lt.load_sound("constant.wav")
let voice = lt.voice_play(sound, lt.SPEECH)
check(voice > 0 and lt.voice_active(voice), "voice starts")
check(lt.voice_pause(voice) and lt.voice_pause(voice, 0), "pause resume")
check(lt.ducking(0.2) and lt.bus_volume(lt.MUSIC, 0.5), "mix controls")
check(lt.voice_stop(voice) and not lt.voice_active(voice), "voice stops")
check(lt.sound_unload(sound), "asset releases")
let musicSound = lt.try_load_sound("left.wav")
let music = lt.music(musicSound)
check(music > voice, "music starts")
check(lt.voice_stop(music) and lt.sound_unload(musicSound), "music releases")
)");
    run("lt.load_sound(\"missing.wav\")", "load_sound failed");
    run("lt.try_load_sound(\"../escape.wav\")", "path refused");
    run("lt.voice_play(1, \"speech\")", "expected num");
    for (const std::string& source : {
        "lt.voice_play(1.5)", "lt.voice_active(2147483648)", "lt.voice_stop(1, 61)",
        "lt.voice_pause(1, 0.5)", "lt.voice_play(1, 2.5)", "lt.voice_play(1, 0, 1.1)",
        "lt.voice_play(1, 0, 1, -1)", "lt.music(1, 1, 2)", "lt.bus_volume(0, 1, -1)",
        "lt.ducking(0, 0, 61)", "lt.sound_unload(2147483648)", "lt.play(1.5)",
        "lt.stop(2147483648)", "lt.channel_volume(0.25, 1)", "lt.volume(1 / 0)",
        "lt.voice_play(sqrt(-1))", "lt.voice_active(1 / 0)", "lt.voice_play(1, 0, 1 / 0)"})
        run(source, "invalid audio argument");
    reset();
    run(R"(
lt.volume(2)
lt.volume(9999999999999999999999999999999999999999)
let sound = lt.load_sound("constant.wav")
let channel = lt.play(sound, 2)
check(channel >= 0, "legacy amplification accepted")
lt.channel_volume(channel, -0.1)
lt.channel_volume(channel, 9999999999999999999999999999999999999999)
lt.stop(channel)
channel = lt.play(sound, -2)
check(channel >= 0, "legacy inversion accepted")
lt.stop(channel)
check(lt.sound_unload(sound), "legacy fixture releases")
)");
    reset();
    int amplified = lt_sound_load((dir / "constant.wav").c_str());
    int legacy = lt_sound_play(amplified, 2, 1);
    expect(legacy >= 0, "C legacy gain above one accepted");
    near(render(1)[0], 0.5f, "legacy gain amplifies before final clipping");
    lt_channel_volume(legacy, -0.1f); near(render(1)[0], 0, "legacy channel lower clamp");
    lt_channel_volume(legacy, 2); lt_master_volume(2);
    near(render(1)[0], 0.25f, "legacy volume upper clamps");
    lt_sound_stop(legacy);
    legacy = lt_sound_play(amplified, -2, 1);
    near(render(1)[0], -0.5f, "legacy negative gain preserves inversion");
    lt_sound_stop(legacy); expect(lt_sound_unload(amplified), "legacy asset unloads");
    // Direct embedding with a wrong tagged value fails before touching the
    // numeric union, even if a caller bypasses the typed Wick compiler.
    auto vm = std::unique_ptr<VM, decltype(&wick::destroy)>(wick::create(), wick::destroy);
    Value bad[] = {Value::boolean(true)};
    expect(!wVoiceActive(*vm, bad, 1).b, "direct wrong-tag native input rejected");
    wick::Diagnostic diagnostic;
    expect(checkWickSource("let v = lt.voice_play(1, lt.SPEECH)\nlt.voice_pause(v)\nlt.music(1)\nlt.try_load_sound(\"missing.wav\")", "compile.wick", diagnostic),
           "public compile-only host accepts new signatures/constants");
    expect(!checkWickSource("lt.music(\"bad\")", "compile.wick", diagnostic), "compile-only rejects audio type errors");
}
void concurrencyChecks(const std::filesystem::path& dir) {
    reset();
    std::atomic<bool> finite{true}, controlsDone{false};
    std::atomic<int> rendered{0};
    std::thread callback([&] {
        float data[32];
        for (int i = 0; i < 4000 || !controlsDone; ++i) {
            callbackAudit = true; lt::mixerRender(data, 16); callbackAudit = false;
            for (float v : data) if (!std::isfinite(v) || std::abs(v) > 1) finite = false;
            ++rendered;
        }
    });
    while (!rendered) std::this_thread::yield();
    for (int i = 0; i < 300; ++i) {
        int sound = lt_sound_load((dir / "constant.wav").c_str());
        int voice = lt_voice_play(sound, i % 3, 1, 1, 0);
        lt_voice_pause(voice, 1); lt_voice_pause(voice, 0);
        lt_bus_volume(i % 3, 0.5f, 0.01f); lt_ducking(0.2f, 0.01f, 0.01f);
        lt_voice_stop(voice, 0); lt_sound_unload(sound);
        if (i % 10 == 0) lt::audioReset();
    }
    controlsDone = true;
    callback.join(); expect(rendered >= 4000, "callback exercised throughout control mutations"); expect(callbackAllocations == 0 && callbackFrees == 0, "concurrent callback has no allocation/deallocation"); expect(finite, "concurrent render/control/unload/reset outputs remain finite and bounded");
    reset();
    int sound = lt_sound_load((dir / "constant.wav").c_str());
    // Terminal handle injection is test-only; the production counters never
    // reset or wrap. Verify the last valid handle, then deterministic failure.
    lt::nextVoice = uint64_t(std::numeric_limits<int>::max());
    int final = lt_voice_play(sound, 0, 1, 1, 0);
    expect(final == std::numeric_limits<int>::max(), "last positive voice handle supported");
    lt_voice_stop(final, 0); lt::audioReset();
    sound = lt_sound_load((dir / "constant.wav").c_str());
    expect(lt_voice_play(sound, 0, 1, 0, 0) == -1, "voice exhaustion survives reset and fails without wrapping");
    lt::nextSound = uint64_t(std::numeric_limits<int>::max());
    expect(lt_sound_load((dir / "constant.wav").c_str()) == std::numeric_limits<int>::max() &&
           lt_sound_load((dir / "constant.wav").c_str()) == -1, "asset ID exhaustion fails without wrapping");
    lt::audioShutdown();
}
void wickRenderChecks() {
    const char* scaleEnv = std::getenv("LANTERN_RENDER_SCALE");
    const char* nativeEnv = std::getenv("LANTERN_RENDER_NATIVE");
    const bool hadScale = scaleEnv != nullptr, hadNative = nativeEnv != nullptr;
    const std::string savedScale = scaleEnv ? scaleEnv : "", savedNative = nativeEnv ? nativeEnv : "";
    unsetenv("LANTERN_RENDER_NATIVE"); setenv("LANTERN_RENDER_SCALE","2",1);
    auto run = [&](const std::string& source) {
        auto vm = std::unique_ptr<VM, decltype(&wick::destroy)>(wick::create(),wick::destroy);
        std::string err;
        expect(registerLT(vm.get(),err),"register real render info signatures");
        expect(wick::load(vm.get(),source,"render-info.wick",err),"live Wick render info: "+err);
    };
    expect(lt_boot("render-info",3),"explicit live render info boot");
    run("check(lt.W == 400 and lt.H == 240 and lt.render_width() == 800 and lt.render_height() == 480 and not lt.render_native(), \"explicit physical target\")");
    lt_shutdown();
    unsetenv("LANTERN_RENDER_SCALE"); setenv("LANTERN_RENDER_NATIVE","1",1);
    renderOutputW = 853; renderOutputH = 511;
    expect(lt_boot("native-render-info",3),"native live render info boot");
    run("check(lt.W == 400 and lt.H == 240 and lt.render_width() == 850 and lt.render_height() == 510 and lt.render_native(), \"native odd backing target\")");
    renderOutputW = 1920; renderOutputH = 1080;
    lt_frame_begin(); lt_frame_end();
    run("check(lt.render_width() == 1800 and lt.render_height() == 1080, \"live native resize\")");
    lt_shutdown(); renderOutputW = 1200; renderOutputH = 720;
    if (hadScale) setenv("LANTERN_RENDER_SCALE",savedScale.c_str(),1); else unsetenv("LANTERN_RENDER_SCALE");
    if (hadNative) setenv("LANTERN_RENDER_NATIVE",savedNative.c_str(),1); else unsetenv("LANTERN_RENDER_NATIVE");
}
} // namespace
namespace lt {
bool platInit(const char*, int) { return true; }
void platShutdown() {}
bool platPoll() { return true; }
void platSetNativeRendering(bool) {}
bool platOutputSize(int& w, int& h) { w = renderOutputW; h = renderOutputH; return true; }
void platPresent(const uint8_t*, int, int, int) {}
double platTime() { return 0; }
int platInputDown(const char*) { return 0; }
int platGamepadConnected() { return 0; }
void platRumble(float, float, int) {}
PlatformTouch platTouch() { return {}; }
PlatformPointer platPointer() { return {}; }
PlatformMouseButtons platMouseButtons() { return {}; }
void platCursorVisible(bool) {}
std::string platSavePath(const char*) { return {}; }
bool platAudioStart() { ++starts; return deviceAvailable; }
void platAudioStop() { ++stops; }
} // namespace lt
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::string pattern = (std::filesystem::path(argv[1]) / "native-audio-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
    char* created = ::mkdtemp(name.data()); if (!created) return 2;
    std::filesystem::path dir(created);
    try {
        mixerChecks(dir); decoderChecks(dir); wickChecks(dir); concurrencyChecks(dir); wickRenderChecks();
        std::filesystem::remove_all(dir); // only this invocation's generated WAV fixtures
        std::cout << "native audio: " << assertions << " assertions passed\n";
        return 0;
    } catch (const std::exception& e) {
        lt::audioShutdown(); std::filesystem::remove_all(dir);
        std::cerr << "native audio failure after " << assertions << " assertions: " << e.what() << '\n';
        return 1;
    }
}
