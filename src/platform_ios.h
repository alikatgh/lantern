// platform_ios.h — the tiny glue surface between the iOS app host
// (ios/main.mm) and the iOS backend (platform_ios.mm). The host owns the
// UIKit lifecycle; the backend owns everything platform.hpp promises.
#pragma once
#ifdef __OBJC__
@class CAMetalLayer;

namespace lt {

// Must be called before lt_boot: the layer the backend presents into.
// The host keeps ownership and keeps drawableSize in sync with layout.
void iosSetMetalLayer(CAMetalLayer* layer);

// Touch forwarding, in DRAWABLE PIXELS (view points x contentScaleFactor).
void iosTouchBegan(float px, float py);
void iosTouchMoved(float px, float py);
void iosTouchEnded(void);

} // namespace lt
#endif
