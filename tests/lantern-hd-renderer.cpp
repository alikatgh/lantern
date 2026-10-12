// Independent marker checks for logical-to-physical raster density. This same
// fixture links the untouched reference core for scale-1 byte parity.
#include "lantern.h"
#include "gfx.hpp"
#include "platform.hpp"
#include "render_target.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int assertions = 0, initCalls = 0, presents = 0;
int presentedW = 0, presentedH = 0, presentedPitch = 0;
std::array<uint8_t, 4> presentedLast{};
void expect(bool yes, const std::string& message) {
    ++assertions;
    if (!yes) throw std::runtime_error(message);
}
using RGB = std::array<uint8_t, 3>;
constexpr RGB black{0, 0, 0}, red{255, 0, 0}, green{0, 255, 0},
              blue{0, 0, 255}, yellow{255, 255, 0}, white{255, 255, 255};
void writeBMP(const std::filesystem::path& path, int w, int h,
              const std::vector<RGB>& pixels) {
    const int pitch = (w * 3 + 3) & ~3;
    std::vector<uint8_t> data(54 + pitch * h, 0);
    auto put = [&](int off, uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) data[off + i] = uint8_t(value >> (i * 8));
    };
    data[0] = 'B'; data[1] = 'M';
    put(2, uint32_t(data.size()), 4); put(10, 54, 4); put(14, 40, 4);
    put(18, w, 4); put(22, h, 4); put(26, 1, 2); put(28, 24, 2);
    put(34, pitch * h, 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const RGB& c = pixels[y * w + x];
            size_t i = 54 + (h - 1 - y) * pitch + x * 3;
            data[i] = c[2]; data[i + 1] = c[1]; data[i + 2] = c[0];
        }
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    expect(bool(f), "write independent BMP marker input");
}
void makeInputs(const std::filesystem::path& dir) {
    std::vector<RGB> atlas(16 * 8, green), checker(8 * 8);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            atlas[y * 16 + x] = x < 4 && y < 4 ? blue :
                                x >= 4 && y >= 4 ? yellow : red;
            checker[y * 8 + x] = (x + y) % 2 ? white : black;
        }
    writeBMP(dir / "marker.bmp", 16, 8, atlas);
    writeBMP(dir / "checker.bmp", 8, 8, checker);
}
void fixture(const std::filesystem::path& dir) {
    lt_frame_begin();
    lt_clear(0.07f, 0.12f, 0.18f);
    lt_camera(0, 0, 3, 0, 0, 0, 60);
    lt_light(0.4f, -1, -0.3f, 0.5f);
    const int cube = lt_mesh_cube();
    lt_draw_mesh(cube, -1, -0.8f, 0, 0, 0.2f, 0.4f, 0.1f,
                 1, 1, 1, 0.8f, 0.4f, 0.2f);
    int w = 0, h = 0;
    const int tex = lt_texture_load((dir / "marker.bmp").c_str(), &w, &h);
    expect(tex >= 0 && w == 16 && h == 8, "fixture texture loads");
    lt_rect(-2.5f, 4.25f, 23, 12, 0.7f, 0.2f, 0.5f, 0.5f);
    lt_rect(399, 239, 2, 2, 1, 0, 0, 1);
    lt_sprite_uv(tex, 30, 30, 8, 8, 0, 0, 0.5f, 1);
    lt_sprite_uv(tex, 60, 30, -8, 8, 0, 0, 0.5f, 1);
    lt_sprite_ex(tex, 100, 50, 1, 1, 1.5707963267948966f, 1, 1, 1, 1);
    lt_sprite_ex(tex, 130, 50, -1, 1, 0.31f, 0.8f, 0.6f, 1, 0.7f);
    lt_print("DENSITY 1: ABI + UV + ROTATION", 5, 210, 1, 1, 1, 1);
    lt_frame_end();
}
#ifndef NYURGUN_HD_BASELINE
void pixel(const lt::Gfx& gfx, int x, int y, RGB expected,
           const std::string& message, int tolerance = 1) {
    expect(x >= 0 && y >= 0 && x < gfx.framebufferWidth() &&
           y < gfx.framebufferHeight(), "pixel probe is inside physical target");
    const uint8_t* p = gfx.framebuffer() + y * gfx.framebufferPitch() + x * 4;
    if (std::abs(int(p[0]) - expected[0]) > tolerance ||
        std::abs(int(p[1]) - expected[1]) > tolerance ||
        std::abs(int(p[2]) - expected[2]) > tolerance)
        throw std::runtime_error(message + " at " + std::to_string(x) + "," +
            std::to_string(y) + " got " + std::to_string(p[0]) + "," +
            std::to_string(p[1]) + "," + std::to_string(p[2]));
    ++assertions;
}
void metadata(const lt::Gfx& g, int s) {
    expect(g.renderScale() == s, "requested raster density retained");
    expect(g.framebufferWidth() == 400 * s && g.framebufferHeight() == 240 * s,
           "physical dimensions scale but logical aspect remains fixed");
    expect(g.framebufferPitch() == 400 * s * 4, "physical RGBA pitch");
    expect(g.framebufferSize() == size_t(400 * s) * (240 * s) * 4,
           "frame allocation is exact and bounded");
}
void scale2(const std::filesystem::path& dir) {
    lt::Gfx g;
    expect(g.init(2), "scale2 initializes"); metadata(g, 2);
    g.beginFrame(); g.clear(0, 0, 0);
    g.rect(-1, -1, 2, 2, 0, 1, 0, 1);
    g.rect(399, 239, 2, 2, 1, 0, 0, 1);
    g.rect(399.5f, 239.5f, 1, 1, 0, 0, 1, 1);
    g.rect(10, 10, 6, 6, 1, 1, 1, 0.5f);
    const int tex = g.loadTexture((dir / "marker.bmp").string());
    const int check = g.loadTexture((dir / "checker.bmp").string());
    expect(tex >= 0 && check >= 0, "scale2 textures load");
    g.spriteUV(tex, 30, 30, 8, 8, 0, 0, 0.5f, 1);
    g.spriteUV(tex, 60, 30, -8, 8, 0, 0, 0.5f, 1);
    g.spriteEx(tex, 100, 50, 1, 1, 1.5707963267948966f, 1, 1, 1, 1);
    g.spriteEx(tex, 130, 50, -1, 1, 0, 1, 1, 1, 1);
    g.spriteUV(check, 180, 30, 4, 4, 0, 0, 1, 1);
    g.endFrame();
    pixel(g, 0, 0, green, "top-left clip"); pixel(g, 1, 1, green, "logical unit spans two pixels");
    pixel(g, 2, 0, black, "left clip ends at physical boundary");
    pixel(g, 799, 0, black, "first row right edge does not wrap");
    pixel(g, 0, 479, black, "last row left edge does not wrap");
    pixel(g, 798, 478, red, "bottom-right raster coverage");
    pixel(g, 799, 478, red, "last column uses physical stride");
    pixel(g, 798, 479, red, "last row uses physical stride");
    pixel(g, 799, 479, blue, "subpixel edge resolves at HD density");
    pixel(g, 797, 479, black, "right clipping does not expand logical geometry");
    for (int y = 20; y < 32; ++y)
        for (int x = 20; x < 32; ++x)
            pixel(g, x, y, RGB{127, 127, 127}, "translucent shared edges blend exactly once");
    pixel(g, 62, 62, blue, "normal UV asymmetric top-left marker");
    pixel(g, 74, 74, yellow, "normal UV asymmetric lower-right marker");
    pixel(g, 75, 62, red, "bilinear taps stay inside first atlas cell");
    pixel(g, 75, 74, yellow, "atlas corner avoids adjacent green cell");
    pixel(g, 106, 62, red, "negative width flips top-right to the left");
    pixel(g, 116, 62, blue, "negative width flips top-left to the right");
    pixel(g, 106, 74, yellow, "negative width preserves vertical orientation");
    pixel(g, 204, 88, blue, "90degree rotation preserves asymmetric source marker");
    pixel(g, 204, 112, green, "90degree rotation preserves opposite atlas cell");
    pixel(g, 188, 100, black, "rotated quad bounds stay logical");
    pixel(g, 248, 98, green, "sprite_ex negative scale flips the full texture");
    pixel(g, 272, 94, blue, "sprite_ex flip retains asymmetric marker");
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            pixel(g, 360 + x, 60 + y, (x + y) % 2 ? white : black,
                  "HD resolves all64 source texels rather than upscaling a4x4 result", 0);
    g.screenshot((dir / "scale2.bmp").string());
    g.reset(); metadata(g, 2);
    g.beginFrame(); g.clear(0, 0, 0);
    g.rect(399, 239, 1, 1, 1, 1, 0, 1); g.endFrame();
    pixel(g, 799, 479, yellow, "resource reset retains HD target and builtins");
}
void depth2() {
    lt::Gfx g; expect(g.init(2), "3D scale2 initializes");
    g.beginFrame(); g.clear(0, 0, 0);
    g.setCamera({0, 0, 2}, {0, 0, 0}, 90);
    g.setLight({0, 0, -1}, 1);
    const float positions[6][2] = {{-1, -.5f}, {1, -.5f}, {1, .5f},
                                  {-1, -.5f}, {1, .5f}, {-1, .5f}};
    float v[6 * lt::VERT_FLOATS];
    for (int i = 0; i < 6; ++i) {
        float* o = v + i * lt::VERT_FLOATS;
        const float row[12] = {positions[i][0], positions[i][1], 0, 0, 0, 1,
                               0, 0, 1, 1, 1, 1};
        for (int k = 0; k < 12; ++k) o[k] = row[k];
    }
    const int mesh = g.makeMesh(v, 6);
    g.drawMesh(mesh, -1, lt::Mat4{}, 1, 0, 0);
    g.drawMesh(mesh, -1, lt::translate(0, 0, -1), 0, 0, 1);
    g.endFrame();
    pixel(g, 400, 240, red, "3D physical center and depth rejection");
    pixel(g, 290, 240, red, "3D viewport is800wide, not400");
    pixel(g, 279, 240, black, "3D logical left bound retains projection");
    pixel(g, 520, 240, black, "3D logical right bound retains projection");
    pixel(g, 400, 179, black, "3D vertical upper projection bound");
    pixel(g, 400, 300, black, "3D vertical lower projection bound");
    // A larger near rectangle covers the target's first and last rows/columns;
    // the far blue pass must remain rejected at those physical depth strides.
    g.beginFrame(); g.clear(0, 0, 0);
    g.drawMesh(mesh, -1, lt::scale(5, 10, 1), 1, 0, 0);
    g.drawMesh(mesh, -1, lt::mul(lt::translate(0, 0, -1), lt::scale(8, 16, 1)), 0, 0, 1);
    g.endFrame();
    pixel(g, 0, 0, red, "3D first physical depth index");
    pixel(g, 799, 0, red, "3D last column depth stride");
    pixel(g, 0, 479, red, "3D last row depth stride");
    pixel(g, 799, 479, red, "3D last physical depth index");
}
void boundedInitialization(const std::filesystem::path& dir) {
    lt::Gfx invalid;
    expect(!invalid.init(0) && !invalid.init(11) && invalid.framebufferSize() == 0,
           "direct invalid densities allocate no frame");
    for (const char* bad : {"", "0", "11", "-1", "+2", " 2", "2 ", "2x", "2.0", "02", "22", "99999999999999999999"}) {
        setenv("LANTERN_RENDER_SCALE", bad, 1);
        const int before = initCalls;
        expect(!lt_boot("invalid", 3), "malformed density rejects boot");
        expect(initCalls == before, "malformed density fails before platform initialization");
        expect(!lt_frame_poll(), "invalid boot cannot enter frame loop");
    }
    unsetenv("LANTERN_RENDER_SCALE");
    expect(lt_boot("default", 3), "unset setting defaults to valid boot");
    lt_frame_begin(); lt_clear(0, 0, 0); lt_frame_end();
    expect(presentedW == 400 && presentedH == 240 && presentedPitch == 1600,
           "unset setting retains legacy physical target");
    lt_shutdown();
    for (int s = 1; s <= 10; ++s) {
        const std::string value = std::to_string(s);
        setenv("LANTERN_RENDER_SCALE", value.c_str(), 1);
        expect(lt_boot("valid", 3), "each density1..10 initializes");
        expect(lt_screen_w() == 400 && lt_screen_h() == 240, "public dimensions remain logical");
        expect(lt_touch_x() == 200 && lt_touch_y() == 120, "input coordinates remain logical");
        lt_frame_begin(); lt_clear(0, 0, 0);
        lt_rect(399, 239, 1, 1, 1, 0, 0, 1); lt_frame_end();
        expect(presentedW == 400 * s && presentedH == 240 * s &&
               presentedPitch == 1600 * s, "engine passes physical presentation metadata");
        expect(presentedLast == std::array<uint8_t, 4>{255, 0, 0, 255},
               "presentation metadata indexes the actual last pixel");
        if (s > 2 && s <= 4) lt_screenshot((dir / ("scale" + value + ".bmp")).c_str());
        setenv("LANTERN_RENDER_SCALE", s == 1 ? "4" : "1", 1);
        lt_frame_begin(); lt_clear(0, 0, 0); lt_frame_end();
        expect(presentedW == 400 * s, "density is initialization-only, not mutable per frame");
        lt_shutdown();
    }
    unsetenv("LANTERN_RENDER_SCALE");
}
#endif
} // namespace

// Read-only marker backend: the engine/rasterizer are real, presentation/input
// are deterministic. The reference and overlay differ only in present signature.
namespace lt {
bool platInit(const char*, int) { ++initCalls; return true; }
void platShutdown() {}
bool platPoll() { return true; }
void platSetNativeRendering(bool) {}
bool platOutputSize(int& w, int& h) { w = 1200; h = 720; return true; }
#ifdef NYURGUN_HD_BASELINE
void platPresent(const uint8_t*) { ++presents; }
#else
void platPresent(const uint8_t* p, int width, int height, int pitch) {
    ++presents; presentedW = width; presentedH = height; presentedPitch = pitch;
    expect(p && validFrameLayout(width, height, pitch),
           "presentation contract is valid and bounded");
    const uint8_t* last = p + (height - 1) * pitch + (width - 1) * 4;
    for (int i = 0; i < 4; ++i) presentedLast[i] = last[i];
}
#endif
double platTime() { return 0; }
int platInputDown(const char*) { return 0; }
int platGamepadConnected() { return 0; }
void platRumble(float, float, int) {}
PlatformTouch platTouch() { PlatformTouch t; t.x = 200; t.y = 120; return t; }
std::string platSavePath(const char*) { return {}; }
bool platAudioStart() { return false; }
void platAudioStop() {}
} // namespace lt

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: renderer-test SESSION_OWNED_OUTPUT_DIRECTORY");
        const std::filesystem::path dir(argv[1]);
        if (!std::filesystem::is_directory(dir))
            throw std::runtime_error("output directory must already exist in owned scratch");
        makeInputs(dir);
        unsetenv("LANTERN_RENDER_SCALE");
        unsetenv("LANTERN_RENDER_NATIVE");
        unsetenv("LANTERN_SHOT"); unsetenv("LANTERN_SHOT_FRAME");
        setenv("LANTERN_FIXED_DT", "1", 1);
        expect(lt_boot("fixture", 3), "fixture boot");
        fixture(dir);
#ifdef NYURGUN_HD_BASELINE
        lt_screenshot((dir / "baseline.bmp").c_str());
#else
        lt_screenshot((dir / "scale1.bmp").c_str());
#endif
        lt_shutdown();
#ifndef NYURGUN_HD_BASELINE
        boundedInitialization(dir); scale2(dir); depth2();
#endif
        std::printf("{\"status\":\"passed\",\"assertions\":%d,\"presentations\":%d}\n", assertions, presents);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "HD renderer test failed: %s\n", e.what());
        return 1;
    }
}
