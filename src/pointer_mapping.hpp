#pragma once
#include "platform.hpp"
#include "render_target.hpp"
#include <cmath>

namespace lt {
// Match presentation exactly: window points -> backing pixels -> logical
// content. Render density changes framebuffer detail, never input geometry.
inline PlatformPointer mapPointer(float windowX, float windowY, bool inWindow,
                                  int windowW, int windowH,
                                  int outputW, int outputH, bool native) {
    PlatformPointer p;
    if (windowW <= 0 || windowH <= 0 || outputW <= 0 || outputH <= 0 ||
        !std::isfinite(windowX) || !std::isfinite(windowY)) return p;
    const auto d = presentationRect(outputW, outputH, native);
    if (d.w <= 0 || d.h <= 0) return p;
    const float px = windowX * (float(outputW) / float(windowW));
    const float py = windowY * (float(outputH) / float(windowH));
    p.x = std::clamp((px - float(d.x)) * (400.0f / float(d.w)), 0.0f, 399.0f);
    p.y = std::clamp((py - float(d.y)) * (240.0f / float(d.h)), 0.0f, 239.0f);
    p.inside = inWindow && windowX >= 0 && windowX < windowW &&
               windowY >= 0 && windowY < windowH &&
               px >= d.x && px < d.x + d.w && py >= d.y && py < d.y + d.h;
    return p;
}
} // namespace lt
