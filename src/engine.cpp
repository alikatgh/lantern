// engine.cpp — implements the lantern C ABI (include/lantern.h) on top of
// Gfx + the platform layer (platform.hpp). This file owns ALL engine state
// and is platform-free; hosts (the Lua runner, C games) talk only through
// lantern.h, and the OS talks only through a platform_*.cpp backend.
#include "lantern.h"
#include "audio.hpp"
#include "gfx.hpp"
#include "obj.hpp"
#include "platform.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// The canonical named-input list (lantern.h documents these). The engine
// edge-detects over this list; what each name maps to is the backend's
// business (keyboard+pad on desktop, virtual pad later on touch screens).
const char* const kInputNames[] = {"left",  "right",  "up",     "down", "z",
                                   "x",     "c",      "space",  "return",
                                   "escape", "a",     "s",      "d",    "w"};

struct Engine {
    bool booted = false;
    lt::Gfx gfx;
    double prevTime = 0;
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
    bool prevTouchDown = false;                     // for lt_touch_pressed
};
Engine E;

lt::Mat4 trs(float x, float y, float z, float rx, float ry, float rz,
             float sx, float sy, float sz) {
    return lt::mul(lt::translate(x, y, z),
                   lt::mul(lt::mul(lt::rotateZ(rz),
                                   lt::mul(lt::rotateY(ry), lt::rotateX(rx))),
                           lt::scale(sx, sy, sz)));
}

} // namespace

extern "C" {

int lt_boot(const char* title, int window_scale) {
    if (!lt::platInit(title, window_scale)) return 0;
    if (!E.gfx.init()) {
        std::fprintf(stderr, "gfx init failed\n");
        lt::platShutdown();
        return 0;
    }
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
    E.prevTime = lt::platTime();
    E.booted = true;
    lt::audioInit(); // failure = silent engine, not a boot failure
    return 1;
}

void lt_shutdown(void) {
    lt::audioShutdown();
    lt::platShutdown();
    E = Engine{};
}

int lt_frame_poll(void) {
    if (!E.booted) return 0; // not booted (or boot failed): never loop
    // snapshot last frame's state for _pressed edge detection
    for (const char* name : kInputNames)
        E.prevDown[name] = lt_input_down(name) != 0;
    E.prevTouchDown = lt::platTouch().down;
    if (!lt::platPoll()) E.quit = true;
    // default dev behavior: Escape quits; lt_escape_quits(0) opts out
    if (E.escapeQuits && lt::platInputDown("escape") && !E.prevDown["escape"])
        E.quit = true;
    double now = lt::platTime();
    E.dt = now - E.prevTime;
    E.prevTime = now;
    if (E.dt > 0.1) E.dt = 0.1;
    if (E.fixedDt > 0) E.dt = E.fixedDt; // deterministic mode
    return E.quit ? 0 : 1;
}

double lt_frame_dt(void) { return E.dt; }

void lt_frame_begin(void) { E.gfx.beginFrame(); }

void lt_frame_end(void) {
    if (!E.booted) return;
    E.gfx.endFrame(); // flush the 2D batch into our framebuffer
    if (E.shotPath && E.frame == E.shotFrame) {
        E.gfx.screenshot(std::string(E.shotPath) + ".bmp");
        E.quit = true; // verification run: capture, then exit the loop
    }
    E.frame++;
    lt::platPresent(E.gfx.framebuffer());
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

int lt_input_down(const char* name) { return lt::platInputDown(name); }

int lt_input_pressed(const char* name) {
    if (!lt_input_down(name)) return 0;
    auto it = E.prevDown.find(name);
    return (it == E.prevDown.end() || !it->second) ? 1 : 0;
}

int lt_gamepad_connected(void) { return lt::platGamepadConnected(); }

void lt_rumble(float low, float high, int duration_ms) {
    lt::platRumble(low, high, duration_ms);
}

int lt_touch_down(void) { return lt::platTouch().down ? 1 : 0; }

int lt_touch_pressed(void) {
    return (lt::platTouch().down && !E.prevTouchDown) ? 1 : 0;
}

float lt_touch_x(void) { return lt::platTouch().x; }
float lt_touch_y(void) { return lt::platTouch().y; }

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

int lt_save_write(const char* name, const void* data, int len) {
    if (!saveNameOk(name) || len < 0) return 0;
    std::string p = lt::platSavePath(name);
    if (p.empty()) return 0;
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return 0;
    size_t wrote = len ? std::fwrite(data, 1, (size_t)len, f) : 0;
    std::fclose(f);
    return wrote == (size_t)len ? 1 : 0;
}

int lt_save_read(const char* name, void* buf, int buf_len) {
    if (!saveNameOk(name) || buf_len < 0) return -1;
    std::string p = lt::platSavePath(name);
    if (p.empty()) return -1;
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return -1;
    int got = (int)std::fread(buf, 1, (size_t)buf_len, f);
    std::fclose(f);
    return got;
}

double lt_time(void) {
    if (E.fixedDt > 0) return E.frame * E.fixedDt; // deterministic mode
    return lt::platTime();
}

} // extern "C"
