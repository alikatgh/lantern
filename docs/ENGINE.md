# LANTERN — a 3DS-class 2D/3D game engine

*Working name: **lantern** (the butter lamps of KORA's monastery; rename is a
one-line grep). Decided 2026-07-14 with the user:*

| Decision | Choice |
|---|---|
| Core stack | **C++20 + SDL2 + OpenGL 3.3 core** (Metal/Vulkan backends later) |
| Developer API | **C ABI engine core + Lua 5.5 gameplay scripting** |
| Graphics identity | **Authentic Nintendo-3DS-class constraints** (not "inspired by") |
| First game | **KORA**, ported from the Godot 4 build (`kora/godot/`) |

## Why this engine exists

The famemu platform vision (see `famicom-rf-hackrf-decoder/docs/PLATFORM.md`)
is engines-as-a-platform funded by content. lantern is the third engine tier:

- NES engine (nestopia, clean-room later) — 8-bit content
- SNES engine (snes9x, clean-room later) — 16-bit content
- **lantern — "the 3DS generation": low-poly 3D + pixel-perfect 2D**

Unlike the first two it is not an emulator: it's an **original engine we give
to game developers**, with our own KORA as the flagship title proving it.

## The graphics identity — constraints are the aesthetic

Same philosophy as the RF chain: the look *emerges* from real constraints,
nothing is a filter.

- **Internal render target: 400×240** (the 3DS top screen), integer-upscaled
  to the window with nearest filtering. All rendering — 2D and 3D — happens at
  this resolution. Optional second "bottom screen" context later (240×320? no
  — 320×240) is a *maybe*, not v1.
- **Per-vertex (gouraud) lighting** as the default lighting model — the
  PICA200 idiom. No per-pixel PBR. Hard per-face normals + vertex color are
  first-class citizens.
- **Small budgets** enforced culturally, not by the API: ~a few thousand
  triangles a scene, 256×256 max textures, nearest-filtered pixel-art
  textures encouraged.
- **2D and 3D coexist per frame**: 3D scene (depth-tested) then 2D
  sprites/HUD composited on top at native 400×240 pixels.

## Architecture

```
engine/
  include/lantern.h      ← the C ABI (what non-Lua devs/hosts link against)
  src/
    main.cpp             ← SDL2 host: window, GL context, main loop, Lua VM
    gfx.cpp / gfx.hpp    ← renderer: 400×240 FBO, sprite batch, mesh pipeline
    lantern_math.h       ← mat4/vec3 (column-major, GL conventions)
  games/
    demo/main.lua        ← hello-world scene (spinning stupa, HUD)
  docs/ENGINE.md         ← this file
```

- **Host** owns SDL, GL and the Lua state. A game is a directory with a
  `main.lua` defining `update(dt)` and `draw()`.
- **Renderer**: one offscreen FBO (400×240, RGBA8 + depth). Mesh draws are
  immediate (depth on). Sprite/rect calls accumulate in a batch flushed after
  `draw()` returns (depth off, alpha blend) — HUD always composites over 3D.
  The FBO blits to the window with integer scaling, letterboxed.
- **Lua API** (`lt.*`) is intentionally LÖVE/PICO-8-small. The C ABI in
  `lantern.h` will expose the same surface for C/C++ games; Lua bindings are
  its first consumer (dogfooding the ABI is a v0.2 refactor — v0.1 binds
  internals directly to get KORA moving).

## Lua API v0.3

```lua
-- frame:    engine calls update(dt) then draw() at 60 fps
-- 3D scene
lt.clear(r,g,b)                        -- also clears depth
lt.camera(ex,ey,ez, tx,ty,tz [,fov])   -- 3D camera (defaults persist)
lt.light(dx,dy,dz [,ambient])          -- directional light, per-vertex
lt.point_light(i, x,y,z, radius [,r,g,b]) -- 4 slots; radius<=0 = off
lt.fog(start, end_, r,g,b)             -- linear by view depth; end<=start off
-- meshes (vertex = pos3 normal3 uv2 rgba4 — 12 floats)
m = lt.cube()                          -- also: lt.plane(segs), lt.sphere(seg),
                                       --       lt.cylinder(seg), lt.cone(seg)
m = lt.mesh{...}                       -- custom, 12 floats/vertex
m = lt.load_mesh("model.obj")          -- Wavefront OBJ (flat-normal fallback)
lt.draw(m, x,y,z, rx,ry,rz, sx,sy,sz [,r,g,b [,tex]])
lt.billboard(t, x,y,z, w,h [,u0,v0,u1,v1]) -- camera-facing, fullbright, fogged
-- 2D (composites over 3D; 400×240 pixels, y-down)
lt.rect(x,y,w,h, r,g,b [,a])
t = lt.load_texture("img.bmp")         -- magenta (255,0,255) = transparent
lt.sprite(t, x,y [,sx,sy])
lt.sprite_ex(t, cx,cy [,sx,sy, rot, r,g,b,a]) -- center, rotate, tint, flip(±s)
lt.sprite_uv(t, x,y,w,h, u0,v0,u1,v1)  -- atlas sub-rect (tilemaps)
lt.print("TEXT", x,y [,r,g,b,a])       -- built-in 8x8 font, 8px advance
-- audio (48 kHz stereo, 16 channels)
s = lt.load_sound("bell.wav")
ch = lt.play(s [,volume, loop])        -- returns channel (-1 if none free)
lt.stop(ch)  ·  lt.volume(v)           -- master 0..1
-- input / misc
lt.key(name)  ·  lt.pressed(name)      -- held / went-down-this-frame
lt.gamepad()                           -- pad connected?
lt.quit()                              -- request a clean exit
lt.escape_quits(false)                 -- take over the Escape key (pause menus)
lt.save(name, str)  ·  lt.load_save(name) -- binary-safe; nil if missing
lt.screenshot("out.bmp")  ·  lt.time()
```

The same surface in C is `include/lantern.h` (`lt_*`); `games/hello_c/`
is a complete pure-C game against it, and the Lua host itself is an ABI
client (`src/main.cpp` — dogfooding). The built-in font is our own art,
ported from Rocket Rush's `make_chr.py` via `tools/gen_font.py`.
`games/template/` is the copy-me starter.

## Verification culture (day one)

`LANTERN_SHOT=/path/prefix ./lantern <game>` runs 60 frames headlessly-ish,
saves the 400×240 FBO as `<prefix>.bmp`, and exits 0. Same pattern as
`KORA_SHOT` in the Godot build and `dump_ppm` in Rocket Rush: **verify by
rendered frames, never by "it compiled".**

## Roadmap

- **v0.1 ✅ (2026-07-14)** — buildable core: window, 400×240 FBO + integer
  upscale, sprite batch, gouraud mesh pipeline, Lua loop, demo, LANTERN_SHOT.
- **v0.2 ✅ (2026-07-14)** — the C ABI (`lantern_core` static lib +
  `lantern.h`, Lua host is a client, `hello_c` proves pure C); textured
  meshes + `lt.mesh`; built-in 8×8 font (`lt.print`) + `sprite_uv`;
  SDL_GameController input merged into `lt.key` with hotplug.
- **v0.3 ✅ (2026-07-14) — engine completeness** (user directive: engine
  first, port later): OBJ loading; vertex format grew RGBA color (12
  floats); 4 point lights + fog + alpha-test (the PICA200 set); billboards;
  primitives (plane/sphere/cylinder/cone); 16-channel WAV mixer; save data;
  `sprite_ex` (rotate/tint/flip); `pressed` edges; magenta color-key;
  `lt.screenshot`; README + `games/template/` starter. All frame-verified;
  audio + saves verified by scripted roundtrip games.
- **v0.4 ✅ core (2026-07-14)** — the dev loop: **Lua hot-reload** (save
  main.lua while the engine runs → live reload, verified mid-run) and an
  **in-engine error screen** (Lua errors render with file:line instead of
  crashing; fix + save resumes). `LANTERN_SHOT_FRAME=N` controls the
  verification-capture frame. Follow-up same day: gamepad **rumble**
  (`lt.rumble(low, high, ms)`), uniform-location caching (no per-draw
  string lookups), `LANTERN_NOVSYNC=1` for benchmarks, and `games/stress/`
  — measured **~10,300 gouraud-lit mesh draw calls inside the 16.6 ms
  budget** on the dev machine (each call = full uniform upload + draw; a
  real 3DS scene is a few hundred). **Binary distribution shipped**:
  `tools/make_dist.sh` builds a relocatable macOS bundle (static Lua,
  bundled SDL2+SDL3 rewired to `@executable_path`, self-verifying — it
  fails unless the packaged binary renders a frame), ~1.2 MB tar.gz.
  Still open for the public release: Linux/Windows builds (needs CI infra).
- **v0.5 — KORA arrives** (was v0.3; deferred by user directive): tilemap/
  room-builder mirroring `kora/godot/world.gd`, dialogue, then the 3D
  diorama layer (ALBW framing: top-down camera, low-poly architecture,
  billboard characters — the engine already has every piece this needs).
- **v0.5-pre ✅ (2026-07-14) — showcase game**: `games/showcase/` (KORA
  NIGHT) — a complete title→play→results mini-game in ~200 lines where the
  4 point lights are the core mechanic; verified by a deterministic
  self-playing round (`SHOWCASE_AUTO=1`) captured at all three states, with
  the hi-score save confirmed on disk. Dist rebuilt as 0.5.0 (1.3 MB) with
  the showcase included.
- **v0.6 ✅ (2026-07-14) — OWN THE PIXELS.** User directive: "we need to
  control everything." OpenGL is GONE — the renderer is now **our own
  software rasterizer** (`gfx.cpp`): vertex transform, near-plane clipping,
  perspective-correct edge-function rasterization, depth buffer, **bilinear
  texture sampling** (the actual 3DS look — ALBW is soft, not pixelated;
  nearest stays for the font and the presentation upscale), per-vertex
  lighting, fog, alpha test/blend — every pixel computed by our C++. SDL is
  presentation plumbing only (window/input/audio device; frame goes up as
  one streaming texture). Deterministic output across machines. New ALBW
  capabilities: **`lt_shadow`** grounding blobs (depth-tested decal, no
  z-write) and **`lt_draw_mesh_lerp`** Quake-style keyframe tweening.
  Alpha-bleed at texture load kills magenta fringing under bilinear.
  Honest perf: ~12,400 small-cube draws (~150k lit vertices) inside
  16.6 ms in pure software on the dev machine; fill is cheap at 96k pixels.
- **v0.6.1 ✅ (2026-07-14) — the ALBW room** (`games/room/`): the proof
  against the user's reference screenshot. A warm wooden shop interior —
  generated plank/wall/carpet textures, red furniture, bed, barrels,
  plants — near-overhead camera, a cuboid-built hero **keyframe-animated
  via `lt.draw_lerp`** (walks, faces travel direction), a purple
  shopkeeper, blob shadows under both, two warm point lights, hearts +
  item-panel HUD. `ROOM_AUTO=1` self-walks for LANTERN_SHOT verification.
  Dist 0.6.1 includes it.
- **v0.6.2 ✅ (2026-07-14) — max-review fix batch.** All 15 confirmed
  findings from the full-engine adversarial review fixed and regression-
  verified (report: famicom docs/audits/2026-07-14-lantern-max-review.md):
  crash/UB class (0-byte saves, empty-WAV loop OOB, negative-color casts,
  boot-failure leaks, pre-init draws, straight-down camera), rendering
  (shared-edge double blend via boundary-ownership fill rule, atlas-safe
  bilinear clamping, cone normals), and contracts (LANTERN_SHOT/_FRAME +
  new **LANTERN_FIXED_DT** deterministic mode are engine-owned for every
  host; `lt.quit`/`lt.escape_quits`; `lt_resources_reset` makes hot-reload
  leak-free). Plus: cached view-projection, hoisted light normalization,
  nearest white texture, dead-code removal, site CSS rule compliance.
  Fixed-dt determinism verified by byte-identical repeated captures.
- **v1.0** — public developer release.

## Porting notes: KORA (Godot → lantern)

Source of truth: `kora/godot/` (1,080 lines GDScript; the SNES build stays
the retro demake). Mapping:

| Godot | lantern |
|---|---|
| `world.gd` ASCII map + cfg → tiles/props/NPCs | same data, a Lua room-builder module |
| `player.gd` 4-dir walk, feet collision box | Lua, identical constants |
| `dialog.gd` portrait + typewriter | Lua + sprite font |
| `overlay.gd` day/night tint, memory lines | 2D tinted rects + text layer |
| 480×270 logical res | **400×240** — re-tune map paddings, not art (16px tiles fit 25×15) |
| `KORA_SHOT` screenshot battery | `LANTERN_SHOT` battery |

## Rules for contributors (us, tonight)

- The 400×240 target is inviolable. No rendering at window resolution.
- Every feature lands with a demo-scene exercise and a LANTERN_SHOT check.
- Bug lessons go to `docs/BUG_JOURNAL.md` (repo root) same-commit.
- UI work follows `~/.claude/UI_DESIGN_RULES.md` where applicable.
