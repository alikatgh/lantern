// engine.cpp — implements the lantern C ABI (include/lantern.h) on top of
// SDL2 + Gfx. This file owns ALL engine state; hosts (the Lua runner, C
// games) talk only through lantern.h.
#include "lantern.h"
#include "audio.hpp"
#include "gfx.hpp"
#include "obj.hpp"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Engine {
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;      // presentation only — we never draw
    SDL_Texture* frameTex = nullptr;  // the 400x240 frame our raster fills
    SDL_GameController* pad = nullptr;
    lt::Gfx gfx;
    const Uint8* keys = nullptr;
    Uint64 prevCounter = 0;
    double dt = 1.0 / 60.0;
    bool quit = false;
    bool escapeQuits = true;          // lt_escape_quits() opts out
    // engine-owned verification contract (works for EVERY host, not just
    // the Lua runner): LANTERN_SHOT/_FRAME/_FIXED_DT env vars
    const char* shotPath = nullptr;
    int shotFrame = 60;
    long frame = 0;
    double fixedDt = 0;               // >0 = deterministic timing mode
    std::unordered_map<std::string, bool> prevDown; // for _pressed edges
};
Engine E;

void openFirstPad() {
    if (E.pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            E.pad = SDL_GameControllerOpen(i);
            if (E.pad) {
                std::fprintf(stderr, "lantern: gamepad '%s'\n",
                             SDL_GameControllerName(E.pad));
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

lt::Mat4 trs(float x, float y, float z, float rx, float ry, float rz,
             float sx, float sy, float sz) {
    return lt::mul(lt::translate(x, y, z),
                   lt::mul(lt::mul(lt::rotateZ(rz),
                                   lt::mul(lt::rotateY(ry), lt::rotateX(rx))),
                           lt::scale(sx, sy, sz)));
}

} // namespace

extern "C" {

// tear down whatever a partially-failed boot created — lt_boot's "0 =
// failed" contract must not leak a live window/renderer behind it
static int bootFail(const char* what) {
    std::fprintf(stderr, "%s: %s\n", what, SDL_GetError());
    if (E.frameTex) SDL_DestroyTexture(E.frameTex);
    if (E.ren) SDL_DestroyRenderer(E.ren);
    if (E.win) SDL_DestroyWindow(E.win);
    SDL_Quit();
    E = Engine{};
    return 0;
}

int lt_boot(const char* title, int window_scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) !=
        0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 0;
    }
    if (window_scale < 1) window_scale = 3;
    E.win = SDL_CreateWindow(
        title ? title : "lantern", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, lt::SCREEN_W * window_scale,
        lt::SCREEN_H * window_scale,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!E.win) return bootFail("SDL_CreateWindow");
    // Integer-scale with nearest sampling — the pixel identity lives here.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    // LANTERN_NOVSYNC=1 uncaps the frame rate (benchmarks/CI only)
    Uint32 rflags = std::getenv("LANTERN_NOVSYNC")
                        ? SDL_RENDERER_ACCELERATED
                        : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    E.ren = SDL_CreateRenderer(E.win, -1, rflags);
    if (!E.ren) return bootFail("SDL_CreateRenderer");
    E.frameTex = SDL_CreateTexture(E.ren, SDL_PIXELFORMAT_ABGR8888,
                                   SDL_TEXTUREACCESS_STREAMING, lt::SCREEN_W,
                                   lt::SCREEN_H);
    if (!E.frameTex) return bootFail("SDL_CreateTexture");
    if (!E.gfx.init()) return bootFail("gfx init");
    E.keys = SDL_GetKeyboardState(nullptr);
    E.prevCounter = SDL_GetPerformanceCounter();
    // engine-owned frame-verification contract (any host, Lua or C):
    // LANTERN_SHOT=<prefix> [LANTERN_SHOT_FRAME=N] → save <prefix>.bmp at
    // frame N (default 60) and request quit.
    E.shotPath = std::getenv("LANTERN_SHOT");
    if (const char* sf = std::getenv("LANTERN_SHOT_FRAME"))
        E.shotFrame = std::atoi(sf);
    // LANTERN_FIXED_DT=<seconds|1> → deterministic dt and lt_time (CI):
    // 1 or empty numeric means 1/60.
    if (const char* fd = std::getenv("LANTERN_FIXED_DT")) {
        E.fixedDt = std::atof(fd);
        if (E.fixedDt <= 0 || E.fixedDt >= 1.0) E.fixedDt = 1.0 / 60.0;
    }
    openFirstPad();
    lt::audioInit(); // failure = silent engine, not a boot failure
    return 1;
}

void lt_shutdown(void) {
    lt::audioShutdown();
    if (E.pad) SDL_GameControllerClose(E.pad);
    if (E.frameTex) SDL_DestroyTexture(E.frameTex);
    if (E.ren) SDL_DestroyRenderer(E.ren);
    if (E.win) SDL_DestroyWindow(E.win);
    SDL_Quit();
    E = Engine{};
}

int lt_frame_poll(void) {
    if (!E.win) return 0; // not booted (or boot failed): never loop
    // snapshot last frame's state for _pressed edge detection
    for (const auto& [name, bd] : bindings()) {
        (void)bd;
        E.prevDown[name] = lt_input_down(name.c_str()) != 0;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) E.quit = true;
        if (E.escapeQuits && e.type == SDL_KEYDOWN &&
            e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
            E.quit = true; // default dev behavior; lt_escape_quits(0) opts out
        if (e.type == SDL_CONTROLLERDEVICEADDED) openFirstPad();
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && E.pad) {
            SDL_GameControllerClose(E.pad);
            E.pad = nullptr;
            openFirstPad();
        }
    }
    Uint64 now = SDL_GetPerformanceCounter();
    E.dt = (now - E.prevCounter) / (double)SDL_GetPerformanceFrequency();
    E.prevCounter = now;
    if (E.dt > 0.1) E.dt = 0.1;
    if (E.fixedDt > 0) E.dt = E.fixedDt; // deterministic mode
    return E.quit ? 0 : 1;
}

double lt_frame_dt(void) { return E.dt; }

void lt_frame_begin(void) { E.gfx.beginFrame(); }

void lt_frame_end(void) {
    if (!E.ren) return; // not booted
    E.gfx.endFrame(); // flush the 2D batch into our framebuffer
    if (E.shotPath && E.frame == E.shotFrame) {
        E.gfx.screenshot(std::string(E.shotPath) + ".bmp");
        E.quit = true; // verification run: capture, then exit the loop
    }
    E.frame++;
    SDL_UpdateTexture(E.frameTex, nullptr, E.gfx.framebuffer(),
                      lt::SCREEN_W * 4);
    int dw, dh;
    SDL_GetRendererOutputSize(E.ren, &dw, &dh);
    int s = std::max(1, std::min(dw / lt::SCREEN_W, dh / lt::SCREEN_H));
    SDL_Rect dst{(dw - lt::SCREEN_W * s) / 2, (dh - lt::SCREEN_H * s) / 2,
                 lt::SCREEN_W * s, lt::SCREEN_H * s};
    SDL_SetRenderDrawColor(E.ren, 13, 13, 15, 255);
    SDL_RenderClear(E.ren);
    SDL_RenderCopy(E.ren, E.frameTex, nullptr, &dst);
    SDL_RenderPresent(E.ren);
}

void lt_run(lt_update_fn update, lt_draw_fn draw) {
    while (lt_frame_poll()) {
        if (update) update(E.dt);
        lt_frame_begin();
        if (draw) draw();
        lt_frame_end();
    }
}

void lt_quit(void) { E.quit = true; }

void lt_escape_quits(int enable) { E.escapeQuits = enable != 0; }

void lt_resources_reset(void) {
    E.gfx.reset();
    lt::audioReset();
}

int lt_screen_w(void) { return lt::SCREEN_W; }
int lt_screen_h(void) { return lt::SCREEN_H; }

void lt_clear(float r, float g, float b) { E.gfx.clear(r, g, b); }

void lt_screenshot(const char* bmp_path) { E.gfx.screenshot(bmp_path); }

void lt_camera(float ex, float ey, float ez, float tx, float ty, float tz,
               float fov_deg) {
    E.gfx.setCamera({ex, ey, ez}, {tx, ty, tz}, fov_deg);
}

void lt_light(float dx, float dy, float dz, float ambient) {
    E.gfx.setLight({dx, dy, dz}, ambient);
}

void lt_point_light(int i, float x, float y, float z, float radius, float r,
                    float g, float b) {
    E.gfx.setPointLight(i, {x, y, z}, radius, {r, g, b});
}

void lt_fog(float start, float end, float r, float g, float b) {
    E.gfx.setFog(start, end, {r, g, b});
}

int lt_mesh_cube(void) { return E.gfx.makeCube(); }
int lt_mesh_plane(int segs) { return E.gfx.makePlane(segs); }
int lt_mesh_sphere(int seg) { return E.gfx.makeSphere(seg); }
int lt_mesh_cylinder(int seg) { return E.gfx.makeCylinder(seg); }
int lt_mesh_cone(int seg) { return E.gfx.makeCone(seg); }

int lt_mesh_create(const float* verts, int vert_count) {
    return E.gfx.makeMesh(verts, vert_count);
}

int lt_mesh_load_obj(const char* path) {
    std::vector<float> v;
    if (!lt::loadObj(path, v)) return -1;
    return E.gfx.makeMesh(v.data(), (int)v.size() / lt::VERT_FLOATS);
}

void lt_billboard(int tex, float x, float y, float z, float w, float h,
                  float u0, float v0, float u1, float v1) {
    E.gfx.billboard(tex, {x, y, z}, w, h, u0, v0, u1, v1);
}

void lt_shadow(float x, float y, float z, float radius, float alpha) {
    E.gfx.shadowBlob({x, y, z}, radius, alpha);
}

void lt_draw_mesh_lerp(int mesh_a, int mesh_b, float t, int tex, float x,
                       float y, float z, float rx, float ry, float rz,
                       float sx, float sy, float sz, float r, float g,
                       float b) {
    E.gfx.drawMeshLerp(mesh_a, mesh_b, t, tex,
                       trs(x, y, z, rx, ry, rz, sx, sy, sz), r, g, b);
}

void lt_draw_mesh(int mesh, int tex, float x, float y, float z, float rx,
                  float ry, float rz, float sx, float sy, float sz, float r,
                  float g, float b) {
    E.gfx.drawMesh(mesh, tex, trs(x, y, z, rx, ry, rz, sx, sy, sz), r, g, b);
}

int lt_texture_load(const char* bmp_path, int* out_w, int* out_h) {
    return E.gfx.loadTexture(bmp_path, out_w, out_h);
}

void lt_rect(float x, float y, float w, float h, float r, float g, float b,
             float a) {
    E.gfx.rect(x, y, w, h, r, g, b, a);
}

void lt_sprite(int tex, float x, float y, float sx, float sy) {
    E.gfx.sprite(tex, x, y, sx, sy);
}

void lt_sprite_ex(int tex, float x, float y, float sx, float sy, float rot,
                  float r, float g, float b, float a) {
    E.gfx.spriteEx(tex, x, y, sx, sy, rot, r, g, b, a);
}

void lt_sprite_uv(int tex, float x, float y, float w, float h, float u0,
                  float v0, float u1, float v1) {
    E.gfx.spriteUV(tex, x, y, w, h, u0, v0, u1, v1);
}

void lt_print(const char* text, float x, float y, float r, float g, float b,
              float a) {
    E.gfx.print(text, x, y, r, g, b, a);
}

int lt_input_down(const char* name) {
    auto it = bindings().find(name);
    if (it == bindings().end()) return 0;
    const Binding& bd = it->second;
    if (E.keys && E.keys[bd.key]) return 1;
    if (E.pad) {
        if (bd.btn != SDL_CONTROLLER_BUTTON_INVALID &&
            SDL_GameControllerGetButton(E.pad, bd.btn))
            return 1;
        if (bd.axis != SDL_CONTROLLER_AXIS_INVALID) {
            int v = SDL_GameControllerGetAxis(E.pad, bd.axis);
            if (bd.axisSign > 0 ? v > 13000 : v < -13000) return 1;
        }
    }
    return 0;
}

int lt_input_pressed(const char* name) {
    if (!lt_input_down(name)) return 0;
    auto it = E.prevDown.find(name);
    return (it == E.prevDown.end() || !it->second) ? 1 : 0;
}

int lt_gamepad_connected(void) { return E.pad != nullptr; }

void lt_rumble(float low, float high, int duration_ms) {
    if (!E.pad || duration_ms <= 0) return;
    auto clamp16 = [](float v) {
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        return (Uint16)(v * 65535.0f);
    };
    SDL_GameControllerRumble(E.pad, clamp16(low), clamp16(high),
                             (Uint32)duration_ms);
}

int lt_sound_load(const char* wav_path) { return lt::audioLoad(wav_path); }

int lt_sound_play(int sound, float volume, int loop) {
    return lt::audioPlay(sound, volume, loop != 0);
}

void lt_sound_stop(int channel) { lt::audioStop(channel); }

void lt_master_volume(float volume) { lt::audioMaster(volume); }

static bool saveNameOk(const char* n) {
    if (!n || !*n) return false;
    for (const char* p = n; *p; p++) {
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-'))
            return false;
    }
    return true;
}

static std::string savePath(const char* name) {
    char* pref = SDL_GetPrefPath("lantern", "saves");
    if (!pref) return {};
    std::string p = std::string(pref) + name;
    SDL_free(pref);
    return p;
}

int lt_save_write(const char* name, const void* data, int len) {
    if (!saveNameOk(name) || len < 0) return 0;
    std::string p = savePath(name);
    if (p.empty()) return 0;
    SDL_RWops* f = SDL_RWFromFile(p.c_str(), "wb");
    if (!f) return 0;
    size_t wrote = len ? SDL_RWwrite(f, data, 1, (size_t)len) : 0;
    SDL_RWclose(f);
    return wrote == (size_t)len ? 1 : 0;
}

int lt_save_read(const char* name, void* buf, int buf_len) {
    if (!saveNameOk(name) || buf_len < 0) return -1;
    std::string p = savePath(name);
    if (p.empty()) return -1;
    SDL_RWops* f = SDL_RWFromFile(p.c_str(), "rb");
    if (!f) return -1;
    int got = (int)SDL_RWread(f, buf, 1, (size_t)buf_len);
    SDL_RWclose(f);
    return got;
}

double lt_time(void) {
    if (E.fixedDt > 0) return E.frame * E.fixedDt; // deterministic mode
    return SDL_GetTicks64() / 1000.0;
}

} // extern "C"
