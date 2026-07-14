// platform_sdl.cpp — the SDL2 desktop backend (macOS today; the same file
// is the template for any SDL-shaped port). Implements platform.hpp:
// window + integer-scale presentation of the 400x240 frame, keyboard/
// gamepad input, mouse-as-touch, the audio device, save-file paths.
// NO rendering happens here — the frame arrives fully rasterized.
#include "platform.hpp"
#include "audio.hpp"
#include "gfx.hpp" // SCREEN_W/H
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace lt {
namespace {

struct Plat {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;      // presentation only — we never draw
    SDL_Texture* frameTex = nullptr;  // the 400x240 frame our raster fills
    SDL_GameController* pad = nullptr;
    const Uint8* keys = nullptr;
    SDL_AudioDeviceID audioDev = 0;
    bool quit = false;
    // mouse-as-touch: raw window coords, converted to screen coords lazily
    // in platTouch() (the letterbox can change with every window resize)
    bool touchDown = false;
    float touchWinX = 0, touchWinY = 0;
};
Plat P;

void openFirstPad() {
    if (P.pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            P.pad = SDL_GameControllerOpen(i);
            if (P.pad) {
                std::fprintf(stderr, "lantern: gamepad '%s'\n",
                             SDL_GameControllerName(P.pad));
                return;
            }
        }
    }
}

struct Binding {
    SDL_Scancode key;
    SDL_GameControllerButton btn;   // SDL_CONTROLLER_BUTTON_INVALID = none
    SDL_GameControllerAxis axis;    // SDL_CONTROLLER_AXIS_INVALID = none
    int axisSign;                   // -1 / +1 for stick directions
};

const std::unordered_map<std::string, Binding>& bindings() {
    using B = SDL_GameControllerButton;
    using A = SDL_GameControllerAxis;
    static const std::unordered_map<std::string, Binding> m = {
        {"left",   {SDL_SCANCODE_LEFT,  SDL_CONTROLLER_BUTTON_DPAD_LEFT,  A::SDL_CONTROLLER_AXIS_LEFTX, -1}},
        {"right",  {SDL_SCANCODE_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, A::SDL_CONTROLLER_AXIS_LEFTX, +1}},
        {"up",     {SDL_SCANCODE_UP,    SDL_CONTROLLER_BUTTON_DPAD_UP,    A::SDL_CONTROLLER_AXIS_LEFTY, -1}},
        {"down",   {SDL_SCANCODE_DOWN,  SDL_CONTROLLER_BUTTON_DPAD_DOWN,  A::SDL_CONTROLLER_AXIS_LEFTY, +1}},
        {"z",      {SDL_SCANCODE_Z,      B::SDL_CONTROLLER_BUTTON_A, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"x",      {SDL_SCANCODE_X,      B::SDL_CONTROLLER_BUTTON_B, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"c",      {SDL_SCANCODE_C,      B::SDL_CONTROLLER_BUTTON_Y, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"space",  {SDL_SCANCODE_SPACE,  B::SDL_CONTROLLER_BUTTON_A, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"return", {SDL_SCANCODE_RETURN, B::SDL_CONTROLLER_BUTTON_START, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"escape", {SDL_SCANCODE_ESCAPE, B::SDL_CONTROLLER_BUTTON_BACK, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"a",      {SDL_SCANCODE_A, B::SDL_CONTROLLER_BUTTON_INVALID, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"s",      {SDL_SCANCODE_S, B::SDL_CONTROLLER_BUTTON_INVALID, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"d",      {SDL_SCANCODE_D, B::SDL_CONTROLLER_BUTTON_INVALID, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
        {"w",      {SDL_SCANCODE_W, B::SDL_CONTROLLER_BUTTON_INVALID, A::SDL_CONTROLLER_AXIS_INVALID, 0}},
    };
    return m;
}

// letterboxed destination of the 400x240 frame in renderer output pixels
SDL_Rect dstRect(int dw, int dh) {
    int s = std::max(1, std::min(dw / SCREEN_W, dh / SCREEN_H));
    return {(dw - SCREEN_W * s) / 2, (dh - SCREEN_H * s) / 2, SCREEN_W * s,
            SCREEN_H * s};
}

// tear down whatever a partially-failed init created — platInit's "false =
// failed" contract must not leak a live window/renderer behind it
bool initFail(const char* what) {
    std::fprintf(stderr, "%s: %s\n", what, SDL_GetError());
    if (P.frameTex) SDL_DestroyTexture(P.frameTex);
    if (P.ren) SDL_DestroyRenderer(P.ren);
    if (P.win) SDL_DestroyWindow(P.win);
    SDL_Quit();
    P = Plat{};
    return false;
}

void SDLCALL audioCallback(void*, Uint8* stream, int len) {
    mixerRender((float*)stream, len / (int)(sizeof(float) * 2));
}

} // namespace

bool platInit(const char* title, int windowScale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) !=
        0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (windowScale < 1) windowScale = 3;
    P.win = SDL_CreateWindow(title ? title : "lantern", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, SCREEN_W * windowScale,
                             SCREEN_H * windowScale,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!P.win) return initFail("SDL_CreateWindow");
    // Integer-scale with nearest sampling — the pixel identity lives here.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    // LANTERN_NOVSYNC=1 uncaps the frame rate (benchmarks/CI only)
    Uint32 rflags = std::getenv("LANTERN_NOVSYNC")
                        ? SDL_RENDERER_ACCELERATED
                        : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    P.ren = SDL_CreateRenderer(P.win, -1, rflags);
    if (!P.ren) return initFail("SDL_CreateRenderer");
    P.frameTex = SDL_CreateTexture(P.ren, SDL_PIXELFORMAT_ABGR8888,
                                   SDL_TEXTUREACCESS_STREAMING, SCREEN_W,
                                   SCREEN_H);
    if (!P.frameTex) return initFail("SDL_CreateTexture");
    P.keys = SDL_GetKeyboardState(nullptr);
    openFirstPad();
    return true;
}

void platShutdown() {
    if (P.pad) SDL_GameControllerClose(P.pad);
    if (P.frameTex) SDL_DestroyTexture(P.frameTex);
    if (P.ren) SDL_DestroyRenderer(P.ren);
    if (P.win) SDL_DestroyWindow(P.win);
    SDL_Quit();
    P = Plat{};
}

bool platPoll() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) P.quit = true;
        if (e.type == SDL_CONTROLLERDEVICEADDED) openFirstPad();
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && P.pad) {
            SDL_GameControllerClose(P.pad);
            P.pad = nullptr;
            openFirstPad();
        }
        // mouse-as-touch: left button is the finger
        if (e.type == SDL_MOUSEBUTTONDOWN &&
            e.button.button == SDL_BUTTON_LEFT) {
            P.touchDown = true;
            P.touchWinX = (float)e.button.x;
            P.touchWinY = (float)e.button.y;
        }
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
            P.touchDown = false;
        if (e.type == SDL_MOUSEMOTION && P.touchDown) {
            P.touchWinX = (float)e.motion.x;
            P.touchWinY = (float)e.motion.y;
        }
    }
    return !P.quit;
}

void platPresent(const uint8_t* fb) {
    if (!P.ren) return;
    SDL_UpdateTexture(P.frameTex, nullptr, fb, SCREEN_W * 4);
    int dw, dh;
    SDL_GetRendererOutputSize(P.ren, &dw, &dh);
    SDL_Rect dst = dstRect(dw, dh);
    SDL_SetRenderDrawColor(P.ren, 13, 13, 15, 255);
    SDL_RenderClear(P.ren);
    SDL_RenderCopy(P.ren, P.frameTex, nullptr, &dst);
    SDL_RenderPresent(P.ren);
}

double platTime() { return SDL_GetTicks64() / 1000.0; }

int platInputDown(const char* name) {
    auto it = bindings().find(name);
    if (it == bindings().end()) return 0;
    const Binding& bd = it->second;
    if (P.keys && P.keys[bd.key]) return 1;
    if (P.pad) {
        if (bd.btn != SDL_CONTROLLER_BUTTON_INVALID &&
            SDL_GameControllerGetButton(P.pad, bd.btn))
            return 1;
        if (bd.axis != SDL_CONTROLLER_AXIS_INVALID) {
            int v = SDL_GameControllerGetAxis(P.pad, bd.axis);
            if (bd.axisSign > 0 ? v > 13000 : v < -13000) return 1;
        }
    }
    return 0;
}

int platGamepadConnected() { return P.pad != nullptr; }

void platRumble(float low, float high, int durationMs) {
    if (!P.pad || durationMs <= 0) return;
    auto clamp16 = [](float v) {
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        return (Uint16)(v * 65535.0f);
    };
    SDL_GameControllerRumble(P.pad, clamp16(low), clamp16(high),
                             (Uint32)durationMs);
}

PlatformTouch platTouch() {
    PlatformTouch t;
    t.down = P.touchDown;
    if (!P.win || !P.ren) return t;
    // window points -> renderer output pixels (HiDPI) -> undo the letterbox
    int ww, wh, dw, dh;
    SDL_GetWindowSize(P.win, &ww, &wh);
    SDL_GetRendererOutputSize(P.ren, &dw, &dh);
    float px = P.touchWinX * (ww > 0 ? (float)dw / (float)ww : 1.0f);
    float py = P.touchWinY * (wh > 0 ? (float)dh / (float)wh : 1.0f);
    SDL_Rect d = dstRect(dw, dh);
    float s = (float)d.w / (float)SCREEN_W;
    t.x = std::clamp((px - (float)d.x) / s, 0.0f, (float)SCREEN_W - 1);
    t.y = std::clamp((py - (float)d.y) / s, 0.0f, (float)SCREEN_H - 1);
    return t;
}

std::string platSavePath(const char* name) {
    char* pref = SDL_GetPrefPath("lantern", "saves");
    if (!pref) return {};
    std::string p = std::string(pref) + name;
    SDL_free(pref);
    return p;
}

bool platAudioStart() {
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audioCallback;
    P.audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!P.audioDev) {
        std::fprintf(stderr, "audio: %s (continuing silent)\n",
                     SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(P.audioDev, 0);
    return true;
}

void platAudioStop() {
    if (P.audioDev) SDL_CloseAudioDevice(P.audioDev);
    P.audioDev = 0;
}

} // namespace lt
