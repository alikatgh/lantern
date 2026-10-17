// gfx.hpp — the lantern renderer. As of v0.6 this is OUR OWN SOFTWARE
// RASTERIZER: every physical framebuffer pixel is computed by this code —
// vertex transform, near-plane clipping, perspective-correct triangle
// rasterization, depth buffer, bilinear texture sampling, per-vertex
// lighting, fog, blending. No GPU API. SDL is presentation plumbing only.
#pragma once
#include "lantern_math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lt {

constexpr int SCREEN_W = 400;  // logical coordinates; independent of raster density
constexpr int SCREEN_H = 240;
constexpr int MAX_POINT_LIGHTS = 4;   // PICA200 had 4 hardware lights
constexpr int VERT_FLOATS = 12;       // pos3 normal3 uv2 rgba4

struct TexClamp; // texel bounds for atlas-safe sampling (gfx.cpp)

struct Gfx {
    Gfx();
    ~Gfx();
    Gfx(Gfx&&) noexcept;
    Gfx& operator=(Gfx&&) noexcept;
    bool init(int renderScale = 1);    // bounded 1..10; logical space stays 400x240
    bool initNative(int width, int height);
    bool resizeNative(int width, int height); // no allocations after native init
    void reset();                      // free all game meshes/textures/quads
                                       // (built-ins survive; hot-reload)
    void beginFrame();                 // clear color + depth, reset 2D batch
    void endFrame();                   // flush the 2D batch onto the frame
    const uint8_t* framebuffer() const { return fb_.data(); } // RGBA, top-down
    int framebufferWidth() const { return frameW_; }
    int framebufferHeight() const { return frameH_; }
    int framebufferPitch() const { return frameW_ * 4; }
    size_t framebufferSize() const { return fb_.size(); }
    float renderScale() const { return renderScale_; }
    size_t frameCapacityBytes() const { return fb_.capacity() + depth_.capacity() * sizeof(float); }
    void setAxisFastPath(bool enabled) { axisFastPath_ = enabled; }
    // Opt-in affine painted compositor. 0 chooses up to 8 hardware workers;
    // 1 is serial. Allocation/thread failure deterministically uses serial.
    // Calls, like all other Gfx mutations, belong to this instance's owner.
    bool setPainted2D(bool enabled, int workers = 0);
    bool painted2DEnabled() const { return painted2D_; }
    int painted2DWorkers() const;

    // ---- state ----
    void clear(float r, float g, float b);
    void setCamera(Vec3 eye, Vec3 target, float fovDeg);
    void setLight(Vec3 dir, float ambient);
    void setPointLight(int i, Vec3 pos, float radius, Vec3 color);
    void setFog(float start, float end, Vec3 color); // end<=start disables

    // ---- 3D (immediate, depth-tested, alpha-tested) ----
    int  makeMesh(const float* verts, int vertCount); // VERT_FLOATS per vert
    int  makeCube();
    int  makePlane(int segs);
    int  makeSphere(int seg);
    int  makeCylinder(int seg);
    int  makeCone(int seg);
    void drawMesh(int mesh, int tex, const Mat4& model, float r, float g,
                  float b);           // tex -1 = untextured (white)
    // Keyframe animation, the retro way: draw vertices lerped between two
    // meshes with identical vertex counts (Quake-style frame tweening).
    void drawMeshLerp(int meshA, int meshB, float t, int tex,
                      const Mat4& model, float r, float g, float b);
    void billboard(int tex, Vec3 pos, float w, float h, float u0, float v0,
                   float u1, float v1); // camera-facing, fullbright, fogged
    // Soft dark blob under a character (ALBW-style grounding shadow):
    // alpha-blended decal, depth-tested but not depth-written.
    void shadowBlob(Vec3 pos, float radius, float alpha);

    // ---- 2D (batched, drawn over the 3D frame at endFrame) ----
    int  loadTexture(const std::string& bmpPath, int* outW = nullptr,
                     int* outH = nullptr);
    bool textureHasAlpha(int tex) const;
    void rect(float x, float y, float w, float h, float r, float g, float b,
              float a);
    void sprite(int tex, float x, float y, float sx, float sy);
    void spriteEx(int tex, float x, float y, float sx, float sy, float rot,
                  float r, float g, float b, float a);
    void spriteUV(int tex, float x, float y, float w, float h, float u0,
                  float v0, float u1, float v1);
    void spriteUVTinted(int tex, float x, float y, float w, float h, float u0,
                        float v0, float u1, float v1, float r, float g, float b);
    void spriteGrounded(int tex, int frame, int cols, int rows,
                        float x, float feet, float w, float h,
                        float baseline, int dir,
                        float r, float g, float b, float lift);
    void glowRadial(float cx, float cy, float rx, float ry,
                    float r, float g, float b, float a);
    int  drawCount() const { return lastDrawCount_; }
    void setRecordDraws(bool enabled) { recordDraws_ = enabled; }
    void writeFrameJson(const std::string& jsonPath, long frameIndex) const;
    void print(const char* text, float x, float y, float r, float g, float b,
               float a);              // built-in 8x8 font, 8px advance

    void screenshot(const std::string& bmpPath);

    // public for the pipeline helpers in gfx.cpp
    struct Tex {
        int w = 0, h = 0;
        bool nearest = false;          // default is bilinear — the 3DS look
        bool opaque = false;           // every loaded texel has alpha 255
        bool explicitAlpha = false;    // 32-bit BI_BITFIELDS RGBA mask
        std::string path;
        std::vector<uint8_t> px;       // RGBA
    };
    struct DrawRecord {
        const char* kind = "quad";
        int tex = -1;
        float x = 0, y = 0, w = 0, h = 0;
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        float r = 1, g = 1, b = 1, a = 1;
        bool grounded = false;
        float rotation = 0; // radians around the center of x/y/w/h
    };
    struct Mesh { std::vector<float> v; };  // VERT_FLOATS per vertex
    // A vertex mid-pipeline: clip-space position + shaded attributes.
    struct PVert {
        float x, y, z, w;              // clip space
        float u, v;
        float r, g, b, a;              // lit vertex color × tint
        float fog;
    };
    enum RasterMode {
        M_OPAQUE,                      // depth test+write, alpha test
        M_DECAL,                       // depth test, NO write, alpha blend
    };

  private:
    struct PointLight { Vec3 pos; float radius = 0; Vec3 color; };
    struct Quad2D { int tex; float v[6][8]; }; // x,y,u,v,r,g,b,a per corner

    void emitTriangles(const float* verts, int vertCount, const float* verts2,
                       float lerpT, int tex, const Mat4& model, float r,
                       float g, float b, bool unlit, RasterMode mode,
                       const float* uvRect); // {u0,v0,u1,v1} or nullptr
    void rasterTri(const PVert& a, const PVert& b, const PVert& c,
                   const Tex* tex, const TexClamp& uvClamp, RasterMode mode);
    void raster2DTri(const float* v0, const float* v1, const float* v2,
                     const Tex* tex, const TexClamp& uvClamp);
    void flush2D();
    bool isPaintedAxis(const Quad2D& quad) const;
    struct PixelRect { int x0 = 0, y0 = 0, x1 = -1, y1 = -1; };
    PixelRect paintedBounds(const Quad2D& quad) const;
    void rasterPaintedQuad(const Quad2D& quad, const Tex* tex,
                           const TexClamp& clamp, int firstRow, int lastRow,
                           const PixelRect* occlusion = nullptr);
    void paintedRange(size_t first, size_t last);
    struct RowWorkers;
    bool rasterAxisQuad(const Quad2D& quad, const Tex* tex, const TexClamp& uvClamp);
    void pushQuad(int tex, float x, float y, float w, float h, float u0,
                  float v0, float u1, float v1, float r, float g, float b,
                  float a);
    void pushQuadRot(int tex, float cx, float cy, float w, float h, float rot,
                     float u0, float v0, float u1, float v1, float r, float g,
                     float b, float a);
    int  makeFontTexture();
    int  makeShadowTexture();
    bool initTarget(int width, int height, bool native);
    const Tex* texFor(int tex) const;

    float renderScale_ = 1;
    bool nativeTarget_ = false;
    bool axisFastPath_ = true;
    bool painted2D_ = false;
    bool depthNeedsClear_ = false;
    bool recordDraws_ = false;
    int lastDrawCount_ = 0;
    std::unique_ptr<RowWorkers> rowWorkers_;
    int frameW_ = SCREEN_W, frameH_ = SCREEN_H;
    std::vector<uint8_t> fb_;          // frameW_*frameH_*4, RGBA top-down
    std::vector<float> depth_;
    std::vector<Tex> textures_;
    std::vector<Mesh> meshes_;
    std::vector<Quad2D> quads_;
    std::vector<DrawRecord> drawRecords_;
    int whiteTex_ = -1, fontTex_ = -1, shadowTex_ = -1;

    Mat4 view_, proj_, vp_;            // vp_ = proj*view, cached per camera
    Vec3 camRight_{1, 0, 0}, camUp_{0, 1, 0};
    Vec3 lightDir_{0.4f, -1.0f, -0.3f};
    float ambient_ = 0.35f;
    PointLight pointLights_[MAX_POINT_LIGHTS];
    float fogStart_ = 0, fogEnd_ = -1; // disabled
    Vec3 fogColor_{1, 1, 1};
};

} // namespace lt
