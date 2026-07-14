#!/bin/bash
# make_dist.sh — build a relocatable lantern bundle for macOS.
#
# Produces dist/lantern-<version>-macos-<arch>/ with:
#   lantern            (Lua statically linked; SDL2 rewired to ./lib)
#   lib/               (libSDL2 + libSDL3 — sdl2-compat dlopens SDL3 via
#                       @loader_path, so they just sit together)
#   games/  include/  README.md  docs/
# ...then verifies the packaged binary by rendering a frame from the bundle.
#
# Usage: bash tools/make_dist.sh [version]     (from engine/)
set -euo pipefail

VERSION="${1:-0.4.0}"
ARCH="$(uname -m)"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dist/lantern-$VERSION-macos-$ARCH"
BUILD="$ROOT/build-dist"

echo "== configure + build (static Lua) =="
cmake -B "$BUILD" -S "$ROOT" -DCMAKE_BUILD_TYPE=Release \
      -DLANTERN_STATIC_LUA=ON >/dev/null
cmake --build "$BUILD" -j8 | tail -1

echo "== assemble bundle =="
rm -rf "$OUT"
mkdir -p "$OUT/lib" "$OUT/docs"
cp "$BUILD/lantern" "$OUT/"
cp -R "$ROOT/games" "$OUT/games"
cp -R "$ROOT/include" "$OUT/include"
cp "$ROOT/README.md" "$OUT/"
cp "$ROOT/docs/ENGINE.md" "$OUT/docs/"

echo "== bundle dylibs =="
SDL2_SRC="$(otool -L "$OUT/lantern" | awk '/libSDL2/ {print $1}')"
cp "$SDL2_SRC" "$OUT/lib/"
SDL2_NAME="$(basename "$SDL2_SRC")"
# sdl2-compat finds SDL3 via @loader_path/libSDL3.dylib — put it beside it
SDL3_SRC="$(ls /opt/homebrew/opt/sdl3/lib/libSDL3.[0-9]*.dylib | head -1)"
cp "$SDL3_SRC" "$OUT/lib/libSDL3.dylib"
chmod u+w "$OUT/lib/"*.dylib

echo "== rewire install names =="
install_name_tool -change "$SDL2_SRC" \
    "@executable_path/lib/$SDL2_NAME" "$OUT/lantern" 2>/dev/null
install_name_tool -id "@rpath/$SDL2_NAME" "$OUT/lib/$SDL2_NAME" 2>/dev/null
install_name_tool -id "@rpath/libSDL3.dylib" "$OUT/lib/libSDL3.dylib" \
    2>/dev/null
codesign -f -s - "$OUT/lib/$SDL2_NAME" "$OUT/lib/libSDL3.dylib" \
    "$OUT/lantern" 2>/dev/null

echo "== verify: no homebrew paths left in the binary =="
if otool -L "$OUT/lantern" | grep -q /opt/homebrew; then
    echo "FAIL: binary still references /opt/homebrew:" >&2
    otool -L "$OUT/lantern" >&2
    exit 1
fi

echo "== verify: packaged binary renders a frame =="
SHOT="$(mktemp -d)/dist_verify"
(cd "$OUT" && LANTERN_SHOT="$SHOT" ./lantern games/template >/dev/null 2>&1)
if [ ! -s "$SHOT.bmp" ]; then
    echo "FAIL: packaged binary produced no frame" >&2
    exit 1
fi
echo "frame OK: $SHOT.bmp"

TAR="$ROOT/dist/lantern-$VERSION-macos-$ARCH.tar.gz"
tar -czf "$TAR" -C "$ROOT/dist" "$(basename "$OUT")"
echo "== done: $TAR ($(du -h "$TAR" | cut -f1)) =="
