// platform_ios.mm — the iOS/iPadOS backend. Implements platform.hpp:
// Metal presentation of the 400x240 frame (integer scale, letterboxed),
// UITouch input (forwarded by the host via platform_ios.h), the audio
// device (AVAudioSourceNode pulling our mixer), save paths, time.
// NO rendering happens here — the frame arrives fully rasterized from
// our software rasterizer; Metal is presentation plumbing, exactly the
// role SDL_Renderer plays on desktop.
#include "platform.hpp"
#include "platform_ios.h"
#include "audio.hpp"
#include "gfx.hpp" // SCREEN_W/H
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace lt {
namespace {

CAMetalLayer* g_layer = nil;
id<MTLDevice> g_dev = nil;
id<MTLCommandQueue> g_queue = nil;
id<MTLTexture> g_frameTex = nil;
id<MTLRenderPipelineState> g_pso = nil;
AVAudioEngine* g_audio = nil;
AVAudioSourceNode* g_srcNode = nil;

bool g_touchDown = false;
uint32_t g_touchSeq = 0; // bumped per press — sub-frame taps stay visible
float g_touchPxX = 0, g_touchPxY = 0; // drawable pixels
std::chrono::steady_clock::time_point g_t0;

// The whole presentation "pipeline": sample the frame texture with
// NEAREST filtering onto a letterboxed quad. rect = x0,y0,x1,y1 in NDC.
constexpr const char* kBlitShader = R"(
#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; float2 uv; };
vertex VOut vmain(uint vid [[vertex_id]], constant float4& rect [[buffer(0)]]) {
    float2 p[4]  = { {rect.x,rect.y}, {rect.z,rect.y},
                     {rect.x,rect.w}, {rect.z,rect.w} };
    float2 uv[4] = { {0,1}, {1,1}, {0,0}, {1,0} };
    VOut o; o.pos = float4(p[vid], 0, 1); o.uv = uv[vid]; return o;
}
fragment float4 fmain(VOut in [[stage_in]],
                      texture2d<float> tex [[texture(0)]]) {
    constexpr sampler s(mag_filter::nearest, min_filter::nearest);
    return tex.sample(s, in.uv);
}
)";

struct DstRect { int x, y, w, h; };
DstRect dstRect(int dw, int dh) {
    int s = std::max(1, std::min(dw / SCREEN_W, dh / SCREEN_H));
    return {(dw - SCREEN_W * s) / 2, (dh - SCREEN_H * s) / 2, SCREEN_W * s,
            SCREEN_H * s};
}

} // namespace

void iosSetMetalLayer(CAMetalLayer* layer) { g_layer = layer; }
void iosTouchBegan(float px, float py) {
    g_touchDown = true;
    g_touchSeq++;
    g_touchPxX = px;
    g_touchPxY = py;
}
void iosTouchMoved(float px, float py) {
    if (!g_touchDown) return;
    g_touchPxX = px;
    g_touchPxY = py;
}
void iosTouchEnded(void) { g_touchDown = false; }

bool platInit(const char* /*title*/, int /*windowScale*/) {
    if (!g_layer) {
        std::fprintf(stderr, "platInit: no CAMetalLayer registered "
                             "(call lt::iosSetMetalLayer first)\n");
        return false;
    }
    g_dev = g_layer.device ? g_layer.device : MTLCreateSystemDefaultDevice();
    if (!g_dev) return false;
    g_layer.device = g_dev;
    g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    g_queue = [g_dev newCommandQueue];

    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:SCREEN_W
                                    height:SCREEN_H
                                 mipmapped:NO];
    g_frameTex = [g_dev newTextureWithDescriptor:td];

    NSError* err = nil;
    id<MTLLibrary> lib =
        [g_dev newLibraryWithSource:@(kBlitShader) options:nil error:&err];
    if (!lib) {
        std::fprintf(stderr, "metal shader: %s\n",
                     err.localizedDescription.UTF8String);
        return false;
    }
    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vmain"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fmain"];
    pd.colorAttachments[0].pixelFormat = g_layer.pixelFormat;
    g_pso = [g_dev newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!g_pso) {
        std::fprintf(stderr, "metal pipeline: %s\n",
                     err.localizedDescription.UTF8String);
        return false;
    }
    g_t0 = std::chrono::steady_clock::now();
    return true;
}

void platShutdown() {
    platAudioStop();
    g_pso = nil;
    g_frameTex = nil;
    g_queue = nil;
    g_dev = nil;
    g_layer = nil;
    g_touchDown = false;
}

// UIKit pumps its own events; quit is not a thing an iOS game initiates.
bool platPoll() { return true; }

void platPresent(const uint8_t* fb) {
    if (!g_queue || !g_layer) return;
    @autoreleasepool {
        [g_frameTex replaceRegion:MTLRegionMake2D(0, 0, SCREEN_W, SCREEN_H)
                      mipmapLevel:0
                        withBytes:fb
                      bytesPerRow:SCREEN_W * 4];
        id<CAMetalDrawable> drawable = [g_layer nextDrawable];
        if (!drawable) return; // layout not ready yet: skip the present
        int dw = (int)g_layer.drawableSize.width;
        int dh = (int)g_layer.drawableSize.height;
        DstRect d = dstRect(dw, dh);
        float rect[4] = {
            2.0f * d.x / dw - 1.0f,             // x0 (left, NDC)
            1.0f - 2.0f * (float)(d.y + d.h) / dh, // y0 (bottom, NDC)
            2.0f * (float)(d.x + d.w) / dw - 1.0f, // x1 (right, NDC)
            1.0f - 2.0f * d.y / dh,             // y1 (top, NDC)
        };
        MTLRenderPassDescriptor* rp =
            [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = // the desktop letterbox gray
            MTLClearColorMake(13 / 255.0, 13 / 255.0, 15 / 255.0, 1);
        id<MTLCommandBuffer> cb = [g_queue commandBuffer];
        id<MTLRenderCommandEncoder> enc =
            [cb renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:g_pso];
        [enc setVertexBytes:rect length:sizeof rect atIndex:0];
        [enc setFragmentTexture:g_frameTex atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0
                vertexCount:4];
        [enc endEncoding];
        [cb presentDrawable:drawable];
        [cb commit];
    }
}

double platTime() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         g_t0)
        .count();
}

// No keyboard on a phone; the named-button set arrives with game-
// controller support (GCController) later. Touch is the input story.
int platInputDown(const char* /*name*/) { return 0; }
int platGamepadConnected() { return 0; }
void platRumble(float /*low*/, float /*high*/, int /*durationMs*/) {}

PlatformTouch platTouch() {
    PlatformTouch t;
    t.down = g_touchDown;
    t.seq = g_touchSeq;
    if (!g_layer) return t;
    int dw = (int)g_layer.drawableSize.width;
    int dh = (int)g_layer.drawableSize.height;
    if (dw <= 0 || dh <= 0) return t;
    DstRect d = dstRect(dw, dh);
    float s = (float)d.w / (float)SCREEN_W;
    t.x = std::clamp((g_touchPxX - (float)d.x) / s, 0.0f,
                     (float)SCREEN_W - 1);
    t.y = std::clamp((g_touchPxY - (float)d.y) / s, 0.0f,
                     (float)SCREEN_H - 1);
    return t;
}

std::string platSavePath(const char* name) {
    NSString* dir = [NSSearchPathForDirectoriesInDomains(
                         NSApplicationSupportDirectory, NSUserDomainMask, YES)
                        firstObject];
    if (!dir) return {};
    dir = [dir stringByAppendingPathComponent:@"lantern/saves"];
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir
                                   withIntermediateDirectories:YES
                                                    attributes:nil
                                                         error:nil])
        return {};
    return std::string(
        [[dir stringByAppendingPathComponent:@(name)] UTF8String]);
}

bool platAudioStart() {
    @autoreleasepool {
        @try {
            [[AVAudioSession sharedInstance]
                setCategory:AVAudioSessionCategoryAmbient
                      error:nil];
            [[AVAudioSession sharedInstance] setActive:YES error:nil];
            // The render block sees our native interleaved layout; the
            // CONNECTION must use the engine's standard (non-interleaved)
            // format — AVAudioEngine converts, and rejects interleaved
            // mixer inputs outright (kAudioUnitErr_FormatNotSupported).
            AVAudioFormat* renderFmt = [[AVAudioFormat alloc]
                initWithCommonFormat:AVAudioPCMFormatFloat32
                          sampleRate:48000
                            channels:2
                         interleaved:YES];
            AVAudioFormat* busFmt = [[AVAudioFormat alloc]
                initStandardFormatWithSampleRate:48000
                                        channels:2];
            g_srcNode = [[AVAudioSourceNode alloc]
                initWithFormat:renderFmt
                   renderBlock:^OSStatus(BOOL*, const AudioTimeStamp*,
                                         AVAudioFrameCount frameCount,
                                         AudioBufferList* abl) {
                       mixerRender((float*)abl->mBuffers[0].mData,
                                   (int)frameCount);
                       return noErr;
                   }];
            g_audio = [[AVAudioEngine alloc] init];
            [g_audio attachNode:g_srcNode];
            [g_audio connect:g_srcNode
                          to:g_audio.mainMixerNode
                      format:busFmt];
            NSError* err = nil;
            if (![g_audio startAndReturnError:&err]) {
                std::fprintf(stderr, "audio: %s (continuing silent)\n",
                             err.localizedDescription.UTF8String);
                g_audio = nil;
                g_srcNode = nil;
                return false;
            }
            return true;
        } @catch (NSException* e) {
            // audio must NEVER take boot down with it
            std::fprintf(stderr, "audio: %s (continuing silent)\n",
                         e.reason.UTF8String);
            g_audio = nil;
            g_srcNode = nil;
            return false;
        }
    }
}

void platAudioStop() {
    if (g_audio) [g_audio stop];
    g_audio = nil;
    g_srcNode = nil;
}

} // namespace lt
