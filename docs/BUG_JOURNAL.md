# Bug Journal — lantern engine

Cheap-to-write, cheap-to-read, expensive-to-skip. `grep -i <symptom>` this
before reproducing anything.

## Patterns to scan for FIRST

- **Linear filtering + color-keyed textures = edge fringe.** Any pipeline
  that samples RGB of transparent texels must alpha-bleed at load time;
  alpha-test alone doesn't save you because RGB is sampled before the test.
- **Code generators must sanitize target-language syntax even inside
  comments** (`\` at end of line, `*/`, trigraphs). Fix the emitter, don't
  silence the warning.
- **Frame-capture hooks must read at the point the player sees** — after
  every deferred/batched stage (sprite batch, blit) has flushed, not before.

## Chronological log (newest first — 5 lines max each)

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
