#include "gfx.hpp"
#include <array>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0, negatives = 0, parity = 0;
void expect(bool pass, const char* why) {
    ++checks;
    if (!pass) throw std::runtime_error(why);
}
class Comma : public std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '_'; }
    std::string do_grouping() const override { return "\3"; }
};
void texture(const std::filesystem::path& path) {
    std::array<unsigned char, 70> bmp{};
    auto put = [&](int at, unsigned value, int count) {
        for (int i = 0; i < count; ++i) bmp[at + i] = (value >> (i * 8)) & 255;
    };
    bmp[0] = 'B'; bmp[1] = 'M'; put(2, bmp.size(), 4); put(10, 54, 4);
    put(14, 40, 4); put(18, 2, 4); put(22, 2, 4); put(26, 1, 2); put(28, 24, 2);
    for (int i = 54; i < 70; ++i) bmp[i] = (i * 31) & 255;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
    expect(bool(out), "write control-path BMP");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        const std::filesystem::path root = argv[1];
        std::string name = "controls-";
        for (char c = 1; c < 32; ++c) name += c;
        name += "-quote\"-slash\\-ы.bmp";
        const auto path = root / name;
        texture(path);
        lt::Gfx g;
        expect(g.init(1), "initialize renderer");
        const int tex = g.loadTexture(path.string());
        expect(tex >= 0, "load BMP with control bytes and UTF-8 in path");
        g.setRecordDraws(true);
        g.beginFrame();
        g.rect(1.25f, 2.5f, 3.75f, 4.25f, 0.25f, 0.5f, 0.75f, 1);
        g.spriteEx(tex, 100, 80, 20, 10, 0.7f, 1, 1, 1, 1);
        g.spriteGrounded(tex, 3, 2, 2, 10.75f, 100, 20, 40, 30, -1, 1, 1, 1, 2);
        g.writeFrameJson((root / "ordinary.json").string(), 1234);
        g.endFrame();
        g.writeFrameJson((root / "flushed.json").string(), 1234);

        const auto oldCpp = std::locale();
        const std::string oldC = std::setlocale(LC_NUMERIC, nullptr);
        const bool cComma = std::setlocale(LC_NUMERIC, "fr_FR.UTF-8") != nullptr;
        std::locale::global(std::locale(oldCpp, new Comma));
        g.writeFrameJson((root / "locale.json").string(), 1234);
        std::locale::global(oldCpp);
        std::setlocale(LC_NUMERIC, oldC.c_str());

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        g.beginFrame();
        g.rect(nan, inf, -inf, nan, inf, -inf, nan, inf);
        g.spriteUV(tex, 10, 20, 30, 40, nan, inf, -inf, nan);
        g.spriteEx(tex, 100, 80, 1, 1, inf, 1, 1, 1, 1);
        g.writeFrameJson((root / "nonfinite.json").string(), 5);

        const std::array<float, 9> valid = {10, 100, 20, 40, 30, 1, 1, 1, 0};
        auto reject = [&](std::array<float, 9> v, int frame = 0, int cols = 2, int rows = 2) {
            g.beginFrame();
            g.rect(1, 2, 3, 4, 1, 1, 1, 1);
            g.spriteGrounded(tex, frame, cols, rows, v[0], v[1], v[2], v[3], v[4],
                             1, v[5], v[6], v[7], v[8]);
            expect(g.drawCount() == 1, "invalid grounded call queues neither half");
            ++negatives;
        };
        for (size_t i = 0; i < valid.size(); ++i) for (float value : {nan, inf, -inf}) {
            auto v = valid; v[i] = value; reject(v);
        }
        for (auto index : {2, 3}) for (float value : {0.0f, -1.0f}) {
            auto v = valid; v[index] = value; reject(v);
        }
        for (float value : {-1.0f, 41.0f}) { auto v = valid; v[4] = value; reject(v); }
        for (float value : {-31.0f, std::numeric_limits<float>::max()}) {
            auto v = valid; v[8] = value; reject(v);
        }
        for (int frame : {-1, 4, std::numeric_limits<int>::max()}) reject(valid, frame);
        reject(valid, 0, 0); reject(valid, 0, -1);
        reject(valid, 0, 2, 0); reject(valid, 0, 2, -1);
        for (auto index : {0, 1, 2, 3}) {
            auto v = valid; v[index] = std::numeric_limits<float>::max(); reject(v);
        }
        g.writeFrameJson((root / "rejected.json").string(), 6);

        // Compare valid grounded rendering with the established two-quad
        // contract, including negative lift, mirrored UVs and atlas edges.
        for (int scale : {1, 2}) for (bool painted : {false, true}) {
            lt::Gfx actual, expected;
            expect(actual.init(scale) && expected.init(scale), "parity targets");
            actual.setPainted2D(painted, 1); expected.setPainted2D(painted, 1);
            const int atex = actual.loadTexture(path.string()), etex = expected.loadTexture(path.string());
            expect(atex >= 0 && etex >= 0, "parity textures");
            for (int frame : {0, 3}) for (int dir : {-1, 1}) for (float baseline : {0.0f, 30.0f, 40.0f}) {
                const float lift = baseline ? -2.0f : 2.0f;
                actual.beginFrame(); expected.beginFrame();
                actual.spriteGrounded(atex, frame, 2, 2, 10.75f, 100, 20, 40, baseline, dir, 0.5f, 1, 0.75f, lift);
                float u0 = float(frame % 2) / 2, u1 = float(frame % 2 + 1) / 2;
                if (dir < 0) std::swap(u0, u1);
                const float v0 = float(frame / 2) / 2, sole = v0 + baseline / (40 * 2);
                expected.spriteUVTinted(etex, 10, 100 - baseline - lift, 20, baseline + lift, u0, v0, u1, sole, 0.5f, 1, 0.75f);
                expected.spriteUVTinted(etex, 10, 100, 20, 40 - baseline, u0, sole, u1, v0 + 0.5f, 0.5f, 1, 0.75f);
                expect(actual.drawCount() == 2, "valid grounded calls retain both halves");
                actual.endFrame(); expected.endFrame();
                expect(std::equal(actual.framebuffer(), actual.framebuffer() + actual.framebufferSize(), expected.framebuffer()), "valid grounded framebuffer bytes unchanged");
                ++parity;
            }
        }
        std::printf("{\"checks\":%d,\"negative_cases\":%d,\"pixel_parity_cases\":%d,\"c_comma_locale\":%s}\n", checks, negatives, parity, cComma ? "true" : "false");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "frame_scene_test: %s\n", error.what());
        return 1;
    }
}
