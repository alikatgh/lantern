#pragma once
#include <algorithm>
#include <cstddef>

namespace lt {
constexpr int MAX_RENDER_SCALE = 10;
constexpr int MAX_FRAME_W = 4000, MAX_FRAME_H = 2400;
constexpr size_t MAX_FRAME_PIXELS = size_t(MAX_FRAME_W) * MAX_FRAME_H;

// Exact 5:3 aspect in physical pixels; native mode also permits targets below
// the logical dimensions. All arithmetic follows bounded positive validation.
inline bool validFrameSize(int w, int h) {
    return w >= 5 && w <= MAX_FRAME_W && h >= 3 && h <= MAX_FRAME_H &&
           w % 5 == 0 && h == (w / 5) * 3;
}
inline bool validFrameLayout(int w, int h, int pitch) {
    return validFrameSize(w, h) && pitch == w * 4;
}
inline bool nativeFrameSize(int outputW, int outputH, int& w, int& h) {
    if (outputW < 5 || outputH < 3) return false;
    const int units = std::min({outputW / 5, outputH / 3, MAX_FRAME_W / 5});
    w = units * 5; h = units * 3;
    return validFrameSize(w, h);
}
struct PresentRect { int x = 0, y = 0, w = 0, h = 0; };
inline PresentRect presentationRect(int outputW, int outputH, bool native) {
    if (outputW <= 0 || outputH <= 0) return {};
    const int units = native ? std::min(outputW / 5, outputH / 3)
                             : std::max(1, std::min(outputW / 400, outputH / 240)) * 80;
    const int w = units * 5, h = units * 3;
    return {(outputW - w) / 2, (outputH - h) / 2, w, h};
}
} // namespace lt
