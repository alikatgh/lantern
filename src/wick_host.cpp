// wick_host.cpp — runs a wick game (main.wick) against the lantern C ABI.
// Mirrors the Lua host's dev loop exactly: hot-reload on save, in-engine
// error screen with file:line, LANTERN_SHOT handled by the engine. The
// lt.* API is registered with TYPED signatures, so a wrong argument is a
// compile error on screen, not a runtime surprise.
#include "wick_host.hpp"
#include "lantern.h"
#include "path_sandbox.hpp"
#include "../wick/wick.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <limits>
#include <fstream>
#include <memory>
#include <exception>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

using wick::Value;
using wick::VM;

static std::string g_dir;

// Resolve under g_dir or set a runtime error and return false.
static bool resolvePath(VM& vm, const std::string& rel, std::string& out) {
    std::string err;
    if (!lt::pathResolveUnder(g_dir, rel, out, err)) {
        wick::setError(vm, "path refused: " + err);
        return false;
    }
    return true;
}

// ---- natives: thin typed shims over lt_* ----------------------------------
#define N0(name, call) \
    static Value name(VM&, const Value*, int) { call; return Value::nil(); }
#define NARG(i) (float)a[i].d

static Value wFramebufferW(VM&, const Value*, int) { return Value::num(lt_render_width()); }
static Value wFramebufferH(VM&, const Value*, int) { return Value::num(lt_render_height()); }
static Value wRenderNative(VM&, const Value*, int) { return Value::boolean(lt_render_native() != 0); }
static Value wRenderPainted(VM&, const Value*, int) { return Value::boolean(lt_render_painted() != 0); }
static Value wRenderWorkers(VM&, const Value*, int) { return Value::num(lt_render_workers()); }
static Value wClear(VM&, const Value* a, int) { lt_clear(NARG(0), NARG(1), NARG(2)); return Value::nil(); }
static Value wCamera(VM&, const Value* a, int) { lt_camera(NARG(0), NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6)); return Value::nil(); }
static Value wLight(VM&, const Value* a, int) { lt_light(NARG(0), NARG(1), NARG(2), NARG(3)); return Value::nil(); }
static Value wPointLight(VM&, const Value* a, int) { lt_point_light((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6), NARG(7)); return Value::nil(); }
static Value wFog(VM&, const Value* a, int) { lt_fog(NARG(0), NARG(1), NARG(2), NARG(3), NARG(4)); return Value::nil(); }
static Value wCube(VM&, const Value*, int) { return Value::num(lt_mesh_cube()); }
static Value wPlane(VM&, const Value* a, int) { return Value::num(lt_mesh_plane((int)a[0].d)); }
static Value wSphere(VM&, const Value* a, int) { return Value::num(lt_mesh_sphere((int)a[0].d)); }
static Value wCylinder(VM&, const Value* a, int) { return Value::num(lt_mesh_cylinder((int)a[0].d)); }
static Value wCone(VM&, const Value* a, int) { return Value::num(lt_mesh_cone((int)a[0].d)); }
static Value wMesh(VM& vm, const Value* a, int) {
    int n = wick::listLen(a[0]);
    if (n == 0 || n % 12 != 0) {
        wick::setError(vm, "lt.mesh: table length must be a multiple of "
                           "12 (pos3 normal3 uv2 rgba4 per vertex)");
        return Value::nil();
    }
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; i++) v[(size_t)i] = (float)wick::listGet(a[0], i).d;
    return Value::num(lt_mesh_create(v.data(), n / 12));
}
static Value wLoadMesh(VM& vm, const Value* a, int) {
    std::string p;
    if (!resolvePath(vm, wick::getStr(a[0]), p)) return Value::num(-1);
    int id = lt_mesh_load_obj(p.c_str());
    if (id < 0) wick::setError(vm, "load_mesh failed: " + wick::getStr(a[0]));
    return Value::num(id);
}
static Value wDraw(VM&, const Value* a, int) {
    lt_draw_mesh((int)a[0].d, (int)a[13].d, NARG(1), NARG(2), NARG(3),
                 NARG(4), NARG(5), NARG(6), NARG(7), NARG(8), NARG(9),
                 NARG(10), NARG(11), NARG(12));
    return Value::nil();
}
static Value wDrawLerp(VM&, const Value* a, int) {
    lt_draw_mesh_lerp((int)a[0].d, (int)a[1].d, NARG(2), (int)a[15].d,
                      NARG(3), NARG(4), NARG(5), NARG(6), NARG(7), NARG(8),
                      NARG(9), NARG(10), NARG(11), NARG(12), NARG(13),
                      NARG(14));
    return Value::nil();
}
static Value wShadow(VM&, const Value* a, int) { lt_shadow(NARG(0), NARG(1), NARG(2), NARG(3), NARG(4)); return Value::nil(); }
static Value wBillboard(VM&, const Value* a, int) { lt_billboard((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6), NARG(7), NARG(8), NARG(9)); return Value::nil(); }
static Value wRect(VM&, const Value* a, int) { lt_rect(NARG(0), NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6), NARG(7)); return Value::nil(); }
static Value wLoadTexture(VM& vm, const Value* a, int) {
    std::string p;
    if (!resolvePath(vm, wick::getStr(a[0]), p)) return Value::num(-1);
    int w, h;
    int id = lt_texture_load(p.c_str(), &w, &h);
    if (id < 0) wick::setError(vm, "load_texture failed: " + wick::getStr(a[0]));
    return Value::num(id);
}
static Value wSprite(VM&, const Value* a, int) { lt_sprite((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4)); return Value::nil(); }
static Value wSpriteEx(VM&, const Value* a, int) { lt_sprite_ex((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6), NARG(7), NARG(8), NARG(9)); return Value::nil(); }
static Value wSpriteUV(VM&, const Value* a, int) { lt_sprite_uv((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6), NARG(7), NARG(8)); return Value::nil(); }
static Value wSpriteUVTinted(VM&, const Value* a, int) { lt_sprite_uv_tinted((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6), NARG(7), NARG(8), NARG(9), NARG(10), NARG(11)); return Value::nil(); }
static Value wTextureHasAlpha(VM&, const Value* a, int) { return Value::boolean(lt_texture_has_alpha((int)a[0].d) != 0); }
static Value wSpriteGrounded(VM&, const Value* a, int) {
    lt_sprite_grounded((int)a[0].d, (int)a[1].d, (int)a[2].d, (int)a[3].d,
                       NARG(4), NARG(5), NARG(6), NARG(7),
                       NARG(8), (int)a[9].d,
                       NARG(10), NARG(11), NARG(12), NARG(13));
    return Value::nil();
}
static Value wGlowRadial(VM&, const Value* a, int) {
    lt_glow_radial(NARG(0), NARG(1), NARG(2), NARG(3),
                   NARG(4), NARG(5), NARG(6), NARG(7));
    return Value::nil();
}
static Value wDrawCount(VM&, const Value*, int) { return Value::num(lt_draw_count()); }
static Value wPrint(VM&, const Value* a, int) { lt_print(wick::getStr(a[0]).c_str(), NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6)); return Value::nil(); }
static Value wKey(VM&, const Value* a, int) { return Value::boolean(lt_input_down(wick::getStr(a[0]).c_str()) != 0); }
static Value wPressed(VM&, const Value* a, int) { return Value::boolean(lt_input_pressed(wick::getStr(a[0]).c_str()) != 0); }
static Value wGamepad(VM&, const Value*, int) { return Value::boolean(lt_gamepad_connected() != 0); }
static Value wRumble(VM&, const Value* a, int) { lt_rumble(NARG(0), NARG(1), (int)a[2].d); return Value::nil(); }
static Value wLoadSound(VM& vm, const Value* a, int) {
    std::string p;
    if (!resolvePath(vm, wick::getStr(a[0]), p)) return Value::num(-1);
    int id = lt_sound_load(p.c_str());
    if (id < 0) wick::setError(vm, "load_sound failed: " + wick::getStr(a[0]));
    return Value::num(id);
}
// Validate doubles before narrowing to int/float. Typed signatures reject
// wrong Wick types at compile time; these guards also protect direct embedding.
static bool audioNumber(VM& vm, const Value& a, double low, double high,
                        bool integral, const char* operation) {
    if (a.tag == Value::NUM && std::isfinite(a.d) && a.d >= low && a.d <= high &&
        (!integral || std::trunc(a.d) == a.d)) return true;
    wick::setError(vm, std::string(operation) + ": invalid audio argument");
    return false;
}
static bool audioId(VM& vm, const Value& a, const char* operation) {
    return audioNumber(vm, a, -1, std::numeric_limits<int>::max(), true, operation);
}
static bool audioGain(VM& vm, const Value& a, const char* operation) {
    return audioNumber(vm, a, 0, 1, false, operation);
}
static bool audioTime(VM& vm, const Value& a, const char* operation) {
    return audioNumber(vm, a, 0, 60, false, operation);
}
static bool audioFlag(VM& vm, const Value& a, const char* operation) {
    return audioNumber(vm, a, 0, 1, true, operation);
}
static Value wTryLoadSound(VM& vm, const Value* a, int) {
    std::string p;
    if (!resolvePath(vm, wick::getStr(a[0]), p)) return Value::num(-1);
    return Value::num(lt_sound_load(p.c_str()));
}
static Value wPlay(VM& vm, const Value* a, int) {
    const double gainLimit = double(std::numeric_limits<float>::max()) / 16;
    if (!audioId(vm, a[0], "play") || !audioNumber(vm, a[1], -gainLimit, gainLimit, false, "play") ||
        !audioNumber(vm, a[2], -std::numeric_limits<double>::max(),
                     std::numeric_limits<double>::max(), false, "play")) return Value::num(-1);
    return Value::num(lt_sound_play((int)a[0].d, NARG(1), a[2].d != 0));
}
static Value wStop(VM& vm, const Value* a, int) {
    if (audioId(vm, a[0], "stop")) lt_sound_stop((int)a[0].d);
    return Value::nil();
}
static Value wChanVol(VM& vm, const Value* a, int) {
    if (audioId(vm, a[0], "channel_volume") && audioNumber(vm, a[1],
        -std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), false, "channel_volume"))
        lt_channel_volume((int)a[0].d, float(std::clamp(a[1].d, 0.0, 1.0)));
    return Value::nil();
}
static Value wVolume(VM& vm, const Value* a, int) {
    if (audioNumber(vm, a[0], -std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::max(), false, "volume"))
        lt_master_volume(float(std::clamp(a[0].d, 0.0, 1.0)));
    return Value::nil();
}
static Value wUnloadSound(VM& vm, const Value* a, int) {
    return Value::boolean(audioId(vm, a[0], "sound_unload") && lt_sound_unload((int)a[0].d));
}
static Value wVoicePlay(VM& vm, const Value* a, int) {
    if (!audioId(vm, a[0], "voice_play") || !audioNumber(vm, a[1], 0, 2, true, "voice_play") ||
        !audioGain(vm, a[2], "voice_play") || !audioFlag(vm, a[3], "voice_play") ||
        !audioTime(vm, a[4], "voice_play")) return Value::num(-1);
    return Value::num(lt_voice_play((int)a[0].d, (int)a[1].d, NARG(2), (int)a[3].d, NARG(4)));
}
static Value wVoiceStop(VM& vm, const Value* a, int) {
    return Value::boolean(audioId(vm, a[0], "voice_stop") && audioTime(vm, a[1], "voice_stop") &&
                          lt_voice_stop((int)a[0].d, NARG(1)));
}
static Value wVoicePause(VM& vm, const Value* a, int) {
    return Value::boolean(audioId(vm, a[0], "voice_pause") && audioFlag(vm, a[1], "voice_pause") &&
                          lt_voice_pause((int)a[0].d, (int)a[1].d));
}
static Value wVoiceActive(VM& vm, const Value* a, int) {
    return Value::boolean(audioId(vm, a[0], "voice_active") && lt_voice_active((int)a[0].d));
}
static Value wMusic(VM& vm, const Value* a, int) {
    if (!audioId(vm, a[0], "music") || !audioGain(vm, a[1], "music") ||
        !audioFlag(vm, a[2], "music") || !audioTime(vm, a[3], "music")) return Value::num(-1);
    return Value::num(lt_music((int)a[0].d, NARG(1), (int)a[2].d, NARG(3)));
}
static Value wBusVolume(VM& vm, const Value* a, int) {
    return Value::boolean(audioNumber(vm, a[0], 0, 2, true, "bus_volume") &&
        audioGain(vm, a[1], "bus_volume") && audioTime(vm, a[2], "bus_volume") &&
        lt_bus_volume((int)a[0].d, NARG(1), NARG(2)));
}
static Value wDucking(VM& vm, const Value* a, int) {
    return Value::boolean(audioGain(vm, a[0], "ducking") && audioTime(vm, a[1], "ducking") &&
        audioTime(vm, a[2], "ducking") && lt_ducking(NARG(0), NARG(1), NARG(2)));
}
static Value wSave(VM&, const Value* a, int) {
    std::string d = wick::getStr(a[1]);
    return Value::boolean(
        lt_save_write(wick::getStr(a[0]).c_str(), d.data(), (int)d.size()) != 0);
}
static Value wLoadSave(VM& vm, const Value* a, int) {
    std::vector<char> buf(1 << 20);
    int got = lt_save_read(wick::getStr(a[0]).c_str(), buf.data(),
                           (int)buf.size());
    if (got < 0) return Value::nil();          // str? — nil means no save
    return wick::makeStr(vm, std::string(buf.data(), (size_t)got));
}
static Value wTouchDown(VM&, const Value*, int) { return Value::boolean(lt_touch_down() != 0); }
static Value wTouchPressed(VM&, const Value*, int) { return Value::boolean(lt_touch_pressed() != 0); }
static Value wMousePressed(VM&, const Value* a, int) { return Value::boolean(lt_mouse_pressed(wick::getStr(a[0]).c_str()) != 0); }
static Value wTouchX(VM&, const Value*, int) { return Value::num(lt_touch_x()); }
static Value wTouchY(VM&, const Value*, int) { return Value::num(lt_touch_y()); }
static Value wTouchPressX(VM&, const Value*, int) { return Value::num(lt_touch_press_x()); }
static Value wTouchPressY(VM&, const Value*, int) { return Value::num(lt_touch_press_y()); }
static Value wTouchPressInside(VM&, const Value*, int) { return Value::boolean(lt_touch_press_inside() != 0); }
static Value wPointerX(VM&, const Value*, int) { return Value::num(lt_pointer_x()); }
static Value wPointerY(VM&, const Value*, int) { return Value::num(lt_pointer_y()); }
static Value wPointerInside(VM&, const Value*, int) { return Value::boolean(lt_pointer_inside() != 0); }
static Value wCursorVisible(VM&, const Value* a, int) { lt_cursor_visible(a[0].b ? 1 : 0); return Value::nil(); }
static Value wQuit(VM&, const Value*, int) { lt_quit(); return Value::nil(); }
static Value wEscQuits(VM&, const Value* a, int) { lt_escape_quits(a[0].b ? 1 : 0); return Value::nil(); }
static Value wTime(VM&, const Value*, int) { return Value::num(lt_time()); }
static Value wScreenshot(VM& vm, const Value* a, int) {
    std::string out, err;
    if (!lt::pathResolveScreenshot(g_dir, wick::getStr(a[0]), out, err)) {
        wick::setError(vm, "screenshot refused: " + err);
        return Value::nil();
    }
    lt_screenshot(out.c_str());
    return Value::nil();
}

static bool registerLT(VM* vm, std::string& err) {
    struct Reg { const char* sig; wick::NativeFn fn; };
    static const Reg regs[] = {
        {"render_width(): num", wFramebufferW},
        {"render_height(): num", wFramebufferH},
        {"render_native(): bool", wRenderNative},
        {"render_painted(): bool", wRenderPainted},
        {"render_workers(): num", wRenderWorkers},
        {"clear(num, num, num)", wClear},
        {"camera(num, num, num, num, num, num, num=55)", wCamera},
        {"light(num, num, num, num=0.35)", wLight},
        {"point_light(num, num, num, num, num, num=1, num=1, num=1)", wPointLight},
        {"fog(num, num, num, num, num)", wFog},
        {"cube(): num", wCube},
        {"plane(num=1): num", wPlane},
        {"sphere(num=12): num", wSphere},
        {"cylinder(num=16): num", wCylinder},
        {"cone(num=16): num", wCone},
        {"mesh(list): num", wMesh},
        {"load_mesh(str): num", wLoadMesh},
        {"draw(num, num, num, num, num=0, num=0, num=0, num=1, num=1, num=1, num=1, num=1, num=1, num=-1)", wDraw},
        {"draw_lerp(num, num, num, num, num, num, num=0, num=0, num=0, num=1, num=1, num=1, num=1, num=1, num=1, num=-1)", wDrawLerp},
        {"shadow(num, num, num, num, num=0.35)", wShadow},
        {"billboard(num, num, num, num, num, num, num=0, num=0, num=1, num=1)", wBillboard},
        {"rect(num, num, num, num, num, num, num, num=1)", wRect},
        {"load_texture(str): num", wLoadTexture},
        {"sprite(num, num, num, num=1, num=1)", wSprite},
        {"sprite_ex(num, num, num, num=1, num=1, num=0, num=1, num=1, num=1, num=1)", wSpriteEx},
        {"sprite_uv(num, num, num, num, num, num, num, num, num)", wSpriteUV},
        {"sprite_uv_tinted(num, num, num, num, num, num, num, num, num, num, num, num)", wSpriteUVTinted},
        {"texture_has_alpha(num): bool", wTextureHasAlpha},
        {"sprite_grounded(num, num, num, num, num, num, num, num, num, num=1, num=1, num=1, num=1, num=0)", wSpriteGrounded},
        {"glow_radial(num, num, num, num, num=1, num=1, num=1, num=1)", wGlowRadial},
        {"draw_count(): num", wDrawCount},
        {"print(str, num, num, num=1, num=1, num=1, num=1)", wPrint},
        {"key(str): bool", wKey},
        {"pressed(str): bool", wPressed},
        {"gamepad(): bool", wGamepad},
        {"touch_down(): bool", wTouchDown},
        {"touch_pressed(): bool", wTouchPressed},
        {"mouse_pressed(str): bool", wMousePressed},
        {"touch_x(): num", wTouchX},
        {"touch_y(): num", wTouchY},
        {"touch_press_x(): num", wTouchPressX},
        {"touch_press_y(): num", wTouchPressY},
        {"touch_press_inside(): bool", wTouchPressInside},
        {"pointer_x(): num", wPointerX},
        {"pointer_y(): num", wPointerY},
        {"pointer_inside(): bool", wPointerInside},
        {"cursor_visible(bool)", wCursorVisible},
        {"rumble(num, num, num)", wRumble},
        {"load_sound(str): num", wLoadSound},
        {"try_load_sound(str): num", wTryLoadSound},
        {"sound_unload(num): bool", wUnloadSound},
        {"voice_play(num, num=0, num=1, num=0, num=0): num", wVoicePlay},
        {"voice_stop(num, num=0): bool", wVoiceStop},
        {"voice_pause(num, num=1): bool", wVoicePause},
        {"voice_active(num): bool", wVoiceActive},
        {"music(num, num=1, num=1, num=0.5): num", wMusic},
        {"bus_volume(num, num, num=0): bool", wBusVolume},
        {"ducking(num, num=0.08, num=0.3): bool", wDucking},
        {"play(num, num=1, num=0): num", wPlay},
        {"stop(num)", wStop},
        {"channel_volume(num, num)", wChanVol},
        {"volume(num)", wVolume},
        {"save(str, str): bool", wSave},
        {"load_save(str): str?", wLoadSave},
        {"quit()", wQuit},
        {"escape_quits(bool)", wEscQuits},
        {"time(): num", wTime},
        {"screenshot(str)", wScreenshot},
    };
    for (const Reg& r : regs)
        if (!wick::addNative(vm, "lt", r.sig, r.fn, err)) return false;
    wick::addConst(vm, "lt", "W", lt_screen_w());
    wick::addConst(vm, "lt", "H", lt_screen_h());
    wick::addConst(vm, "lt", "SFX", LT_BUS_SFX);
    wick::addConst(vm, "lt", "MUSIC", LT_BUS_MUSIC);
    wick::addConst(vm, "lt", "SPEECH", LT_BUS_SPEECH);
    return true;
}

bool checkWickSource(const std::string& source, const std::string& chunkName,
                     wick::Diagnostic& diagnostic) {
    try {
        std::unique_ptr<VM, decltype(&wick::destroy)> environment(wick::create(),
                                                               wick::destroy);
        std::string err;
        if (!registerLT(environment.get(), err)) {
            diagnostic = {chunkName, 0, err, "host", "LANTERN_CHECK_HOST"};
            return false;
        }
        return wick::check(environment.get(), source, chunkName, diagnostic);
    } catch (const std::exception& error) {
        diagnostic = {chunkName, 0, error.what(), "host", "LANTERN_CHECK_HOST"};
        return false;
    }
}

// ---- the dev loop (mirror of the Lua host) --------------------------------

static time_t fileMtime(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static std::string g_error;

static void loadWickGame(VM* vm, const std::string& mainWick) {
    lt_resources_reset();
    wick::reset(vm);
    std::string src;
    if (!readFile(mainWick, src)) {
        g_error = "cannot read " + mainWick;
        return;
    }
    std::string err;
    if (!wick::load(vm, src, mainWick, err)) {
        g_error = err;
        std::fprintf(stderr, "wick: %s\n", err.c_str());
    } else {
        g_error.clear();
    }
}

static void drawErrorScreen() {
    lt_clear(0.25f, 0.05f, 0.07f);
    lt_print("WICK ERROR - FIX THE FILE AND SAVE TO RELOAD", 4, 4, 1, 0.8f,
             0.6f, 1);
    const int cols = 49;
    int y = 20;
    for (size_t i = 0; i < g_error.size() && y < lt_screen_h() - 10;
         i += cols) {
        lt_print(g_error.substr(i, cols).c_str(), 4, (float)y, 1, 1, 1, 1);
        y += 10;
    }
}

// ---- host state (shared by the desktop loop and the embedded stepper) ----

static VM* g_vm = nullptr;
static std::string g_mainWick;
static time_t g_mtime = 0;
static double g_nextCheck = 0.5;

bool wickHostInit(const std::string& gameDir) {
    g_dir = gameDir;
    g_vm = wick::create();
    std::string err;
    if (!registerLT(g_vm, err)) {
        std::fprintf(stderr, "wick natives: %s\n", err.c_str());
        wick::destroy(g_vm);
        g_vm = nullptr;
        return false;
    }
    g_mainWick = gameDir + "/main.wick";
    loadWickGame(g_vm, g_mainWick);
    g_mtime = fileMtime(g_mainWick);
    g_nextCheck = 0.5;
    return true;
}

void wickHostFrame() {
    if (!g_vm) return;
    std::string err;
    // hot-reload: poll mtime twice a second
    g_nextCheck -= lt_frame_dt();
    if (g_nextCheck <= 0) {
        g_nextCheck = 0.5;
        time_t m = fileMtime(g_mainWick);
        if (m != g_mtime) {
            g_mtime = m;
            std::fprintf(stderr, "lantern: reloading %s\n",
                         g_mainWick.c_str());
            loadWickGame(g_vm, g_mainWick);
        }
    }
    if (g_error.empty() &&
        !wick::call(g_vm, "update", lt_frame_dt(), true, err)) {
        g_error = err;
        std::fprintf(stderr, "wick: %s\n", err.c_str());
    }
    lt_frame_begin();
    if (g_error.empty() && !wick::call(g_vm, "draw", 0, false, err)) {
        g_error = err;
        std::fprintf(stderr, "wick: %s\n", err.c_str());
    }
    if (!g_error.empty()) drawErrorScreen();
    lt_frame_end();
    wick::collect(g_vm); // GC only ever runs here, between frames
}

void wickHostShutdown() {
    if (g_vm) wick::destroy(g_vm);
    g_vm = nullptr;
}

int runWickHost(const std::string& gameDir) {
    if (!lt_boot("lantern (wick)", 3)) return 1;
    if (!wickHostInit(gameDir)) return 1;
    while (lt_frame_poll()) wickHostFrame();
    int rc = g_error.empty() ? 0 : 1;
    wickHostShutdown();
    lt_shutdown();
    return rc;
}
