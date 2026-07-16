// audio.hpp — internal interface for the lantern mixer (see audio.cpp).
// Platform-free; the platform backend owns the device and pulls
// mixerRender from its audio callback (any thread — the mixer locks).
#pragma once

namespace lt {
bool audioInit();               // starts the platform device; false = silent
void audioShutdown();
int  audioLoad(const char* wavPath);            // -1 on failure
int  audioPlay(int sound, float volume, bool loop); // returns channel or -1
void audioStop(int channel);
void audioChannelVolume(int channel, float volume); // live fades/crossfades
void audioMaster(float volume);
void audioReset();              // stop all channels, free all sounds
// Called by the platform audio callback: mix `frames` interleaved stereo
// float frames into out (zero-filled first).
void mixerRender(float* out, int frames);
} // namespace lt
