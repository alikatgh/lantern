// audio.hpp — internal interface for the lantern mixer (see audio.cpp).
#pragma once

namespace lt {
bool audioInit();               // opens the device; false = stay silent
void audioShutdown();
int  audioLoad(const char* wavPath);            // -1 on failure
int  audioPlay(int sound, float volume, bool loop); // returns channel or -1
void audioStop(int channel);
void audioMaster(float volume);
void audioReset();              // stop all channels, free all sounds
} // namespace lt
