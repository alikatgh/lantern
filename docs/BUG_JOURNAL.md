# Bug Journal — lantern engine

Cheap-to-write, cheap-to-read, expensive-to-skip. `grep -i <symptom>` this
before reproducing anything.

## Patterns to scan for FIRST

- **Game-relative loads must refuse `..` and absolute paths.** Joining
  `gameDir + "/" + userPath` is not a sandbox — path traversal leaves the
  package. Use a shared resolve-under-root helper; test with a wick fixture
  that must fail (`tests/path_sandbox_test.sh`).
- **`.lant` names may be nested (`assets/x.bmp`) but never `..`.** Pack
  recursively; extract creates parent dirs. Flat games still pack flat.
- **Level-sampled input misses sub-frame events.** Any input read as "is it
  down NOW" loses presses shorter than one frame (fast clicks, synthetic
  taps). Latch a sequence counter at the event source; edge-detect on the
  counter, not the level.
- **AVAudioEngine mixer inputs must be non-interleaved.** Connecting a node
  with an interleaved format throws kAudioUnitErr_FormatNotSupported at
  runtime. Give the render block its interleaved format at node init, but
  connect with initStandardFormatWithSampleRate (the engine converts).
- **Linear filtering + color-keyed textures = edge fringe.** Any pipeline
  that samples RGB of transparent texels must alpha-bleed at load time;
  alpha-test alone doesn't save you because RGB is sampled before the test.
- **Code generators must sanitize target-language syntax even inside
  comments** (`\` at end of line, `*/`, trigraphs). Fix the emitter, don't
  silence the warning.
- **Frame-capture hooks must read at the point the player sees** — after
  every deferred/batched stage (sprite batch, blit) has flushed, not before.

## Chronological log (newest first — 5 lines max each)

### 2026-07-14 — store path/pack sandbox + wick records
- Symptom: design critique — loads joined paths naively; packages flat-only
  so KORA `assets/` could not ship; parallel prop lists painful.
- Cause: no resolve-under-root; pack tool only read one directory level;
  language had no records.
- Fix: `path_sandbox.cpp`; recursive nested `.lant` names; `record` +
  OP_REC/FGET/FSET; kitchen props → `list<Prop>`; package-mode screenshot
  confined to extract dir.
- Lesson: security story must match natives; pack layout must match game
  paths; admit records only when a real table hurts (kitchen props).

### 2026-07-14 — touch test red on CI only (tests/touch_test.c)
- Symptom: center-tap asserted 200 but got 244 — only on the GitHub runner.
- Cause: the runner's display is smaller than 1200x720; SDL shrank the window, the frame letterboxed, and the test's hardcoded window coords no longer meant "center".
- Fix: measure the real window size; assert only letterbox-invariant points (center, corners).
- Lesson: never hardcode a requested window size in a test — displays clamp windows; assert at invariant points.

### 2026-07-14 — iOS: taps did nothing despite correct HUD coords (src/engine.cpp lt_touch_pressed)
- Symptom: simulator tap updated lt_touch_x/y but the touchdemo ball never moved.
- Cause: a synthetic tap's began+ended both landed between two frames — lt_touch_down never read 1, and _pressed edge-detected on that level.
- Fix: PlatformTouch.seq increments per touch-begin; _pressed compares seq to the frame-start snapshot.
- Lesson: level-sampled input misses sub-frame events; latch a sequence counter at the source.

### 2026-07-14 — iOS: boot crashed in AVAudioEngine connect (src/platform_ios.mm platAudioStart)
- Symptom: app aborted at launch, NSException kAudioUnitErr_FormatNotSupported (-10868).
- Cause: connected the source node to mainMixerNode with an INTERLEAVED float format; mixer inputs only accept the standard non-interleaved layout.
- Fix: render block keeps interleaved, connection uses initStandardFormatWithSampleRate; whole start wrapped in @try (audio failure = silent engine, never fatal).
- Lesson: AVAudioEngine converts render-block formats but rejects interleaved on connections — and audio init must be crash-isolated from boot.

### 2026-07-14 — v0.6: bilinear + color key = magenta edge fringe (src/gfx.cpp loadTexture)
- Symptom: after the software-rasterizer switch (bilinear default), keyed sprites (monk billboard) grew a pink outline.
- Cause: color keying zeroes ALPHA but leaves RGB magenta; bilinear blends the RGB of transparent neighbors into edge samples.
- Fix: alpha-bleed at load — dilate opaque neighbors' RGB into transparent texels (4 passes).
- Lesson: any keyed/premultiplied-less texture pipeline that turns on linear filtering MUST bleed edge colors.

### 2026-07-14 — font generator: `// \` comment swallowed the next glyph row (tools/gen_font.py)
- Symptom: clang `-Wcomment` "multi-line // comment" on the generated lantern_font.h.
- Cause: the backslash glyph's `// \` line-continues the comment, eating the `]` glyph's initializer and shifting every later glyph by one.
- Fix: generator names unprintable-in-comment chars ("backslash") instead of emitting them.
- Lesson: code GENERATORS must sanitize chars that are syntax in the target language even inside comments.

### 2026-07-14 — LANTERN_SHOT missed the 2D HUD layer (src/main.cpp)
- Symptom: verification screenshot showed the 3D scene but no HUD rects.
- Cause: screenshot read the FBO before endFrame(), where the sprite batch flushes.
- Fix: capture AFTER endFrame() (the FBO retains the completed frame post-blit).
- Lesson: a "verify by frames" hook must capture at the same pipeline point the player sees.
