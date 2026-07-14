// lantern_math.h — minimal column-major mat4/vec3, OpenGL conventions.
#pragma once
#include <cmath>

namespace lt {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 norm(Vec3 v) {
    float l = std::sqrt(dot(v, v));
    if (l < 1e-8f) return {0, 0, 0};
    return {v.x / l, v.y / l, v.z / l};
}

// Column-major: m[col*4 + row], matching glUniformMatrix4fv(transpose=GL_FALSE).
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}

inline Mat4 translate(float x, float y, float z) {
    Mat4 r;
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

inline Mat4 scale(float x, float y, float z) {
    Mat4 r;
    r.m[0] = x; r.m[5] = y; r.m[10] = z;
    return r;
}

inline Mat4 rotateX(float a) {
    Mat4 r; float c = std::cos(a), s = std::sin(a);
    r.m[5] = c; r.m[9] = -s; r.m[6] = s; r.m[10] = c;
    return r;
}
inline Mat4 rotateY(float a) {
    Mat4 r; float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[8] = s; r.m[2] = -s; r.m[10] = c;
    return r;
}
inline Mat4 rotateZ(float a) {
    Mat4 r; float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[4] = -s; r.m[1] = s; r.m[5] = c;
    return r;
}

inline Mat4 perspective(float fovyDeg, float aspect, float znear, float zfar) {
    Mat4 r;
    float f = 1.0f / std::tan(fovyDeg * 3.14159265f / 360.0f);
    r.m[0] = f / aspect; r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar); r.m[11] = -1;
    r.m[14] = (2 * zfar * znear) / (znear - zfar); r.m[15] = 0;
    return r;
}

inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = norm(sub(target, eye));
    Vec3 s = norm(cross(f, up));
    // view direction parallel to up (straight up/down camera): pick any
    // orthogonal basis instead of collapsing to zero
    if (dot(s, s) < 1e-10f) s = norm(cross(f, Vec3{0, 0, 1}));
    if (dot(s, s) < 1e-10f) s = {1, 0, 0}; // f was ±Z too (can't happen with
                                           // both, but stay total)
    Vec3 u = cross(s, f);
    Mat4 r;
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
}

} // namespace lt
