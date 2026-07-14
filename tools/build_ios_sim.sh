#!/bin/bash
# build_ios_sim.sh — build the lantern touchdemo as an iPhone-simulator app.
#
# Built to catch: "the engine only compiles on desktop" rot. It compiles
# the whole engine core + the native iOS backend (no SDL, no Lua) with the
# iphonesimulator SDK and assembles a runnable .app. It does NOT catch:
# device-only issues (code signing, thermal, real-hardware Metal).
#
# Usage: tools/build_ios_sim.sh          (from the repo root)
# Output: build-ios/Lantern.app
set -euo pipefail
cd "$(dirname "$0")/.."

SDK=iphonesimulator
TARGET=arm64-apple-ios15.0-simulator
OUT=build-ios
APP="$OUT/Lantern.app"
CXXFLAGS=(-std=c++20 -O2 -Wall -Wextra -target "$TARGET" -Isrc -Iinclude)
CFLAGS=(-std=c99 -O2 -Wall -Wextra -target "$TARGET" -Iinclude
        -DLANTERN_NO_MAIN)

mkdir -p "$OUT/obj" "$APP"

echo "== compiling engine core =="
for f in gfx obj audio engine; do
    xcrun -sdk $SDK clang++ "${CXXFLAGS[@]}" -c "src/$f.cpp" \
        -o "$OUT/obj/$f.o"
done

echo "== compiling iOS backend + host =="
xcrun -sdk $SDK clang++ "${CXXFLAGS[@]}" -fobjc-arc -c src/platform_ios.mm \
    -o "$OUT/obj/platform_ios.o"
xcrun -sdk $SDK clang++ "${CXXFLAGS[@]}" -fobjc-arc -c ios/main.mm \
    -o "$OUT/obj/host.o"

echo "== compiling touchdemo =="
xcrun -sdk $SDK clang "${CFLAGS[@]}" -c games/touchdemo/main.c \
    -o "$OUT/obj/touchdemo.o"

echo "== linking =="
xcrun -sdk $SDK clang++ -target "$TARGET" "$OUT"/obj/*.o -o "$APP/Lantern" \
    -framework UIKit -framework Metal -framework QuartzCore \
    -framework AVFoundation -framework Foundation -framework CoreGraphics

cp ios/Info.plist "$APP/Info.plist"
codesign --force --sign - "$APP" >/dev/null 2>&1 || true

echo "== done: $APP =="
echo "run it:  xcrun simctl boot 'iPhone 17 Pro' (once)"
echo "         xcrun simctl install booted $APP"
echo "         xcrun simctl launch booted com.aulenor.lantern.touchdemo"
