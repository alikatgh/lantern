#include "lantern.h"
#include "gfx.hpp"
#include "platform.hpp"
#include "render_target.hpp"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <sys/resource.h>

namespace {
bool failAllocation = false, countAllocations = false;
size_t allocationCount = 0;
int assertions = 0, initCalls = 0, outputW = 3840, outputH = 2160;
int presentedW = 0, presentedH = 0;
bool platformNative = false;
bool requireLastRed = false;
void expect(bool pass, const char* why) {
    ++assertions;
    if (!pass) throw std::runtime_error(why);
}
void resetEnvironment() {
    unsetenv("LANTERN_RENDER_SCALE"); unsetenv("LANTERN_RENDER_NATIVE");
    unsetenv("LANTERN_SHOT"); unsetenv("LANTERN_FIXED_DT");
}
void settings() {
    resetEnvironment();
    expect(lt_render_width() == 0 && lt_render_height() == 0 && !lt_render_native(), "unbooted info");
    for (const char* value : {"", "0", "2", "01", "1 ", "+1", "native", "1111111111111111"}) {
        setenv("LANTERN_RENDER_NATIVE", value, 1);
        const int before = initCalls;
        expect(!lt_boot("bad native", 3) && before == initCalls, "bad native rejects before platform allocation");
    }
    setenv("LANTERN_RENDER_NATIVE", "1", 1);
    for (const char* value : {"", "1", "9", "10", "11"}) {
        setenv("LANTERN_RENDER_SCALE", value, 1);
        const int before = initCalls;
        expect(!lt_boot("conflict", 3) && before == initCalls, "mode conflict rejects before allocation");
    }
    resetEnvironment();
    for (const char* value : {"010", "10x", "10 ", " 10", "10.0", "00", "100000000000000000000"}) {
        setenv("LANTERN_RENDER_SCALE", value, 1);
        const int before = initCalls;
        expect(!lt_boot("bad scale", 3) && before == initCalls, "strict decimal parser");
    }
    resetEnvironment();
    expect(lt_boot("legacy", 3), "legacy boot");
    expect(lt_render_width() == 400 && lt_render_height() == 240 && !lt_render_native() && !platformNative, "unset legacy target unchanged");
    lt_shutdown();
}
void layouts() {
    for (const auto& size : {std::pair<int,int>{3840,2160}, {1920,1080}, {853,511}, {501,307}, {320,200}, {5,3}, {4000,2400}, {INT_MAX,INT_MAX}}) {
        int w = 0, h = 0;
        expect(lt::nativeFrameSize(size.first, size.second, w, h), "native size fits");
        expect(lt::validFrameLayout(w, h, w * 4) && w <= size.first && h <= size.second, "target is bounded exact 5:3");
        const auto r = lt::presentationRect(size.first, size.second, true);
        if (size.first <= 4000 && size.second <= 2400)
            expect(r.w == w && r.h == h, "native target equals destination without resampling");
    }
    int w = 99, h = 99;
    expect(!lt::nativeFrameSize(0, 100, w, h) && w == 99 && h == 99, "unavailable output retains target");
    expect(!lt::validFrameSize(4001,2400) && !lt::validFrameSize(4000,2401) && !lt::validFrameSize(INT_MAX,INT_MAX), "over-limit and overflow layouts reject");
    expect(!lt::validFrameLayout(4000,2400,16001) && !lt::validFrameLayout(3999,2400,15996), "stride/aspect reject");
    const auto legacy = lt::presentationRect(853,511,false);
    expect(legacy.w == 800 && legacy.h == 480, "legacy integer presentation retained");
    const auto uhd = lt::presentationRect(3840,2160,true);
    expect(uhd.x == 120 && uhd.y == 0 && uhd.w == 3600 && uhd.h == 2160, "UHD content preserves logical5:3");
}
void allocationAndResize() {
    {
        lt::Gfx g;
        failAllocation = true;
        const bool ok = g.initNative(3600,2160);
        expect(!ok && !failAllocation && g.framebufferSize() == 0, "native allocation failure returns empty target");
    }
    {
        lt::Gfx g;
        failAllocation = true;
        const bool ok = g.init(10);
        expect(!ok && !failAllocation && g.framebufferSize() == 0, "explicit allocation failure returns empty target");
    }
    lt::Gfx g;
    expect(g.initNative(400,240), "native init");
    const auto capacity = g.frameCapacityBytes();
    expect(capacity == lt::MAX_FRAME_PIXELS * 8, "native reserve is exactly 73.25MiB for both CPU planes");
    allocationCount = 0; countAllocations = true;
    bool good = true;
    for (const auto& size : {std::pair<int,int>{3600,2160}, {850,510}, {4000,2400}, {5,3}, {1800,1080}, {400,240}}) {
        good = good && g.resizeNative(size.first,size.second);
        g.beginFrame(); g.clear(0,0,0); g.endFrame();
        good = good && g.frameCapacityBytes() == capacity && g.framebufferWidth() == size.first;
    }
    countAllocations = false;
    expect(good && allocationCount == 0, "all native resizes stay within existing allocations");
    expect(!g.resizeNative(4005,2403) && g.framebufferWidth() == 400, "invalid resize leaves target unchanged");
    expect(g.resizeNative(850,510), "fractional target");
    g.setAxisFastPath(false);
    g.beginFrame(); g.clear(0,0,0); g.rect(0,0,400,240,1,0,0,0.5f); g.endFrame();
    const std::vector<uint8_t> alphaReference(g.framebuffer(),g.framebuffer()+g.framebufferSize());
    g.setAxisFastPath(true);
    g.beginFrame(); g.clear(0,0,0); g.rect(0,0,400,240,1,0,0,0.5f); g.endFrame();
    expect(std::equal(alphaReference.begin(),alphaReference.end(),g.framebuffer()), "fractional native full-frame alpha exactly retains reference");
    for (int pixel : {0, 255*850+425, 850*510-1}) {
        const auto* value = g.framebuffer()+size_t(pixel)*4;
        expect(value[0] >= 126 && value[0] <= 127 && value[1] == 0 && value[2] == 0, "fractional native rectangle alpha shades shared diagonal once");
    }
}
void integration() {
    resetEnvironment(); setenv("LANTERN_RENDER_NATIVE", "1", 1);
    expect(lt_boot("native",3) && platformNative && lt_render_native(), "native boot");
    for (const auto& size : {std::pair<int,int>{3840,2160}, {853,511}, {1920,1080}, {4000,2400}, {320,200}}) {
        outputW = size.first; outputH = size.second;
        int w = 0, h = 0; lt::nativeFrameSize(outputW,outputH,w,h);
        requireLastRed = true;
        lt_frame_begin(); lt_clear(0,0,0); lt_rect(399,239,1,1,1,0,0,1); lt_frame_end();
        requireLastRed = false;
        expect(lt_render_width() == w && lt_render_height() == h && presentedW == w && presentedH == h, "current output drives real framebuffer/present metadata");
        expect(lt_screen_w() == 400 && lt_screen_h() == 240 && lt_touch_x() == 200 && lt_touch_y() == 120, "native rendering retains logical dimensions/input");
    }
    outputW = 0; outputH = 0;
    lt_frame_begin(); lt_frame_end();
    expect(lt_render_width() == 320 && lt_render_height() == 192, "minimized output keeps prior bounded frame");
    setenv("LANTERN_RENDER_NATIVE", "bad", 1);
    outputW = 3840; outputH = 2160;
    lt_frame_begin(); lt_frame_end();
    expect(lt_render_width() == 3600, "mode is boot-only");
    lt_shutdown(); resetEnvironment();
}
void benchmark(const char* artDir) {
    lt::Gfx g;
    if (!g.initNative(400,240)) throw std::runtime_error("benchmark init failed");
    int world=-1,hero=-1,font=-1,fontW=0,fontH=0;
    if (artDir) {
        const std::filesystem::path root(artDir);
        world=g.loadTexture((root/"world-0.bmp").string());
        hero=g.loadTexture((root/"hero-atlas.bmp").string());
        font=g.loadTexture((root/"font.bmp").string(),&fontW,&fontH);
        if (world<0||hero<0||font<0) throw std::runtime_error("benchmark current-art inputs missing");
    }
    auto draw = [&](bool painted) {
        g.beginFrame(); g.clear(0,0,0);
        if (!painted) g.rect(0,0,400,240,0.7f,0.5f,0.3f,1);
        else {
            g.spriteUV(world,-200,0,800,240,0,0,1,1);
            for (int i=0;i<6;++i)
                g.spriteUV(hero,25.0f+i*60,148,64,64,i/8.0f,0,(i+1)/8.0f,0.25f);
            for (int i=0;i<160;++i) {
                const float u=float(i%16)/16, v=float(i/16)/11;
                g.spriteUVTinted(font,float(i%60)*6,float(i/60)*11,6,11,u,v,u+1.0f/16,v+1.0f/11,0.94f,0.88f,0.72f);
            }
        }
        g.endFrame();
    };
    std::printf("{\"kind\":\"cpu_render_benchmark\",\"samples\":[");
    bool first = true;
    for (bool painted : {false,true}) {
    if (painted&&!artDir) continue;
    for (int scale : {2,4,9,10}) {
        g.resizeNative(400*scale,240*scale);
        // Warm the quad capacity, pages and renderer before measuring.
        draw(painted);
        double ms[3]; allocationCount = 0; countAllocations = true;
        for (double& value : ms) {
            const auto start = std::chrono::steady_clock::now();
            draw(painted);
            value = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
        countAllocations = false;
        std::sort(ms,ms+3);
        std::printf("%s{\"scene\":\"%s\",\"width\":%d,\"height\":%d,\"median_ms\":%.3f,\"max_ms\":%.3f,\"within_60hz_cpu_budget\":%s,\"frame_allocations\":%zu}",
                    first?"":",",painted?"current_panorama_six_sprites_160_glyphs":"one_opaque_full_frame_rect",400*scale,240*scale,ms[1],ms[2],ms[2]<=1000.0/60?"true":"false",allocationCount);
        first = false;
    }
    }
    rusage usage{}; getrusage(RUSAGE_SELF,&usage);
#ifdef __APPLE__
    const auto rss = static_cast<unsigned long long>(usage.ru_maxrss);
#else
    const auto rss = static_cast<unsigned long long>(usage.ru_maxrss)*1024;
#endif
    std::printf("],\"cpu_frame_capacity_bytes\":%zu,\"process_peak_rss_bytes\":%llu,\"includes_native_upload\":false,\"includes_game_assets\":%s}\n",g.frameCapacityBytes(),rss,artDir?"true":"false");
}
} // namespace

void* operator new(size_t bytes) {
    if (failAllocation) { failAllocation = false; throw std::bad_alloc(); }
    if (countAllocations) ++allocationCount;
    if (void* value = std::malloc(bytes ? bytes : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value,size_t) noexcept { std::free(value); }
void operator delete[](void* value,size_t) noexcept { std::free(value); }

namespace lt {
bool platInit(const char*,int) { ++initCalls; return true; }
void platShutdown() { platformNative = false; }
bool platPoll() { return true; }
void platSetNativeRendering(bool enabled) { platformNative = enabled; }
bool platOutputSize(int& w,int& h) { w = outputW; h = outputH; return w > 0 && h > 0; }
void platPresent(const uint8_t* p,int w,int h,int pitch) {
    expect(p && validFrameLayout(w,h,pitch),"real frame layout valid");
    presentedW = w; presentedH = h;
    if (requireLastRed) {
        const auto* last = p+size_t(h-1)*pitch+size_t(w-1)*4;
        expect(last[0] == 255 && last[1] == 0 && last[2] == 0, "native final physical pixel receives real rasterized logical corner");
    }
}
double platTime() { return 0; }
int platInputDown(const char*) { return 0; }
int platGamepadConnected() { return 0; }
void platRumble(float,float,int) {}
PlatformTouch platTouch() { PlatformTouch t; t.x = 200; t.y = 120; return t; }
std::string platSavePath(const char*) { return {}; }
bool platAudioStart() { return false; }
void platAudioStop() {}
}
int main(int argc,char** argv) {
    try {
        if ((argc == 2||argc==3) && std::string(argv[1]) == "--bench") { benchmark(argc==3?argv[2]:nullptr); return 0; }
        settings(); layouts(); allocationAndResize(); integration();
        std::printf("{\"status\":\"passed\",\"assertions\":%d}\n",assertions);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"native renderer test: %s\n",error.what()); return 1;
    }
}
