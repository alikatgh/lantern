// gfx.cpp — lantern's software rasterizer. Every pixel is ours: transform,
// near clip, perspective-correct raster, depth, bilinear sampling, lighting,
// fog, blending — all computed here, no GPU API anywhere.
//
// At 400×240 (96k pixels) a tight scalar rasterizer comfortably feeds
// 60 fps on anything modern; that resolution ceiling is exactly what makes
// owning the pipeline practical. Same philosophy as the famemu RF chain:
// nothing delegated, nothing faked.
#include "gfx.hpp"
#include "lantern_font.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace lt {

// ---------------------------------------------------------------- textures

const Gfx::Tex* Gfx::texFor(int tex) const {
    if (tex >= 0 && tex < (int)textures_.size()) return &textures_[tex];
    return &textures_[whiteTex_];
}

// Bilinear by default — the actual 3DS look (ALBW is soft, not pixelated).
// At exact 1:1 scale bilinear lands on texel centers and is identical to
// nearest, so UI/font at native size stays crisp.
static inline void sampleTex(const Gfx::Tex* t, float u, float v, float* out);

struct TexelFetch {
    static inline const uint8_t* at(const Gfx::Tex* t, int x, int y) {
        x = x < 0 ? 0 : (x >= t->w ? t->w - 1 : x);
        y = y < 0 ? 0 : (y >= t->h ? t->h - 1 : y);
        return &t->px[(size_t)(y * t->w + x) * 4];
    }
};

static inline void sampleTex(const Gfx::Tex* t, float u, float v, float* out) {
    if (t->nearest) {
        int x = (int)(u * t->w);
        int y = (int)(v * t->h);
        const uint8_t* p = TexelFetch::at(t, x, y);
        out[0] = p[0] / 255.0f; out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f; out[3] = p[3] / 255.0f;
        return;
    }
    float fx = u * t->w - 0.5f, fy = v * t->h - 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float ax = fx - x0, ay = fy - y0;
    const uint8_t* p00 = TexelFetch::at(t, x0, y0);
    const uint8_t* p10 = TexelFetch::at(t, x0 + 1, y0);
    const uint8_t* p01 = TexelFetch::at(t, x0, y0 + 1);
    const uint8_t* p11 = TexelFetch::at(t, x0 + 1, y0 + 1);
    for (int i = 0; i < 4; i++) {
        float top = p00[i] + (p10[i] - p00[i]) * ax;
        float bot = p01[i] + (p11[i] - p01[i]) * ax;
        out[i] = (top + (bot - top) * ay) / 255.0f;
    }
}

// ------------------------------------------------------------------- init

int Gfx::makeFontTexture() {
    Tex t;
    t.w = 128; t.h = 32;
    t.nearest = true;                   // glyphs are used 1:1; keep exact
    t.px.assign((size_t)t.w * t.h * 4, 0);
    for (int g = 0; g < 64; g++) {
        int gx = (g % 16) * 8, gy = (g / 16) * 8;
        for (int row = 0; row < 8; row++) {
            uint8_t bits = LT_FONT8[g][row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    size_t i = ((size_t)(gy + row) * t.w + gx + col) * 4;
                    t.px[i] = t.px[i + 1] = t.px[i + 2] = t.px[i + 3] = 255;
                }
            }
        }
    }
    textures_.push_back(std::move(t));
    return (int)textures_.size() - 1;
}

// radial-falloff disc for shadowBlob: white with soft alpha edge
int Gfx::makeShadowTexture() {
    Tex t;
    t.w = 64; t.h = 64;
    t.px.resize((size_t)t.w * t.h * 4);
    for (int y = 0; y < t.h; y++)
        for (int x = 0; x < t.w; x++) {
            float dx = (x + 0.5f) / t.w - 0.5f, dy = (y + 0.5f) / t.h - 0.5f;
            float d = std::sqrt(dx * dx + dy * dy) * 2.0f; // 0 center, 1 edge
            float a = d >= 1.0f ? 0.0f : (1.0f - d * d);   // soft falloff
            size_t i = ((size_t)y * t.w + x) * 4;
            t.px[i] = t.px[i + 1] = t.px[i + 2] = 255;
            t.px[i + 3] = (uint8_t)(a * 255.0f);
        }
    textures_.push_back(std::move(t));
    return (int)textures_.size() - 1;
}

bool Gfx::init() {
    fb_.assign((size_t)SCREEN_W * SCREEN_H * 4, 0);
    depth_.assign((size_t)SCREEN_W * SCREEN_H, 1.0f);
    Tex white;
    white.w = white.h = 1;
    white.px = {255, 255, 255, 255};
    textures_.push_back(std::move(white));
    whiteTex_ = 0;
    fontTex_ = makeFontTexture();
    shadowTex_ = makeShadowTexture();
    setCamera({0, 3, 6}, {0, 0, 0}, 55.0f);
    return true;
}

void Gfx::beginFrame() {
    std::memset(fb_.data(), 0, fb_.size());
    std::fill(depth_.begin(), depth_.end(), 1.0f);
    quads_.clear();
}

void Gfx::clear(float r, float g, float b) {
    uint8_t cr = (uint8_t)(std::min(std::max(r, 0.0f), 1.0f) * 255);
    uint8_t cg = (uint8_t)(std::min(std::max(g, 0.0f), 1.0f) * 255);
    uint8_t cb = (uint8_t)(std::min(std::max(b, 0.0f), 1.0f) * 255);
    for (size_t i = 0; i < fb_.size(); i += 4) {
        fb_[i] = cr; fb_[i + 1] = cg; fb_[i + 2] = cb; fb_[i + 3] = 255;
    }
    std::fill(depth_.begin(), depth_.end(), 1.0f);
}

void Gfx::setCamera(Vec3 eye, Vec3 target, float fovDeg) {
    view_ = lookAt(eye, target, {0, 1, 0});
    proj_ = perspective(fovDeg, (float)SCREEN_W / SCREEN_H, 0.1f, 200.0f);
    camEye_ = eye;
    camRight_ = {view_.m[0], view_.m[4], view_.m[8]};
    camUp_ = {view_.m[1], view_.m[5], view_.m[9]};
}

void Gfx::setLight(Vec3 dir, float ambient) {
    lightDir_ = dir;
    ambient_ = ambient;
}

void Gfx::setPointLight(int i, Vec3 pos, float radius, Vec3 color) {
    if (i < 0 || i >= MAX_POINT_LIGHTS) return;
    pointLights_[i] = {pos, radius, color};
}

void Gfx::setFog(float start, float end, Vec3 color) {
    fogStart_ = start;
    fogEnd_ = end;
    fogColor_ = color;
}

// ------------------------------------------------------------------ meshes

int Gfx::makeMesh(const float* verts, int vertCount) {
    Mesh m;
    m.v.assign(verts, verts + (size_t)vertCount * VERT_FLOATS);
    meshes_.push_back(std::move(m));
    return (int)meshes_.size() - 1;
}

static void vtx(std::vector<float>& v, Vec3 p, Vec3 n, float u, float uvv) {
    v.insert(v.end(), {p.x, p.y, p.z, n.x, n.y, n.z, u, uvv, 1, 1, 1, 1});
}

int Gfx::makeCube() {
    std::vector<float> v;
    auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n) {
        const Vec3 p[6] = {a, b, c, a, c, d};
        const float uv[6][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 1}, {1, 0}, {0, 0}};
        for (int i = 0; i < 6; i++) vtx(v, p[i], n, uv[i][0], uv[i][1]);
    };
    const float h = 0.5f;
    face({-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h},{0,0,1});
    face({ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h},{0,0,-1});
    face({ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h},{1,0,0});
    face({-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h},{-1,0,0});
    face({-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h},{0,1,0});
    face({-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h},{0,-1,0});
    return makeMesh(v.data(), (int)v.size() / VERT_FLOATS);
}

int Gfx::makePlane(int segs) {
    if (segs < 1) segs = 1;
    std::vector<float> v;
    const Vec3 n{0, 1, 0};
    for (int i = 0; i < segs; i++)
        for (int j = 0; j < segs; j++) {
            float x0 = -0.5f + (float)i / segs, x1 = x0 + 1.0f / segs;
            float z0 = -0.5f + (float)j / segs, z1 = z0 + 1.0f / segs;
            float u0 = x0 + 0.5f, u1 = x1 + 0.5f;
            float t0 = z0 + 0.5f, t1 = z1 + 0.5f;
            vtx(v, {x0, 0, z0}, n, u0, t0);
            vtx(v, {x0, 0, z1}, n, u0, t1);
            vtx(v, {x1, 0, z1}, n, u1, t1);
            vtx(v, {x0, 0, z0}, n, u0, t0);
            vtx(v, {x1, 0, z1}, n, u1, t1);
            vtx(v, {x1, 0, z0}, n, u1, t0);
        }
    return makeMesh(v.data(), (int)v.size() / VERT_FLOATS);
}

int Gfx::makeSphere(int seg) {
    if (seg < 3) seg = 3;
    const int rings = seg, slices = seg * 2;
    const float PI = 3.14159265f;
    auto point = [&](int r, int s, Vec3& p, float& u, float& v2) {
        float phi = PI * r / rings;
        float theta = 2 * PI * s / slices;
        p = {0.5f * std::sin(phi) * std::cos(theta), 0.5f * std::cos(phi),
             0.5f * std::sin(phi) * std::sin(theta)};
        u = (float)s / slices;
        v2 = (float)r / rings;
    };
    std::vector<float> v;
    for (int r = 0; r < rings; r++)
        for (int s = 0; s < slices; s++) {
            Vec3 p00, p01, p10, p11;
            float u00, v00, u01, v01, u10, v10, u11, v11;
            point(r, s, p00, u00, v00);
            point(r + 1, s, p01, u01, v01);
            point(r, s + 1, p10, u10, v10);
            point(r + 1, s + 1, p11, u11, v11);
            vtx(v, p00, norm(p00), u00, v00);
            vtx(v, p01, norm(p01), u01, v01);
            vtx(v, p11, norm(p11), u11, v11);
            vtx(v, p00, norm(p00), u00, v00);
            vtx(v, p11, norm(p11), u11, v11);
            vtx(v, p10, norm(p10), u10, v10);
        }
    return makeMesh(v.data(), (int)v.size() / VERT_FLOATS);
}

int Gfx::makeCylinder(int seg) {
    if (seg < 3) seg = 3;
    const float PI = 3.14159265f, h = 0.5f, r = 0.5f;
    std::vector<float> v;
    for (int s = 0; s < seg; s++) {
        float a0 = 2 * PI * s / seg, a1 = 2 * PI * (s + 1) / seg;
        Vec3 n0{std::cos(a0), 0, std::sin(a0)}, n1{std::cos(a1), 0, std::sin(a1)};
        Vec3 b0{r * n0.x, -h, r * n0.z}, b1{r * n1.x, -h, r * n1.z};
        Vec3 t0{r * n0.x, h, r * n0.z}, t1{r * n1.x, h, r * n1.z};
        float u0 = (float)s / seg, u1 = (float)(s + 1) / seg;
        vtx(v, b0, n0, u0, 1); vtx(v, t0, n0, u0, 0); vtx(v, t1, n1, u1, 0);
        vtx(v, b0, n0, u0, 1); vtx(v, t1, n1, u1, 0); vtx(v, b1, n1, u1, 1);
        vtx(v, {0, h, 0}, {0, 1, 0}, .5f, .5f);
        vtx(v, t1, {0, 1, 0}, .5f + n1.x / 2, .5f + n1.z / 2);
        vtx(v, t0, {0, 1, 0}, .5f + n0.x / 2, .5f + n0.z / 2);
        vtx(v, {0, -h, 0}, {0, -1, 0}, .5f, .5f);
        vtx(v, b0, {0, -1, 0}, .5f + n0.x / 2, .5f + n0.z / 2);
        vtx(v, b1, {0, -1, 0}, .5f + n1.x / 2, .5f + n1.z / 2);
    }
    return makeMesh(v.data(), (int)v.size() / VERT_FLOATS);
}

int Gfx::makeCone(int seg) {
    if (seg < 3) seg = 3;
    const float PI = 3.14159265f, h = 0.5f, r = 0.5f;
    std::vector<float> v;
    const Vec3 apex{0, h, 0};
    for (int s = 0; s < seg; s++) {
        float a0 = 2 * PI * s / seg, a1 = 2 * PI * (s + 1) / seg;
        Vec3 d0{std::cos(a0), 0, std::sin(a0)}, d1{std::cos(a1), 0, std::sin(a1)};
        Vec3 b0{r * d0.x, -h, r * d0.z}, b1{r * d1.x, -h, r * d1.z};
        auto sn = [&](Vec3 d) { return norm({d.x, r / h * 0.5f + 0.5f, d.z}); };
        vtx(v, b0, sn(d0), (float)s / seg, 1);
        vtx(v, apex, sn(norm({d0.x + d1.x, 0, d0.z + d1.z})),
            (s + 0.5f) / seg, 0);
        vtx(v, b1, sn(d1), (float)(s + 1) / seg, 1);
        vtx(v, {0, -h, 0}, {0, -1, 0}, .5f, .5f);
        vtx(v, b0, {0, -1, 0}, .5f + d0.x / 2, .5f + d0.z / 2);
        vtx(v, b1, {0, -1, 0}, .5f + d1.x / 2, .5f + d1.z / 2);
    }
    return makeMesh(v.data(), (int)v.size() / VERT_FLOATS);
}

// ------------------------------------------------------- the 3D pipeline

// Clip a polygon against the near plane (w >= eps) in clip space,
// interpolating every attribute linearly — correct in clip space.
static int clipNear(Gfx::PVert* in, int n, Gfx::PVert* out) {
    const float EPS = 1e-3f;
    int m = 0;
    for (int i = 0; i < n; i++) {
        const Gfx::PVert& a = in[i];
        const Gfx::PVert& b = in[(i + 1) % n];
        bool ain = a.w >= EPS, bin = b.w >= EPS;
        if (ain) out[m++] = a;
        if (ain != bin) {
            float t = (EPS - a.w) / (b.w - a.w);
            Gfx::PVert v;
            v.x = a.x + (b.x - a.x) * t;
            v.y = a.y + (b.y - a.y) * t;
            v.z = a.z + (b.z - a.z) * t;
            v.w = EPS;
            v.u = a.u + (b.u - a.u) * t;
            v.v = a.v + (b.v - a.v) * t;
            v.r = a.r + (b.r - a.r) * t;
            v.g = a.g + (b.g - a.g) * t;
            v.b = a.b + (b.b - a.b) * t;
            v.a = a.a + (b.a - a.a) * t;
            v.fog = a.fog + (b.fog - a.fog) * t;
            out[m++] = v;
        }
    }
    return m;
}

void Gfx::rasterTri(const PVert& A, const PVert& B, const PVert& C,
                    const Tex* tex, RasterMode mode) {
    // to screen space; keep 1/w for perspective-correct interpolation
    struct SV { float x, y, z, iw, u, v, r, g, b, a, fog; };
    auto toScreen = [](const PVert& p, SV& s) {
        float iw = 1.0f / p.w;
        s.x = (p.x * iw * 0.5f + 0.5f) * SCREEN_W;
        s.y = (1.0f - (p.y * iw * 0.5f + 0.5f)) * SCREEN_H;
        s.z = p.z * iw;                 // NDC z, affine in screen space
        s.iw = iw;
        s.u = p.u * iw; s.v = p.v * iw; // attrs pre-divided by w
        s.r = p.r * iw; s.g = p.g * iw; s.b = p.b * iw; s.a = p.a * iw;
        s.fog = p.fog * iw;
    };
    SV a, b, c;
    toScreen(A, a); toScreen(B, b); toScreen(C, c);

    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-6f) return;  // degenerate
    float invArea = 1.0f / area;

    int x0 = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x})));
    int x1 = std::min(SCREEN_W - 1, (int)std::ceil(std::max({a.x, b.x, c.x})));
    int y0 = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y})));
    int y1 = std::min(SCREEN_H - 1, (int)std::ceil(std::max({a.y, b.y, c.y})));
    if (x0 > x1 || y0 > y1) return;

    auto edge = [](const SV& p, const SV& q, float px, float py) {
        return (q.x - p.x) * (py - p.y) - (q.y - p.y) * (px - p.x);
    };

    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f;
        uint8_t* row = &fb_[((size_t)y * SCREEN_W + x0) * 4];
        float* drow = &depth_[(size_t)y * SCREEN_W + x0];
        for (int x = x0; x <= x1; x++, row += 4, drow++) {
            float px = x + 0.5f;
            float w0 = edge(b, c, px, py);
            float w1 = edge(c, a, px, py);
            float w2 = edge(a, b, px, py);
            // two-sided: accept either winding
            bool inPos = (w0 >= 0 && w1 >= 0 && w2 >= 0);
            bool inNeg = (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inPos && !inNeg) continue;
            float b0 = w0 * invArea, b1 = w1 * invArea, b2 = w2 * invArea;

            float z = b0 * a.z + b1 * b.z + b2 * c.z;
            if (z < -1.0f || z > 1.0f) continue;
            float zd = z * 0.5f + 0.5f;
            if (zd >= *drow) continue;   // depth test (less)

            float iw = b0 * a.iw + b1 * b.iw + b2 * c.iw;
            float wr = 1.0f / iw;
            float u = (b0 * a.u + b1 * b.u + b2 * c.u) * wr;
            float v = (b0 * a.v + b1 * b.v + b2 * c.v) * wr;
            float cr = (b0 * a.r + b1 * b.r + b2 * c.r) * wr;
            float cg = (b0 * a.g + b1 * b.g + b2 * c.g) * wr;
            float cb = (b0 * a.b + b1 * b.b + b2 * c.b) * wr;
            float ca = (b0 * a.a + b1 * b.a + b2 * c.a) * wr;
            float fog = (b0 * a.fog + b1 * b.fog + b2 * c.fog) * wr;

            float t[4] = {1, 1, 1, 1};
            if (tex) sampleTex(tex, u, v, t);
            float R = t[0] * cr, G = t[1] * cg, B = t[2] * cb;
            float Aa = t[3] * ca;

            if (mode == M_OPAQUE) {
                if (Aa < 0.5f) continue;             // alpha test
                R = R + (fogColor_.x - R) * fog;
                G = G + (fogColor_.y - G) * fog;
                B = B + (fogColor_.z - B) * fog;
                row[0] = (uint8_t)(std::min(R, 1.0f) * 255);
                row[1] = (uint8_t)(std::min(G, 1.0f) * 255);
                row[2] = (uint8_t)(std::min(B, 1.0f) * 255);
                row[3] = 255;
                *drow = zd;                          // depth write
            } else {                                 // M_DECAL: blend, no zwrite
                if (Aa <= 0.004f) continue;
                if (Aa > 1) Aa = 1;
                row[0] = (uint8_t)(std::min(R, 1.0f) * 255 * Aa +
                                   row[0] * (1 - Aa));
                row[1] = (uint8_t)(std::min(G, 1.0f) * 255 * Aa +
                                   row[1] * (1 - Aa));
                row[2] = (uint8_t)(std::min(B, 1.0f) * 255 * Aa +
                                   row[2] * (1 - Aa));
            }
        }
    }
}

// Transform + light + clip + rasterize a vertex stream. verts2/lerpT tween
// between two keyframes (drawMeshLerp); verts2 == nullptr means no tween.
void Gfx::emitTriangles(const float* verts, int vertCount, const float* verts2,
                        float lerpT, int tex, const Mat4& model, float r,
                        float g, float b, bool unlit, RasterMode mode) {
    Mat4 vp = mul(proj_, view_);
    Mat4 mvp = mul(vp, model);
    const Tex* T = tex >= 0 ? texFor(tex) : nullptr;
    bool fogOn = fogEnd_ > fogStart_;

    PVert tri[3];
    for (int i = 0; i + 2 < vertCount; i += 3) {
        for (int k = 0; k < 3; k++) {
            const float* vv = &verts[(size_t)(i + k) * VERT_FLOATS];
            float p[3] = {vv[0], vv[1], vv[2]};
            float nrm[3] = {vv[3], vv[4], vv[5]};
            if (verts2) {
                const float* v2 = &verts2[(size_t)(i + k) * VERT_FLOATS];
                for (int j = 0; j < 3; j++) {
                    p[j] += (v2[j] - p[j]) * lerpT;
                    nrm[j] += (v2[3 + j] - nrm[j]) * lerpT;
                }
            }
            PVert& o = tri[k];
            // clip position
            o.x = mvp.m[0] * p[0] + mvp.m[4] * p[1] + mvp.m[8] * p[2] + mvp.m[12];
            o.y = mvp.m[1] * p[0] + mvp.m[5] * p[1] + mvp.m[9] * p[2] + mvp.m[13];
            o.z = mvp.m[2] * p[0] + mvp.m[6] * p[1] + mvp.m[10] * p[2] + mvp.m[14];
            o.w = mvp.m[3] * p[0] + mvp.m[7] * p[1] + mvp.m[11] * p[2] + mvp.m[15];
            o.u = vv[6];
            o.v = vv[7];
            // world position (lighting + fog)
            float wx = model.m[0] * p[0] + model.m[4] * p[1] + model.m[8] * p[2] + model.m[12];
            float wy = model.m[1] * p[0] + model.m[5] * p[1] + model.m[9] * p[2] + model.m[13];
            float wz = model.m[2] * p[0] + model.m[6] * p[1] + model.m[10] * p[2] + model.m[14];
            float lr = 1, lg = 1, lb = 1;
            if (!unlit) {
                float nx = model.m[0] * nrm[0] + model.m[4] * nrm[1] + model.m[8] * nrm[2];
                float ny = model.m[1] * nrm[0] + model.m[5] * nrm[1] + model.m[9] * nrm[2];
                float nz = model.m[2] * nrm[0] + model.m[6] * nrm[1] + model.m[10] * nrm[2];
                float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }
                Vec3 L = norm({-lightDir_.x, -lightDir_.y, -lightDir_.z});
                float d = std::max(nx * L.x + ny * L.y + nz * L.z, 0.0f);
                lr = lg = lb = ambient_ + d;
                for (int li = 0; li < MAX_POINT_LIGHTS; li++) {
                    const PointLight& P = pointLights_[li];
                    if (P.radius <= 0) continue;
                    float tx = P.pos.x - wx, ty = P.pos.y - wy, tz = P.pos.z - wz;
                    float dist = std::sqrt(tx * tx + ty * ty + tz * tz);
                    float att = 1.0f - dist / P.radius;
                    if (att <= 0) continue;
                    att *= att;
                    float nd = std::max((nx * tx + ny * ty + nz * tz) /
                                        std::max(dist, 1e-4f), 0.0f);
                    lr += P.color.x * nd * att;
                    lg += P.color.y * nd * att;
                    lb += P.color.z * nd * att;
                }
                lr = std::min(lr, 1.6f);
                lg = std::min(lg, 1.6f);
                lb = std::min(lb, 1.6f);
            }
            o.r = r * lr * vv[8];
            o.g = g * lg * vv[9];
            o.b = b * lb * vv[10];
            o.a = vv[11];
            if (fogOn) {
                float vd = -(view_.m[2] * wx + view_.m[6] * wy +
                             view_.m[10] * wz + view_.m[14]);
                float f = (vd - fogStart_) / (fogEnd_ - fogStart_);
                o.fog = f < 0 ? 0 : (f > 1 ? 1 : f);
            } else {
                o.fog = 0;
            }
        }
        // near clip (polygon may grow to 4 verts → fan)
        PVert clipped[8];
        int n = clipNear(tri, 3, clipped);
        for (int k = 1; k + 1 < n; k++)
            rasterTri(clipped[0], clipped[k], clipped[k + 1], T, mode);
    }
}

void Gfx::drawMesh(int mesh, int tex, const Mat4& model, float r, float g,
                   float b) {
    if (mesh < 0 || mesh >= (int)meshes_.size()) return;
    const Mesh& m = meshes_[mesh];
    emitTriangles(m.v.data(), (int)m.v.size() / VERT_FLOATS, nullptr, 0, tex,
                  model, r, g, b, false, M_OPAQUE);
}

void Gfx::drawMeshLerp(int meshA, int meshB, float t, int tex,
                       const Mat4& model, float r, float g, float b) {
    if (meshA < 0 || meshA >= (int)meshes_.size() || meshB < 0 ||
        meshB >= (int)meshes_.size())
        return;
    const Mesh& a = meshes_[meshA];
    const Mesh& b2 = meshes_[meshB];
    if (a.v.size() != b2.v.size()) {
        std::fprintf(stderr,
                     "draw_lerp: meshes %d and %d have different vertex "
                     "counts (%zu vs %zu)\n",
                     meshA, meshB, a.v.size() / VERT_FLOATS,
                     b2.v.size() / VERT_FLOATS);
        return;
    }
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    emitTriangles(a.v.data(), (int)a.v.size() / VERT_FLOATS, b2.v.data(), t,
                  tex, model, r, g, b, false, M_OPAQUE);
}

void Gfx::billboard(int tex, Vec3 pos, float w, float h, float u0, float v0,
                    float u1, float v1) {
    Vec3 fwd = norm(cross(camUp_, camRight_));
    Mat4 model;
    model.m[0] = camRight_.x * w; model.m[1] = camRight_.y * w;
    model.m[2] = camRight_.z * w;
    model.m[4] = camUp_.x * h; model.m[5] = camUp_.y * h;
    model.m[6] = camUp_.z * h;
    model.m[8] = fwd.x; model.m[9] = fwd.y; model.m[10] = fwd.z;
    model.m[12] = pos.x; model.m[13] = pos.y; model.m[14] = pos.z;
    const float P[6][2] = {{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f},
                           {-.5f, -.5f}, {.5f, .5f},  {-.5f, .5f}};
    const float UV[6][2] = {{u0, v1}, {u1, v1}, {u1, v0},
                            {u0, v1}, {u1, v0}, {u0, v0}};
    float qv[6 * VERT_FLOATS];
    for (int i = 0; i < 6; i++) {
        float* o = qv + (size_t)i * VERT_FLOATS;
        o[0] = P[i][0]; o[1] = P[i][1]; o[2] = 0;
        o[3] = 0; o[4] = 0; o[5] = 1;
        o[6] = UV[i][0]; o[7] = UV[i][1];
        o[8] = o[9] = o[10] = o[11] = 1;
    }
    emitTriangles(qv, 6, nullptr, 0, tex, model, 1, 1, 1, true, M_OPAQUE);
}

void Gfx::shadowBlob(Vec3 pos, float radius, float alpha) {
    // flat XZ decal: dark disc, alpha-blended, depth-tested but not written
    const float P[6][2] = {{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f},
                           {-.5f, -.5f}, {.5f, .5f},  {-.5f, .5f}};
    const float UV[6][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 1}, {1, 0}, {0, 0}};
    float qv[6 * VERT_FLOATS];
    for (int i = 0; i < 6; i++) {
        float* o = qv + (size_t)i * VERT_FLOATS;
        o[0] = P[i][0]; o[1] = 0; o[2] = P[i][1];
        o[3] = 0; o[4] = 1; o[5] = 0;
        o[6] = UV[i][0]; o[7] = UV[i][1];
        o[8] = o[9] = o[10] = 0;         // black
        o[11] = alpha;
    }
    Mat4 model = mul(translate(pos.x, pos.y, pos.z),
                     scale(radius * 2, 1, radius * 2));
    emitTriangles(qv, 6, nullptr, 0, shadowTex_, model, 1, 1, 1, true,
                  M_DECAL);
}

// -------------------------------------------------------------------- 2D

int Gfx::loadTexture(const std::string& bmpPath, int* outW, int* outH) {
    SDL_Surface* raw = SDL_LoadBMP(bmpPath.c_str());
    if (!raw) {
        std::fprintf(stderr, "load_texture: %s\n", SDL_GetError());
        return -1;
    }
    SDL_Surface* s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_FreeSurface(raw);
    if (!s) return -1;
    Tex t;
    t.w = s->w;
    t.h = s->h;
    t.px.assign((uint8_t*)s->pixels,
                (uint8_t*)s->pixels + (size_t)s->w * s->h * 4);
    SDL_FreeSurface(s);
    // Retro color key: pure magenta (255,0,255) → transparent.
    bool keyed = false;
    for (size_t i = 0; i < t.px.size(); i += 4)
        if (t.px[i] == 255 && t.px[i + 1] == 0 && t.px[i + 2] == 255) {
            t.px[i + 3] = 0;
            keyed = true;
        }
    // Alpha-bleed: give transparent texels the RGB of their opaque
    // neighbors so bilinear sampling never pulls magenta into edges.
    if (keyed) {
        for (int pass = 0; pass < 4; pass++) {
            bool changed = false;
            std::vector<uint8_t> src = t.px;
            for (int y = 0; y < t.h; y++)
                for (int x = 0; x < t.w; x++) {
                    uint8_t* p = &t.px[((size_t)y * t.w + x) * 4];
                    if (p[3] != 0 || (p[0] != 255 || p[1] != 0 || p[2] != 255))
                        continue; // opaque, or already bled
                    int sr = 0, sg = 0, sb = 0, n = 0;
                    const int off[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (auto& o : off) {
                        int nx = x + o[0], ny = y + o[1];
                        if (nx < 0 || ny < 0 || nx >= t.w || ny >= t.h)
                            continue;
                        const uint8_t* q = &src[((size_t)ny * t.w + nx) * 4];
                        bool qMagenta =
                            q[0] == 255 && q[1] == 0 && q[2] == 255;
                        if (q[3] != 0 || !qMagenta) {
                            sr += q[0]; sg += q[1]; sb += q[2]; n++;
                        }
                    }
                    if (n) {
                        p[0] = (uint8_t)(sr / n);
                        p[1] = (uint8_t)(sg / n);
                        p[2] = (uint8_t)(sb / n);
                        changed = true;
                    }
                }
            if (!changed) break;
        }
    }
    textures_.push_back(std::move(t));
    if (outW) *outW = textures_.back().w;
    if (outH) *outH = textures_.back().h;
    return (int)textures_.size() - 1;
}

void Gfx::pushQuad(int tex, float x, float y, float w, float h, float u0,
                   float v0, float u1, float v1, float r, float g, float b,
                   float a) {
    Quad2D q;
    q.tex = tex;
    const float src[6][8] = {
        {x,     y,     u0, v0, r, g, b, a}, {x + w, y,     u1, v0, r, g, b, a},
        {x + w, y + h, u1, v1, r, g, b, a}, {x,     y,     u0, v0, r, g, b, a},
        {x + w, y + h, u1, v1, r, g, b, a}, {x,     y + h, u0, v1, r, g, b, a},
    };
    std::memcpy(q.v, src, sizeof src);
    quads_.push_back(q);
}

void Gfx::pushQuadRot(int tex, float cx, float cy, float w, float h,
                      float rot, float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a) {
    float c = std::cos(rot), s = std::sin(rot);
    auto px = [&](float dx, float dy, float* out) {
        out[0] = cx + dx * c - dy * s;
        out[1] = cy + dx * s + dy * c;
    };
    float hw = w / 2, hh = h / 2;
    float p0[2], p1[2], p2[2], p3[2];
    px(-hw, -hh, p0); px(hw, -hh, p1); px(hw, hh, p2); px(-hw, hh, p3);
    Quad2D q;
    q.tex = tex;
    const float src[6][8] = {
        {p0[0], p0[1], u0, v0, r, g, b, a}, {p1[0], p1[1], u1, v0, r, g, b, a},
        {p2[0], p2[1], u1, v1, r, g, b, a}, {p0[0], p0[1], u0, v0, r, g, b, a},
        {p2[0], p2[1], u1, v1, r, g, b, a}, {p3[0], p3[1], u0, v1, r, g, b, a},
    };
    std::memcpy(q.v, src, sizeof src);
    quads_.push_back(q);
}

void Gfx::rect(float x, float y, float w, float h, float r, float g, float b,
               float a) {
    pushQuad(whiteTex_, x, y, w, h, 0, 0, 1, 1, r, g, b, a);
}

void Gfx::sprite(int tex, float x, float y, float sx, float sy) {
    if (tex < 0 || tex >= (int)textures_.size()) return;
    const Tex& t = textures_[tex];
    pushQuad(tex, x, y, t.w * sx, t.h * sy, 0, 0, 1, 1, 1, 1, 1, 1);
}

void Gfx::spriteEx(int tex, float x, float y, float sx, float sy, float rot,
                   float r, float g, float b, float a) {
    if (tex < 0 || tex >= (int)textures_.size()) return;
    const Tex& t = textures_[tex];
    float w = t.w * sx, h = t.h * sy;
    float u0 = 0, u1 = 1, v0 = 0, v1 = 1;
    if (w < 0) { w = -w; u0 = 1; u1 = 0; }
    if (h < 0) { h = -h; v0 = 1; v1 = 0; }
    pushQuadRot(tex, x, y, w, h, rot, u0, v0, u1, v1, r, g, b, a);
}

void Gfx::spriteUV(int tex, float x, float y, float w, float h, float u0,
                   float v0, float u1, float v1) {
    if (tex < 0 || tex >= (int)textures_.size()) return;
    pushQuad(tex, x, y, w, h, u0, v0, u1, v1, 1, 1, 1, 1);
}

void Gfx::print(const char* text, float x, float y, float r, float g, float b,
                float a) {
    float cx = x;
    for (const char* p = text; *p; p++) {
        unsigned char c = (unsigned char)std::toupper((unsigned char)*p);
        if (c == '\n') {
            cx = x;
            y += 9;
            continue;
        }
        if (c >= 32 && c < 96 && c != 32) {
            int g8 = c - 32;
            float u0 = (g8 % 16) * 8 / 128.0f, v0 = (g8 / 16) * 8 / 32.0f;
            pushQuad(fontTex_, cx, y, 8, 8, u0, v0, u0 + 8 / 128.0f,
                     v0 + 8 / 32.0f, r, g, b, a);
        }
        cx += 8;
    }
}

// affine 2D triangle with alpha blending (no depth) — HUD compositing
void Gfx::raster2DTri(const float* v0, const float* v1, const float* v2,
                      const Tex* tex) {
    float minx = std::min({v0[0], v1[0], v2[0]});
    float maxx = std::max({v0[0], v1[0], v2[0]});
    float miny = std::min({v0[1], v1[1], v2[1]});
    float maxy = std::max({v0[1], v1[1], v2[1]});
    int x0 = std::max(0, (int)std::floor(minx));
    int x1 = std::min(SCREEN_W - 1, (int)std::ceil(maxx));
    int y0 = std::max(0, (int)std::floor(miny));
    int y1 = std::min(SCREEN_H - 1, (int)std::ceil(maxy));
    if (x0 > x1 || y0 > y1) return;

    float area = (v1[0] - v0[0]) * (v2[1] - v0[1]) -
                 (v1[1] - v0[1]) * (v2[0] - v0[0]);
    if (std::fabs(area) < 1e-6f) return;
    float invArea = 1.0f / area;

    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f;
        uint8_t* row = &fb_[((size_t)y * SCREEN_W + x0) * 4];
        for (int x = x0; x <= x1; x++, row += 4) {
            float px = x + 0.5f;
            float w0 = (v2[0] - v1[0]) * (py - v1[1]) -
                       (v2[1] - v1[1]) * (px - v1[0]);
            float w1 = (v0[0] - v2[0]) * (py - v2[1]) -
                       (v0[1] - v2[1]) * (px - v2[0]);
            float w2 = (v1[0] - v0[0]) * (py - v0[1]) -
                       (v1[1] - v0[1]) * (px - v0[0]);
            bool inPos = (w0 >= 0 && w1 >= 0 && w2 >= 0);
            bool inNeg = (w0 <= 0 && w1 <= 0 && w2 <= 0);
            if (!inPos && !inNeg) continue;
            float b0 = w0 * invArea, b1 = w1 * invArea, b2 = w2 * invArea;
            float u = b0 * v0[2] + b1 * v1[2] + b2 * v2[2];
            float v = b0 * v0[3] + b1 * v1[3] + b2 * v2[3];
            float t[4] = {1, 1, 1, 1};
            if (tex) sampleTex(tex, u, v, t);
            float R = t[0] * (b0 * v0[4] + b1 * v1[4] + b2 * v2[4]);
            float G = t[1] * (b0 * v0[5] + b1 * v1[5] + b2 * v2[5]);
            float B = t[2] * (b0 * v0[6] + b1 * v1[6] + b2 * v2[6]);
            float Aa = t[3] * (b0 * v0[7] + b1 * v1[7] + b2 * v2[7]);
            if (Aa <= 0.004f) continue;
            if (Aa > 1) Aa = 1;
            row[0] = (uint8_t)(std::min(R, 1.0f) * 255 * Aa + row[0] * (1 - Aa));
            row[1] = (uint8_t)(std::min(G, 1.0f) * 255 * Aa + row[1] * (1 - Aa));
            row[2] = (uint8_t)(std::min(B, 1.0f) * 255 * Aa + row[2] * (1 - Aa));
        }
    }
}

void Gfx::flush2D() {
    for (const Quad2D& q : quads_) {
        const Tex* t = texFor(q.tex);
        raster2DTri(q.v[0], q.v[1], q.v[2], t);
        raster2DTri(q.v[3], q.v[4], q.v[5], t);
    }
    quads_.clear();
}

void Gfx::endFrame() { flush2D(); }

void Gfx::screenshot(const std::string& bmpPath) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(
        (void*)fb_.data(), SCREEN_W, SCREEN_H, 32, SCREEN_W * 4,
        SDL_PIXELFORMAT_ABGR8888);
    if (s) {
        SDL_SaveBMP(s, bmpPath.c_str());
        SDL_FreeSurface(s);
        std::fprintf(stderr, "LANTERN_SHOT saved %s\n", bmpPath.c_str());
    }
}

} // namespace lt
