// wick_host.cpp — runs a wick game (main.wick) against the lantern C ABI.
// Mirrors the Lua host's dev loop exactly: hot-reload on save, in-engine
// error screen with file:line, LANTERN_SHOT handled by the engine. The
// lt.* API is registered with TYPED signatures, so a wrong argument is a
// compile error on screen, not a runtime surprise.
#include "wick_host.hpp"
#include "lantern.h"
#include "../wick/wick.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

using wick::Value;
using wick::VM;

static std::string g_dir;

// ---- natives: thin typed shims over lt_* ----------------------------------
#define N0(name, call) \
    static Value name(VM&, const Value*, int) { call; return Value::nil(); }
#define NARG(i) (float)a[i].d

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
    int id = lt_mesh_load_obj((g_dir + "/" + wick::getStr(a[0])).c_str());
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
    int w, h;
    int id = lt_texture_load((g_dir + "/" + wick::getStr(a[0])).c_str(), &w, &h);
    if (id < 0) wick::setError(vm, "load_texture failed: " + wick::getStr(a[0]));
    return Value::num(id);
}
static Value wSprite(VM&, const Value* a, int) { lt_sprite((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4)); return Value::nil(); }
static Value wSpriteEx(VM&, const Value* a, int) { lt_sprite_ex((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6), NARG(7), NARG(8), NARG(9)); return Value::nil(); }
static Value wSpriteUV(VM&, const Value* a, int) { lt_sprite_uv((int)a[0].d, NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6), NARG(7), NARG(8)); return Value::nil(); }
static Value wPrint(VM&, const Value* a, int) { lt_print(wick::getStr(a[0]).c_str(), NARG(1), NARG(2), NARG(3), NARG(4), NARG(5), NARG(6)); return Value::nil(); }
static Value wKey(VM&, const Value* a, int) { return Value::boolean(lt_input_down(wick::getStr(a[0]).c_str()) != 0); }
static Value wPressed(VM&, const Value* a, int) { return Value::boolean(lt_input_pressed(wick::getStr(a[0]).c_str()) != 0); }
static Value wGamepad(VM&, const Value*, int) { return Value::boolean(lt_gamepad_connected() != 0); }
static Value wRumble(VM&, const Value* a, int) { lt_rumble(NARG(0), NARG(1), (int)a[2].d); return Value::nil(); }
static Value wLoadSound(VM& vm, const Value* a, int) {
    int id = lt_sound_load((g_dir + "/" + wick::getStr(a[0])).c_str());
    if (id < 0) wick::setError(vm, "load_sound failed: " + wick::getStr(a[0]));
    return Value::num(id);
}
static Value wPlay(VM&, const Value* a, int) { return Value::num(lt_sound_play((int)a[0].d, NARG(1), a[2].d != 0)); }
static Value wStop(VM&, const Value* a, int) { lt_sound_stop((int)a[0].d); return Value::nil(); }
static Value wVolume(VM&, const Value* a, int) { lt_master_volume(NARG(0)); return Value::nil(); }
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
static Value wTouchX(VM&, const Value*, int) { return Value::num(lt_touch_x()); }
static Value wTouchY(VM&, const Value*, int) { return Value::num(lt_touch_y()); }
static Value wQuit(VM&, const Value*, int) { lt_quit(); return Value::nil(); }
static Value wEscQuits(VM&, const Value* a, int) { lt_escape_quits(a[0].b ? 1 : 0); return Value::nil(); }
static Value wTime(VM&, const Value*, int) { return Value::num(lt_time()); }
static Value wScreenshot(VM&, const Value* a, int) { lt_screenshot(wick::getStr(a[0]).c_str()); return Value::nil(); }

static bool registerLT(VM* vm, std::string& err) {
    struct Reg { const char* sig; wick::NativeFn fn; };
    static const Reg regs[] = {
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
        {"print(str, num, num, num=1, num=1, num=1, num=1)", wPrint},
        {"key(str): bool", wKey},
        {"pressed(str): bool", wPressed},
        {"gamepad(): bool", wGamepad},
        {"touch_down(): bool", wTouchDown},
        {"touch_pressed(): bool", wTouchPressed},
        {"touch_x(): num", wTouchX},
        {"touch_y(): num", wTouchY},
        {"rumble(num, num, num)", wRumble},
        {"load_sound(str): num", wLoadSound},
        {"play(num, num=1, num=0): num", wPlay},
        {"stop(num)", wStop},
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
    return true;
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
