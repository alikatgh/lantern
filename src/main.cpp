// lantern Lua host — a client of the C ABI (include/lantern.h), nothing more.
// Usage: lantern [game_dir | game.lant]
//   game_dir contains main.wick or main.lua (default games/demo);
//   a .lant package extracts to a temp dir and runs from there.
// LANTERN_SHOT=/path/prefix → run 60 frames, save <prefix>.bmp of the
// 400×240 FBO, exit 0. Verify by frames, never by "it compiled".
#include "lantern.h"
#include "package.hpp"
#include "path_sandbox.hpp"
#include "wick_host.hpp"
#include "../wick/wick.hpp"
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

static std::string g_gameDir;

// Resolve a game-relative path or raise a Lua error.
static std::string resolveGamePath(lua_State* L, const char* rel) {
    std::string out, err;
    if (!lt::pathResolveUnder(g_gameDir, rel ? rel : "", out, err))
        luaL_error(L, "path refused: %s", err.c_str());
    return out;
}

// ---------------- Lua bindings (lt.*) — thin shims over lt_* ----------------

static int l_clear(lua_State* L) {
    lt_clear((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
             (float)luaL_checknumber(L, 3));
    return 0;
}

static int l_camera(lua_State* L) {
    lt_camera((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
              (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
              (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6),
              (float)luaL_optnumber(L, 7, 55.0));
    return 0;
}

static int l_light(lua_State* L) {
    lt_light((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
             (float)luaL_checknumber(L, 3),
             (float)luaL_optnumber(L, 4, 0.35));
    return 0;
}

static int l_point_light(lua_State* L) {
    lt_point_light((int)luaL_checkinteger(L, 1),
                   (float)luaL_checknumber(L, 2),
                   (float)luaL_checknumber(L, 3),
                   (float)luaL_checknumber(L, 4),
                   (float)luaL_checknumber(L, 5),
                   (float)luaL_optnumber(L, 6, 1.0),
                   (float)luaL_optnumber(L, 7, 1.0),
                   (float)luaL_optnumber(L, 8, 1.0));
    return 0;
}

static int l_fog(lua_State* L) {
    lt_fog((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
           (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
           (float)luaL_checknumber(L, 5));
    return 0;
}

static int l_cube(lua_State* L) {
    lua_pushinteger(L, lt_mesh_cube());
    return 1;
}
static int l_plane(lua_State* L) {
    lua_pushinteger(L, lt_mesh_plane((int)luaL_optinteger(L, 1, 1)));
    return 1;
}
static int l_sphere(lua_State* L) {
    lua_pushinteger(L, lt_mesh_sphere((int)luaL_optinteger(L, 1, 12)));
    return 1;
}
static int l_cylinder(lua_State* L) {
    lua_pushinteger(L, lt_mesh_cylinder((int)luaL_optinteger(L, 1, 16)));
    return 1;
}
static int l_cone(lua_State* L) {
    lua_pushinteger(L, lt_mesh_cone((int)luaL_optinteger(L, 1, 16)));
    return 1;
}

// lt.mesh{ x,y,z, nx,ny,nz, u,v, r,g,b,a, ... } — 12 floats per vertex.
static int l_mesh(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_Integer n = luaL_len(L, 1);
    if (n == 0 || n % 12 != 0)
        return luaL_error(L, "lt.mesh: table length must be a multiple of 12 "
                             "(pos3 normal3 uv2 rgba4 per vertex), got %d",
                          (int)n);
    std::vector<float> v((size_t)n);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_geti(L, 1, i);
        v[(size_t)i - 1] = (float)luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    lua_pushinteger(L, lt_mesh_create(v.data(), (int)(n / 12)));
    return 1;
}

static int l_load_mesh(lua_State* L) {
    std::string p = resolveGamePath(L, luaL_checkstring(L, 1));
    int id = lt_mesh_load_obj(p.c_str());
    if (id < 0) return luaL_error(L, "load_mesh failed: %s", p.c_str());
    lua_pushinteger(L, id);
    return 1;
}

static int l_draw_lerp(lua_State* L) {
    lt_draw_mesh_lerp(
        (int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
        (float)luaL_checknumber(L, 3), (int)luaL_optinteger(L, 16, -1),
        (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
        (float)luaL_checknumber(L, 6), (float)luaL_optnumber(L, 7, 0),
        (float)luaL_optnumber(L, 8, 0), (float)luaL_optnumber(L, 9, 0),
        (float)luaL_optnumber(L, 10, 1), (float)luaL_optnumber(L, 11, 1),
        (float)luaL_optnumber(L, 12, 1), (float)luaL_optnumber(L, 13, 1),
        (float)luaL_optnumber(L, 14, 1), (float)luaL_optnumber(L, 15, 1));
    return 0;
}

static int l_shadow(lua_State* L) {
    lt_shadow((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
              (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
              (float)luaL_optnumber(L, 5, 0.35));
    return 0;
}

static int l_billboard(lua_State* L) {
    lt_billboard((int)luaL_checkinteger(L, 1), (float)luaL_checknumber(L, 2),
                 (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                 (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6),
                 (float)luaL_optnumber(L, 7, 0), (float)luaL_optnumber(L, 8, 0),
                 (float)luaL_optnumber(L, 9, 1),
                 (float)luaL_optnumber(L, 10, 1));
    return 0;
}

static int l_draw(lua_State* L) {
    lt_draw_mesh((int)luaL_checkinteger(L, 1),
                 (int)luaL_optinteger(L, 14, -1), // optional texture handle
                 (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                 (float)luaL_checknumber(L, 4), (float)luaL_optnumber(L, 5, 0),
                 (float)luaL_optnumber(L, 6, 0), (float)luaL_optnumber(L, 7, 0),
                 (float)luaL_optnumber(L, 8, 1), (float)luaL_optnumber(L, 9, 1),
                 (float)luaL_optnumber(L, 10, 1),
                 (float)luaL_optnumber(L, 11, 1),
                 (float)luaL_optnumber(L, 12, 1),
                 (float)luaL_optnumber(L, 13, 1));
    return 0;
}

static int l_rect(lua_State* L) {
    lt_rect((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
            (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
            (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6),
            (float)luaL_checknumber(L, 7), (float)luaL_optnumber(L, 8, 1.0));
    return 0;
}

static int l_load_texture(lua_State* L) {
    std::string p = resolveGamePath(L, luaL_checkstring(L, 1));
    int w = 0, h = 0;
    int id = lt_texture_load(p.c_str(), &w, &h);
    if (id < 0) return luaL_error(L, "load_texture failed: %s", p.c_str());
    lua_pushinteger(L, id);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 3;
}

static int l_sprite(lua_State* L) {
    lt_sprite((int)luaL_checkinteger(L, 1), (float)luaL_checknumber(L, 2),
              (float)luaL_checknumber(L, 3), (float)luaL_optnumber(L, 4, 1.0),
              (float)luaL_optnumber(L, 5, 1.0));
    return 0;
}

static int l_sprite_uv(lua_State* L) {
    lt_sprite_uv((int)luaL_checkinteger(L, 1), (float)luaL_checknumber(L, 2),
                 (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                 (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6),
                 (float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 8),
                 (float)luaL_checknumber(L, 9));
    return 0;
}

static int l_print(lua_State* L) {
    lt_print(luaL_checkstring(L, 1), (float)luaL_checknumber(L, 2),
             (float)luaL_checknumber(L, 3), (float)luaL_optnumber(L, 4, 1.0),
             (float)luaL_optnumber(L, 5, 1.0), (float)luaL_optnumber(L, 6, 1.0),
             (float)luaL_optnumber(L, 7, 1.0));
    return 0;
}

static int l_sprite_ex(lua_State* L) {
    lt_sprite_ex((int)luaL_checkinteger(L, 1), (float)luaL_checknumber(L, 2),
                 (float)luaL_checknumber(L, 3),
                 (float)luaL_optnumber(L, 4, 1.0),
                 (float)luaL_optnumber(L, 5, 1.0),
                 (float)luaL_optnumber(L, 6, 0.0),
                 (float)luaL_optnumber(L, 7, 1.0),
                 (float)luaL_optnumber(L, 8, 1.0),
                 (float)luaL_optnumber(L, 9, 1.0),
                 (float)luaL_optnumber(L, 10, 1.0));
    return 0;
}

static int l_key(lua_State* L) {
    lua_pushboolean(L, lt_input_down(luaL_checkstring(L, 1)));
    return 1;
}

static int l_pressed(lua_State* L) {
    lua_pushboolean(L, lt_input_pressed(luaL_checkstring(L, 1)));
    return 1;
}

static int l_load_sound(lua_State* L) {
    std::string p = resolveGamePath(L, luaL_checkstring(L, 1));
    int id = lt_sound_load(p.c_str());
    if (id < 0) return luaL_error(L, "load_sound failed: %s", p.c_str());
    lua_pushinteger(L, id);
    return 1;
}

static int l_play(lua_State* L) {
    lua_pushinteger(L, lt_sound_play((int)luaL_checkinteger(L, 1),
                                     (float)luaL_optnumber(L, 2, 1.0),
                                     lua_toboolean(L, 3)));
    return 1;
}

static int l_stop(lua_State* L) {
    lt_sound_stop((int)luaL_checkinteger(L, 1));
    return 0;
}

static int l_volume(lua_State* L) {
    lt_master_volume((float)luaL_checknumber(L, 1));
    return 0;
}

static int l_screenshot(lua_State* L) {
    std::string out, err;
    if (!lt::pathResolveScreenshot(g_gameDir, luaL_checkstring(L, 1), out,
                                   err))
        return luaL_error(L, "screenshot refused: %s", err.c_str());
    lt_screenshot(out.c_str());
    return 0;
}

static int l_save(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    lua_pushboolean(L, lt_save_write(luaL_checkstring(L, 1), data, (int)len));
    return 1;
}

static int l_load_save(lua_State* L) {
    // saves are small (state blobs, not media); 1 MiB is generous
    std::vector<char> buf(1 << 20);
    int got = lt_save_read(luaL_checkstring(L, 1), buf.data(),
                           (int)buf.size());
    if (got < 0)
        lua_pushnil(L);
    else
        lua_pushlstring(L, buf.data(), (size_t)got);
    return 1;
}

static int l_gamepad(lua_State* L) {
    lua_pushboolean(L, lt_gamepad_connected());
    return 1;
}

static int l_quit(lua_State*) {
    lt_quit();
    return 0;
}

static int l_escape_quits(lua_State* L) {
    lt_escape_quits(lua_toboolean(L, 1));
    return 0;
}

static int l_rumble(lua_State* L) {
    lt_rumble((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
              (int)luaL_checkinteger(L, 3));
    return 0;
}

static int l_time(lua_State* L) {
    lua_pushnumber(L, lt_time());
    return 1;
}

static int l_touch_down(lua_State* L) {
    lua_pushboolean(L, lt_touch_down());
    return 1;
}

static int l_touch_pressed(lua_State* L) {
    lua_pushboolean(L, lt_touch_pressed());
    return 1;
}

// lt.touch() → x, y (400x240 screen coords, last touched position)
static int l_touch(lua_State* L) {
    lua_pushnumber(L, lt_touch_x());
    lua_pushnumber(L, lt_touch_y());
    return 2;
}

static void registerAPI(lua_State* L) {
    static const luaL_Reg fns[] = {
        {"clear", l_clear},         {"camera", l_camera},
        {"light", l_light},         {"point_light", l_point_light},
        {"fog", l_fog},             {"cube", l_cube},
        {"plane", l_plane},         {"sphere", l_sphere},
        {"cylinder", l_cylinder},   {"cone", l_cone},
        {"mesh", l_mesh},           {"load_mesh", l_load_mesh},
        {"draw", l_draw},           {"draw_lerp", l_draw_lerp},
        {"shadow", l_shadow},       {"billboard", l_billboard},
        {"rect", l_rect},           {"load_texture", l_load_texture},
        {"sprite", l_sprite},       {"sprite_ex", l_sprite_ex},
        {"sprite_uv", l_sprite_uv}, {"print", l_print},
        {"key", l_key},             {"pressed", l_pressed},
        {"touch", l_touch},         {"touch_down", l_touch_down},
        {"touch_pressed", l_touch_pressed},
        {"gamepad", l_gamepad},     {"quit", l_quit},
        {"escape_quits", l_escape_quits},
        {"rumble", l_rumble},
        {"load_sound", l_load_sound},
        {"play", l_play},           {"stop", l_stop},
        {"volume", l_volume},       {"screenshot", l_screenshot},
        {"save", l_save},           {"load_save", l_load_save},
        {"time", l_time},
        {nullptr, nullptr}};
    luaL_newlib(L, fns);
    lua_pushinteger(L, lt_screen_w());
    lua_setfield(L, -2, "W");
    lua_pushinteger(L, lt_screen_h());
    lua_setfield(L, -2, "H");
    lua_setglobal(L, "lt");
}

// Dev-loop state: a Lua error doesn't kill the engine — it shows an error
// screen and keeps watching main.lua; saving a fixed file hot-reloads it.
static std::string g_error;

static bool callGlobal(lua_State* L, const char* name, double arg,
                       bool hasArg) {
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return true; // optional callback
    }
    if (hasArg) lua_pushnumber(L, arg);
    if (lua_pcall(L, hasArg ? 1 : 0, 0, 0) != LUA_OK) {
        g_error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        std::fprintf(stderr, "lua error in %s: %s\n", name, g_error.c_str());
        lua_pop(L, 1);
        return false;
    }
    return true;
}

static time_t fileMtime(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
}

// Re-run main.lua in the SAME Lua state (globals persist). All engine-side
// meshes/textures/sounds from the previous generation are freed first via
// lt_resources_reset() — the re-run recreates what it needs, so hot-reload
// sessions don't grow without bound.
static void loadGame(lua_State* L, const std::string& mainLua) {
    lt_resources_reset();
    if (luaL_dofile(L, mainLua.c_str()) != LUA_OK) {
        g_error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error";
        std::fprintf(stderr, "lua: %s\n", g_error.c_str());
        lua_pop(L, 1);
    } else {
        g_error.clear();
    }
}

static void drawErrorScreen() {
    lt_clear(0.25f, 0.05f, 0.07f);
    lt_print("LUA ERROR - FIX THE FILE AND SAVE TO RELOAD", 4, 4, 1, 0.8f,
             0.6f, 1);
    // wrap the message to 49 columns of the 8px font
    const int cols = 49;
    int y = 20;
    for (size_t i = 0; i < g_error.size() && y < lt_screen_h() - 10;
         i += cols) {
        lt_print(g_error.substr(i, cols).c_str(), 4, (float)y, 1, 1, 1, 1);
        y += 10;
    }
}

// Check is deliberately dispatched before package extraction and platform boot.
// It opens one source descriptor without following a final symlink and never
// asks the game host to load, draw, save, or initialize resources.
static std::string checkJsonString(const std::string& text) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            out += '\\'; out += (char)c; ++i;
        } else if (c < 0x20) {
            out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; ++i;
        } else if (c < 0x80) {
            out += (char)c; ++i;
        } else {
            // Keep valid UTF-8 intact, but make invalid filesystem/message
            // bytes representable too rather than emitting malformed JSON.
            size_t n = c >= 0xc2 && c <= 0xdf ? 2 :
                       c >= 0xe0 && c <= 0xef ? 3 :
                       c >= 0xf0 && c <= 0xf4 ? 4 : 0;
            bool valid = n && i + n <= text.size();
            for (size_t j = 1; valid && j < n; ++j) {
                unsigned char d = (unsigned char)text[i + j];
                valid = d >= 0x80 && d <= 0xbf;
                if (j == 1 && ((c == 0xe0 && d < 0xa0) ||
                    (c == 0xed && d > 0x9f) || (c == 0xf0 && d < 0x90) ||
                    (c == 0xf4 && d > 0x8f))) valid = false;
            }
            if (valid) { out.append(text, i, n); i += n; }
            else {
                out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; ++i;
            }
        }
    }
    out += '"';
    return out;
}

static int checkResult(bool ok, const std::string& source,
                       const wick::Diagnostic& diagnostic, int exitCode) {
    auto nullable = [](const std::string& value) {
        return value.empty() ? std::string("null") : checkJsonString(value);
    };
    std::string json = "{\"schema\":\"lantern.check.v1\",\"ok\":";
    json += ok ? "true" : "false";
    json += ",\"language\":\"wick\",\"source\":" + nullable(source);
    json += ",\"diagnostics\":[";
    if (!ok) {
        json += "{\"severity\":\"error\",\"phase\":" + checkJsonString(diagnostic.phase);
        json += ",\"code\":" + checkJsonString(diagnostic.code);
        json += ",\"file\":" + nullable(diagnostic.file);
        json += ",\"line\":" + (diagnostic.line > 0 ? std::to_string(diagnostic.line) : "null");
        json += ",\"message\":" + checkJsonString(diagnostic.message) + "}";
    }
    json += "]}\n";
    std::fwrite(json.data(), 1, json.size(), stdout);
    return exitCode;
}

static int checkCLI(int argc, char** argv) {
    std::string sourcePath;
    auto failure = [&](const char* phase, const char* code,
                       const std::string& message) {
        return checkResult(false, sourcePath,
                           {sourcePath, 0, message, phase, code}, 2);
    };
    if (argc != 4 || std::string(argv[1]) != "check" ||
        std::string(argv[2]) != "--json")
        return failure("usage", "LANTERN_CHECK_USAGE",
                       "usage: lantern check --json <directory|source.wick>");
    try {
        namespace fs = std::filesystem;
        fs::path requested(argv[3]);
        if (requested.empty())
            return failure("usage", "LANTERN_CHECK_USAGE", "source path is empty");
        std::error_code ec;
        if (fs::is_directory(requested, ec)) requested /= "main.wick";
        if (requested.extension() != ".wick")
            return failure("io", "LANTERN_CHECK_SOURCE", "source must be a .wick file or a directory containing main.wick");
        // Resolve ancestors, including macOS /tmp -> /private/tmp, but leave
        // the final component for O_NOFOLLOW on the descriptor we read.
        fs::path parent = requested.parent_path();
        if (parent.empty()) parent = ".";
        parent = fs::canonical(parent, ec);
        if (ec)
            return failure("io", "LANTERN_CHECK_IO", "cannot resolve source directory: " + ec.message());
        sourcePath = (parent / requested.filename()).string();
        int fd = open(sourcePath.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            return failure("io", errno == ELOOP ? "LANTERN_CHECK_SOURCE" : "LANTERN_CHECK_IO",
                           "cannot open source: " + std::string(std::strerror(errno)));
        struct Descriptor {
            int fd;
            ~Descriptor() { close(fd); }
        } descriptor{fd};
        struct stat st{};
        if (fstat(fd, &st) != 0)
            return failure("io", "LANTERN_CHECK_IO", "cannot inspect source descriptor");
        if (!S_ISREG(st.st_mode))
            return failure("io", "LANTERN_CHECK_SOURCE", "source must be a regular file");
        if (st.st_size > (off_t)wick::SOURCE_LIMIT_BYTES)
            return failure("io", "WICK_SOURCE_LIMIT", "source exceeds 8388608 bytes");
        std::string source;
        char buffer[8192];
        while (source.size() <= wick::SOURCE_LIMIT_BYTES) {
            size_t remaining = wick::SOURCE_LIMIT_BYTES + 1 - source.size();
            ssize_t count = read(fd, buffer, std::min(sizeof buffer, remaining));
            if (count < 0) {
                if (errno == EINTR) continue;
                return failure("io", "LANTERN_CHECK_IO", "cannot read source: " + std::string(std::strerror(errno)));
            }
            if (!count) break;
            source.append(buffer, (size_t)count);
        }
        if (source.size() > wick::SOURCE_LIMIT_BYTES)
            return failure("io", "WICK_SOURCE_LIMIT", "source exceeds 8388608 bytes");
        wick::Diagnostic diagnostic;
        bool ok = checkWickSource(source, sourcePath, diagnostic);
        int exitCode = ok ? 0 : diagnostic.phase == "compile" ? 1 : 2;
        return checkResult(ok, sourcePath, diagnostic, exitCode);
    } catch (const std::exception& error) {
        return failure("host", "LANTERN_CHECK_HOST", error.what());
    }
}

static int buildInfoCLI(int argc, char** argv) {
    // This path does not inspect games, extract packages or initialize SDL/Lua.
    if (argc != 3 || std::string(argv[2]) != "--json") {
        std::fputs("{\"schema\":\"lantern.build-info.v1\",\"ok\":false,"
                   "\"diagnostics\":[{\"severity\":\"error\",\"phase\":\"usage\","
                   "\"code\":\"LANTERN_BUILD_INFO_USAGE\","
                   "\"message\":\"usage: lantern --build-info --json\"}]}\n", stdout);
        return 2;
    }
#if defined(__aarch64__) || defined(_M_ARM64)
    constexpr const char* architecture = "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
    constexpr const char* architecture = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    constexpr const char* architecture = "x86";
#elif defined(__arm__) || defined(_M_ARM)
    constexpr const char* architecture = "arm";
#else
    constexpr const char* architecture = "unknown";
#endif
    std::printf("{\"schema\":\"lantern.build-info.v1\",\"ok\":true,"
                "\"host_bits\":%zu,\"target_bits\":%u,\"architecture\":\"%s\","
                "\"num\":{\"storage_bits\":%zu,\"mantissa_bits\":%d,\"radix\":%d},"
                "\"diagnostics\":[]}\n",
                sizeof(void*) * CHAR_BIT, wick::HOST_BITS, architecture,
                sizeof(double) * CHAR_BIT, std::numeric_limits<double>::digits,
                std::numeric_limits<double>::radix);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--build-info")
        return buildInfoCLI(argc, argv);
    if (argc > 1 && (std::string(argv[1]) == "check" ||
                     std::string(argv[1]) == "--check"))
        return checkCLI(argc, argv);
    g_gameDir = argc > 1 ? argv[1] : "games/demo";

    // a .lant package: verify, extract to a fresh temp dir, run from there
    if (g_gameDir.size() > 5 &&
        g_gameDir.compare(g_gameDir.size() - 5, 5, ".lant") == 0) {
        const char* tenv = std::getenv("TMPDIR");
        std::string tmpl = std::string(tenv ? tenv : "/tmp");
        if (tmpl.empty() || tmpl.back() != '/') tmpl += '/';
        tmpl += "lantern-XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        if (!mkdtemp(buf.data())) {
            std::fprintf(stderr, "lantern: cannot create temp dir\n");
            return 1;
        }
        std::string err;
        if (!lt::pkgExtract(g_gameDir, buf.data(), err)) {
            std::fprintf(stderr, "lantern: %s\n", err.c_str());
            return 1;
        }
        g_gameDir = buf.data();
        lt::pathSetPackageMode(true);
        lt::pathSetScreenshotDir(g_gameDir); // screenshots stay in extract dir
    }

    // a game written in wick takes precedence over Lua
    struct stat st{};
    if (stat((g_gameDir + "/main.wick").c_str(), &st) == 0)
        return runWickHost(g_gameDir);

    if (!lt_boot("lantern", 3)) return 1;

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    registerAPI(L);
    std::string mainLua = g_gameDir + "/main.lua";
    loadGame(L, mainLua);
    time_t mtime = fileMtime(mainLua);
    double nextCheck = 0.5;

    while (lt_frame_poll()) {
        // hot-reload: poll mtime twice a second
        nextCheck -= lt_frame_dt();
        if (nextCheck <= 0) {
            nextCheck = 0.5;
            time_t m = fileMtime(mainLua);
            if (m != mtime) {
                mtime = m;
                std::fprintf(stderr, "lantern: reloading %s\n",
                             mainLua.c_str());
                loadGame(L, mainLua);
            }
        }
        if (g_error.empty()) callGlobal(L, "update", lt_frame_dt(), true);
        lt_frame_begin();
        if (g_error.empty())
            callGlobal(L, "draw", 0, false);
        if (!g_error.empty()) // update/draw may have just failed
            drawErrorScreen();
        lt_frame_end(); // 2D flush + present; LANTERN_SHOT is engine-owned
    }

    lua_close(L);
    lt_shutdown();
    return g_error.empty() ? 0 : 1;
}
