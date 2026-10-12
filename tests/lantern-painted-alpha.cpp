// Compile the actual renderer in this test translation unit so its internal
// BMP reader and sampler can be checked without adding a production/test ABI.
#include "../src/gfx.cpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using Pixel = std::array<uint8_t, 4>;
using RGB = std::array<uint8_t, 3>;
int assertions = 0, rasterProbes = 0, opaqueSamples = 0, nearestSamples = 0;
int hiddenComparisons = 0;
void expect(bool ok, const std::string& label) {
    ++assertions;
    if (!ok) throw std::runtime_error(label);
}
void writeBmp(const std::filesystem::path& path, int w, int h,
              const std::vector<Pixel>& pixels, int format, bool topDown = true) {
    // format: 0=explicit-alpha V4, 1=24-bit RGB, 2=BGRX, 3=V4 without alpha.
    const bool v4 = format == 0 || format == 3;
    const int bpp = format == 1 ? 24 : 32, offset = v4 ? 122 : 54;
    const int pitch = (w * (bpp / 8) + 3) & ~3;
    std::vector<uint8_t> bytes(offset + pitch * h, 0);
    auto put = [&](int at, uint32_t value, int count) {
        for (int i = 0; i < count; ++i) bytes[at + i] = uint8_t(value >> (i * 8));
    };
    bytes[0] = 'B'; bytes[1] = 'M';
    put(2, uint32_t(bytes.size()), 4); put(10, offset, 4);
    put(14, v4 ? 108 : 40, 4); put(18, w, 4);
    put(22, uint32_t(topDown ? -h : h), 4);
    put(26, 1, 2); put(28, bpp, 2); put(30, v4 ? 3 : 0, 4);
    put(34, pitch * h, 4);
    if (v4) {
        put(54, 0x00ff0000, 4); put(58, 0x0000ff00, 4);
        put(62, 0x000000ff, 4); put(66, format == 0 ? 0xff000000 : 0, 4);
    }
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const auto& p = pixels[y * w + x];
        const int at = offset + (topDown ? y : h - 1 - y) * pitch + x * (bpp / 8);
        bytes[at] = p[2]; bytes[at + 1] = p[1]; bytes[at + 2] = p[0];
        if (bpp == 32) bytes[at + 3] = p[3];
    }
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    expect(bool(file), "fixture BMP written");
}
lt::Gfx::Tex read(const std::filesystem::path& path, bool alphaExpected) {
    lt::Gfx::Tex tex; bool explicitAlpha = !alphaExpected;
    expect(lt::loadBmpFile(path.string(), tex.w, tex.h, tex.px, explicitAlpha),
           "actual BMP reader accepts fixture");
    expect(explicitAlpha == alphaExpected, "BMP explicit alpha mask classification");
    return tex;
}
RGB pixel(const lt::Gfx& gfx, int x, int y) {
    const auto* p = gfx.framebuffer() + y * gfx.framebufferPitch() + x * 4;
    return {p[0], p[1], p[2]};
}
void closePixel(RGB actual, RGB expected, const std::string& label, int tolerance = 1) {
    ++rasterProbes;
    for (int i = 0; i < 3; ++i)
        expect(std::abs(int(actual[i]) - int(expected[i])) <= tolerance,
               label + " channel=" + std::to_string(i) + " actual=" +
               std::to_string(actual[i]) + " expected=" + std::to_string(expected[i]));
}
// Independent double-precision, four-weight reference. Composite the filtered
// premultiplied color directly; there is no straight-RGB divide/multiply here.
RGB reference(const lt::Gfx::Tex& tex, double u, double v,
              int xMin, int xMax, int yMin, int yMax, RGB background) {
    const double fx = u * tex.w - .5, fy = v * tex.h - .5;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const double ax = fx - x0, ay = fy - y0;
    double alpha = 0, premultiplied[3] = {};
    for (int iy = 0; iy < 2; ++iy) for (int ix = 0; ix < 2; ++ix) {
        const int x = std::clamp(x0 + ix, xMin, xMax);
        const int y = std::clamp(y0 + iy, yMin, yMax);
        const auto* p = &tex.px[(y * tex.w + x) * 4];
        const double weight = (ix ? ax : 1 - ax) * (iy ? ay : 1 - ay);
        const double a = p[3] / 255.0;
        alpha += weight * a;
        for (int c = 0; c < 3; ++c) premultiplied[c] += weight * p[c] * a;
    }
    // Existing rasterizer rejects coverage <=0.004, including alpha=1/255.
    if (alpha <= .004) return background;
    RGB result{};
    for (int c = 0; c < 3; ++c)
        result[c] = uint8_t(std::clamp(premultiplied[c] + background[c] * (1 - alpha), 0.0, 255.0));
    return result;
}
void loaderAndMagenta(const std::filesystem::path& dir) {
    const uint8_t alphas[] = {0, 1, 64, 128, 192, 255};
    std::vector<Pixel> values;
    for (uint8_t a : alphas) values.push_back({255, 0, 255, a});
    for (bool topDown : {false, true}) {
        auto path = dir / (topDown ? "alpha-top.bmp" : "alpha-bottom.bmp");
        writeBmp(path, 3, 2, values, 0, topDown);
        const auto tex = read(path, true);
        expect(tex.w == 3 && tex.h == 2, "loader dimensions");
        for (size_t i = 0; i < values.size(); ++i)
            for (int c = 0; c < 4; ++c)
                expect(tex.px[i * 4 + c] == values[i][c], "all explicit RGBA bytes retained");
    }
    for (int format : {1, 2, 3}) {
        auto path = dir / ("legacy-" + std::to_string(format) + ".bmp");
        writeBmp(path, 2, 1, {{255, 0, 255, 64}, {30, 180, 90, 0}}, format);
        const auto tex = read(path, false);
        expect(tex.px[3] == 255 && tex.px[7] == 255, "legacy X bytes do not become alpha");
        for (int scale = 1; scale <= 4; ++scale) {
            lt::Gfx gfx; expect(gfx.init(scale), "legacy raster initializes");
            const int handle = gfx.loadTexture(path.string()); expect(handle >= 0, "public legacy load");
            gfx.beginFrame(); gfx.clear(0, 0, 0);
            gfx.spriteUV(handle, 10, 10, 8, 8, 0, 0, .5f, 1);
            gfx.spriteUV(handle, 30, 10, 8, 8, .5f, 0, 1, 1); gfx.endFrame();
            closePixel(pixel(gfx, 14 * scale, 14 * scale), {0, 0, 0}, "legacy magenta still keys", 0);
            closePixel(pixel(gfx, 34 * scale, 14 * scale), {30, 180, 90}, "legacy nonkey remains opaque");
        }
    }
    const auto alphaPath = dir / "alpha-top.bmp";
    for (int scale = 1; scale <= 4; ++scale) for (int bg : {16, 240}) {
        lt::Gfx gfx; expect(gfx.init(scale), "alpha raster initializes");
        const int handle = gfx.loadTexture(alphaPath.string()); expect(handle >= 0, "public explicit load");
        gfx.beginFrame(); gfx.clear(bg / 255.0f, bg / 255.0f, bg / 255.0f);
        const RGB background = pixel(gfx, 0, 0);
        for (int i = 0; i < 6; ++i) {
            const float u0 = (i % 3) / 3.0f, v0 = (i / 3) / 2.0f;
            gfx.spriteUV(handle, float(10 + i * 10), 30, 8, 8, u0, v0, u0 + 1 / 3.0f, v0 + .5f);
        }
        gfx.endFrame();
        const auto tex = read(alphaPath, true);
        for (int i = 0; i < 6; ++i) {
            const int x = i % 3, y = i / 3;
            closePixel(pixel(gfx, (14 + i * 10) * scale, 34 * scale),
                reference(tex, (x + .5) / 3, (y + .5) / 2, x, x, y, y, background),
                "explicit magenta alpha=" + std::to_string(alphas[i]));
        }
    }
}
void samplingParity() {
    lt::Gfx::Tex tex; tex.w = 3; tex.h = 2;
    tex.px = {10, 20, 30, 255, 255, 0, 255, 255, 111, 213, 31, 255,
              254, 253, 252, 255, 13, 37, 59, 255, 0, 255, 0, 255};
    const lt::TexClamp clamp{0, 0, 2, 1};
    for (int j = -4; j <= 36; ++j) for (int i = -4; i <= 52; ++i) {
        const float u = i / 48.0f, v = j / 32.0f;
        const float fx = u * tex.w - .5f, fy = v * tex.h - .5f;
        const int x = int(std::floor(fx)), y = int(std::floor(fy));
        const float ax = fx - x, ay = fy - y;
        const auto* p00 = lt::texelAt(&tex, x, y, clamp);
        const auto* p10 = lt::texelAt(&tex, x + 1, y, clamp);
        const auto* p01 = lt::texelAt(&tex, x, y + 1, clamp);
        const auto* p11 = lt::texelAt(&tex, x + 1, y + 1, clamp);
        float old[4], actual[4];
        for (int c = 0; c < 4; ++c) {
            const float top = p00[c] + (p10[c] - p00[c]) * ax;
            const float bot = p01[c] + (p11[c] - p01[c]) * ax;
            old[c] = (top + (bot - top) * ay) / 255.0f;
        }
        lt::sampleTex(&tex, u, v, clamp, actual);
        expect(std::memcmp(old, actual, sizeof old) == 0, "opaque bilinear float bits retain original arithmetic");
        ++opaqueSamples;
    }
    const uint8_t alphas[] = {0, 1, 64, 128, 192, 255};
    for (int i = 0; i < 6; ++i) tex.px[i * 4 + 3] = alphas[i];
    tex.nearest = true;
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 3; ++x) {
        float actual[4]; lt::sampleTex(&tex, (x + .5f) / 3, (y + .5f) / 2, clamp, actual);
        for (int c = 0; c < 4; ++c)
            expect(actual[c] == tex.px[(y * 3 + x) * 4 + c] / 255.0f, "nearest RGBA unchanged");
        ++nearestSamples;
    }
    // Non-nearest alpha=1 must survive sampling even though the existing 2D
    // rasterizer rejects that tiny coverage. No test-only threshold change.
    tex.nearest = false; tex.w = tex.h = 1; tex.px = {255, 0, 255, 1};
    float actual[4]; lt::sampleTex(&tex, .5f, .5f, {0, 0, 0, 0}, actual);
    expect(actual[0] == 1 && actual[1] == 0 && actual[2] == 1 && actual[3] == 1 / 255.0f,
           "alpha=1 retains straight RGBA in bilinear sampler");
}
struct Quad { float x, y, w, h, u0, v0, u1, v1; };
void atlasAndHiddenRgb(const std::filesystem::path& dir) {
    const RGB hidden[] = {{0, 0, 0}, {255, 255, 255}, {0, 255, 0}};
    const Quad quads[] = {{10.25f, 12.375f, 11.5f, 9.25f, 0, 0, .5f, 1},
                          {50.75f, 12.375f, -11.5f, 9.25f, 0, 0, .5f, 1},
                          {70.25f, 12.375f, 11.5f, 9.25f, .5f, 0, 0, 1},
                          {100.25f, 12.375f, 11.5f, 9.25f, .5f, 1, 0, 0}};
    std::vector<Pixel> base(8 * 4, {0, 255, 0, 255});
    const Pixel cell[] = {{0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0},
                          {0,0,0,0}, {220,90,40,64}, {60,120,230,128}, {0,0,0,0},
                          {255,0,255,192}, {210,170,100,255}, {30,190,70,1}, {0,0,0,0},
                          {0,0,0,0}, {240,230,210,128}, {30,80,180,64}, {0,0,0,0}};
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) base[y * 8 + x] = cell[y * 4 + x];
    for (int variant = 0; variant < 3; ++variant) {
        auto pixels = base;
        for (auto& p : pixels) if (!p[3]) for (int c = 0; c < 3; ++c) p[c] = hidden[variant][c];
        writeBmp(dir / ("atlas-" + std::to_string(variant) + ".bmp"), 8, 4, pixels, 0);
    }
    for (int scale = 1; scale <= 4; ++scale) for (int bg : {16, 240}) {
        std::vector<uint8_t> baseline;
        for (int variant = 0; variant < 3; ++variant) {
            const auto path = dir / ("atlas-" + std::to_string(variant) + ".bmp");
            const auto tex = read(path, true);
            lt::Gfx gfx; expect(gfx.init(scale), "fractional atlas raster initializes");
            const int handle = gfx.loadTexture(path.string()); expect(handle >= 0, "actual atlas load");
            gfx.beginFrame(); gfx.clear(bg / 255.0f, bg / 255.0f, bg / 255.0f);
            const RGB background = pixel(gfx, 0, 0);
            for (const auto& q : quads) gfx.spriteUV(handle, q.x, q.y, q.w, q.h, q.u0, q.v0, q.u1, q.v1);
            gfx.endFrame();
            for (const auto& q : quads) {
                const int left = int(std::floor(std::min(q.x, q.x + q.w) * scale)) - 1;
                const int right = int(std::ceil(std::max(q.x, q.x + q.w) * scale)) + 1;
                const int top = int(std::floor(q.y * scale)) - 1;
                const int bottom = int(std::ceil((q.y + q.h) * scale)) + 1;
                for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
                    const double tx = ((x + .5) / scale - q.x) / q.w;
                    const double ty = ((y + .5) / scale - q.y) / q.h;
                    if (std::abs(tx) < 1e-7 || std::abs(tx - 1) < 1e-7 ||
                        std::abs(ty) < 1e-7 || std::abs(ty - 1) < 1e-7) continue;
                    const bool inside = tx > 0 && tx < 1 && ty > 0 && ty < 1;
                    const RGB expected = inside ? reference(tex, q.u0 + tx * (q.u1 - q.u0),
                        q.v0 + ty * (q.v1 - q.v0), 0, 3, 0, 3, background) : background;
                    closePixel(pixel(gfx, x, y), expected, "fractional/flipped atlas once-composite independent reference");
                }
            }
            if (!variant) {
                baseline.assign(gfx.framebuffer(), gfx.framebuffer() + gfx.framebufferSize());
                gfx.screenshot((dir / ("alpha-s" + std::to_string(scale) + "-bg" + std::to_string(bg) + ".bmp")).string());
            } else {
                expect(std::memcmp(baseline.data(), gfx.framebuffer(), baseline.size()) == 0,
                       "whole framebuffer is invariant to fully transparent black/white/green RGB");
                ++hiddenComparisons;
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2 || !std::filesystem::is_directory(argv[1]))
            throw std::runtime_error("provide existing session-owned output directory");
        const std::filesystem::path dir(argv[1]);
        loaderAndMagenta(dir); samplingParity(); atlasAndHiddenRgb(dir);
        std::printf("{\"status\":\"passed\",\"assertions\":%d,\"raster_pixel_probes\":%d,\"opaque_bit_parity_samples\":%d,\"nearest_rgba_samples\":%d,\"whole_frame_hidden_rgb_comparisons\":%d}\n",
                    assertions, rasterProbes, opaqueSamples, nearestSamples, hiddenComparisons);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Painted alpha test failed: %s\n", error.what()); return 1;
    }
}
