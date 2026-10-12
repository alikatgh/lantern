// Platform-free, bounded 48 kHz stereo mixer. Platform owns the device and
// calls mixerRender on its callback thread; all state is mutex guarded.
#pragma once

namespace lt {
bool audioInit(); // failure is a silent engine, never a boot failure
void audioShutdown();
int audioLoad(const char* wavPath); // -1 on failure; decoding is off callback
bool audioUnload(int sound); // refuses active or paused users
int audioPlay(int sound, float volume, bool loop); // legacy channel 0..15
void audioStop(int channel);
void audioChannelVolume(int channel, float volume);
void audioMaster(float volume);
// New voices share the 16-channel pool, but use non-reused positive handles.
int audioVoicePlay(int sound, int bus, float volume, bool loop, float fade);
bool audioVoiceStop(int voice, float fade);
bool audioVoicePause(int voice, bool paused);
bool audioVoiceActive(int voice);
int audioMusic(int sound, float volume, bool loop, float crossfade);
bool audioBusVolume(int bus, float volume, float ramp);
bool audioDucking(float level, float attack, float release);
void audioReset(); // stops/frees assets; resets buses/duck, retains master
void mixerRender(float* out, int frames);
} // namespace lt
