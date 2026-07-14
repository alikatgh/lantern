// platform.hpp — the OS layer lantern runs on. Everything platform-specific
// (window, presentation, input devices, audio device, save-file location)
// lives behind these calls; engine.cpp and audio.cpp are platform-free.
// Backends: platform_sdl.cpp (macOS/desktop), platform_ios.mm (iOS/iPadOS).
// Exactly ONE backend is linked into a binary.
#pragma once
#include <cstdint>
#include <string>

namespace lt {

// Single touch point, 3DS-style. Coordinates are in 400x240 screen space
// (the backend undoes the letterbox/scale). While not down, x/y hold the
// last touched position.
struct PlatformTouch {
    bool down = false;
    float x = 0, y = 0;
};

bool platInit(const char* title, int windowScale);
void platShutdown();
bool platPoll();                       // pump events; false = quit requested
void platPresent(const uint8_t* fb);   // 400x240 RGBA top-down, integer-scale
double platTime();                     // monotonic seconds since platInit
int platInputDown(const char* name);   // named buttons (see lantern.h)
int platGamepadConnected();
void platRumble(float low, float high, int durationMs);
PlatformTouch platTouch();
std::string platSavePath(const char* name); // "" = storage unavailable
// Audio device: the backend opens a 48 kHz stereo float stream whose
// callback pulls lt::mixerRender (audio.hpp). Failure = silent engine.
bool platAudioStart();
void platAudioStop();

} // namespace lt
