# lantern — a 3DS-class 2D/3D game engine

Make games that look and feel like the Nintendo 3DS generation: low-poly 3D
with per-vertex lighting, pixel-perfect 2D, one 400×240 screen. The
constraints are the aesthetic — nothing here is a filter.

**The renderer is ours, all the way down.** No GPU API: every pixel of the
400×240 frame is computed by the engine's own software rasterizer —
transform, clipping, perspective-correct rasterization, depth, bilinear
sampling, lighting, fog, blending. SDL is OS plumbing only (window, input,
audio device). The rasterizer is bit-identical everywhere; run with
`LANTERN_FIXED_DT=1` and whole game sessions become deterministic too —
that combination is what lets the engine screenshot-test itself in CI.

- **2D and 3D in the same frame**: depth-tested gouraud-lit meshes, then
  batched sprites/text composited on top, all at 400×240, integer-upscaled.
- **Small, learnable API** (~40 calls) in two flavors: **Lua** (no compiler
  needed — a game is a folder with `main.lua`) and a **C ABI**
  ([include/lantern.h](include/lantern.h)) for C/C++ games.
- Meshes: primitives (cube/plane/sphere/cylinder/cone), custom vertices
  (pos/normal/uv/color), **Wavefront OBJ loading**.
- Lighting the PICA200 way: 1 directional + 4 point lights, per-vertex;
  linear **fog**; alpha-tested transparency (no sorting, hard retro edges).
- **Billboards** — camera-facing sprites in the 3D scene (the
  *A Link Between Worlds* character trick) — plus **grounding blob
  shadows** (`lt_shadow`) and **keyframe mesh animation**
  (`lt_draw_mesh_lerp`, Quake-style tweening).
- 2D: sprites (rotation/tint/flip), atlas sub-rects for tilemaps, a built-in
  8×8 font. BMP textures with magenta color-key transparency.
- **Audio**: 48 kHz 16-channel WAV mixer with looping.
- **Input**: keyboard + game controller merged into one namespace, held and
  edge-triggered, plus rumble; **single-point touch, 3DS-style** (mouse on
  desktop, real touch on iOS). **Save data**: binary-safe per-name storage.
- **Runs on iPhone/iPad**: the platform layer has a native iOS backend —
  Metal-presented (the rasterizer stays ours), UITouch, AVAudio, and
  GCController gamepads. `tools/build_ios_sim.sh [game_dir]` builds a
  simulator app with a **wick game bundled** (Kora Night by default —
  wick has no JIT, so it's iOS-legal by construction); frames render
  byte-identical to macOS.
- Headroom to spare: ~10k lit mesh draw calls fit the 60 fps budget
  (`games/stress/`) — a real 3DS-class scene needs a few hundred.
- **The dev loop**: save `main.lua` while the engine runs and it hot-reloads;
  a Lua error shows an in-engine error screen (file:line) instead of
  crashing — fix the file, save, keep playing.
- Built-in verification: `LANTERN_SHOT=/path/prefix ./lantern <game>` runs
  60 frames (override with `LANTERN_SHOT_FRAME=N`), saves the real 400×240
  framebuffer as BMP, exits — screenshot your game in CI.

## Build & run

```sh
brew install sdl2 lua cmake pkg-config   # macOS; Linux: same packages
cmake -B build && cmake --build build -j8
./build/lantern games/room               # THE SHOP — ALBW-style interior
./build/lantern games/showcase           # KORA NIGHT — a complete mini-game
./build/lantern games/demo               # monastery-at-dusk feature demo
./build/lantern games/template           # minimal starter
./build/lantern_hello_c                  # the same engine from pure C
```

**Kora Night** (`games/showcase/`, ~200 lines of Lua) is the proof the API
holds together as a game: the four point lights are the mechanic (keep the
butter lamps lit through a windy night), the player is a billboard, fog is
the night, audio/rumble are feedback, and the hi-score persists. Title →
play → results, all in one file.

## Distribute

`bash tools/make_dist.sh` produces `dist/lantern-<ver>-macos-<arch>.tar.gz`
(~1.2 MB): the engine binary with Lua statically linked and SDL bundled —
recipients need no homebrew, no compiler. It refuses to package itself
unless the bundled binary actually renders a frame.

**Games ship as `.lant` packages** — one CRC-checked file made with
`lantern_pack mygame/ mygame.lant` and played with `lantern mygame.lant`
([format spec](docs/PACKAGE.md)). Store packages are wick-only, which is a
security guarantee, not a preference: a wick game's only exits are the
typed `lt.*` natives, so distributed games can't touch files or the
network. Sell yours on the [lantern store](https://famemu.aulenor.com/store/)
— developers keep 85%.

## wick — lantern's own language

lantern ships its own scripting language: **wick** — Lua's size and feel
with the sharp edges designed out. Statically typed with `T?` optionals
(the "empty save file crashes the game" class of bug is a *compile error*),
locals-only (typos can't create silent globals), 0-based indexing, strict
bool conditions, typed engine bindings (wrong `lt.draw` args fail at
compile time, on the error screen), deterministic `rand()`, and GC that
runs only between frames. The compiler + VM are ~2k lines in `wick/`,
zero dependencies, same zlib license.

A game is a folder with `main.wick` (the host prefers it over `main.lua`):

```wick
let best = num(lt.load_save("best") ?? "") ?? 0   // optionals, by force

fn update(dt: num) {
  if lt.pressed("z") { best = best + 1  lt.save("best", str(best)) }
}
fn draw() {
  lt.clear(0.1, 0.1, 0.2)
  lt.print("BEST " + str(best), 4, 4, 1, 1, 1, 1)
}
```

Full language reference: [docs/WICK.md](docs/WICK.md). Try it:
`./build/lantern games/wicklab`, or play the real thing:
`./build/lantern games/showcase_wick` — **Kora Night ported to wick**,
kept as a separate project beside the Lua build (`games/showcase`) on
purpose: the two evolve together so the languages stay comparable on the
same game. Test suite: `tests/wick_test.sh` (runs in CI — including eight
programs that MUST fail to compile, plus the wick Kora Night self-play).

## Make a game (Lua)

Copy `games/template/`, edit `main.lua`. Define `update(dt)` and `draw()`;
the engine calls them at 60 fps. That's the whole model:

```lua
local sphere = lt.sphere(14)
function draw()
  lt.clear(0.1, 0.1, 0.2)
  lt.fog(6, 25, 0.1, 0.1, 0.2)
  lt.camera(0, 2, 5, 0, 0, 0, 55)
  lt.draw(sphere, 0, 0, 0, 0, lt.time(), 0, 1, 1, 1, 0.9, 0.5, 0.4)
  lt.print("HELLO", 4, 4, 1, 1, 1, 1)
end
```

The full Lua API table is in [docs/ENGINE.md](docs/ENGINE.md); every call has
a 1:1 `lt_*` twin in [include/lantern.h](include/lantern.h) for C games
(link `lantern_core`, see `games/hello_c/main.c`).

## Rules of the road

- 400×240 is inviolable — there is no way to render at window resolution,
  and that's the point.
- Textures are BMP, **bilinear-filtered** (the real 3DS look; the built-in
  font and the final integer upscale stay nearest). Pure magenta
  (255,0,255) is transparent, with automatic edge bleed so filtering never
  fringes. Keep them small (≤256×256 in spirit).
- Lighting is per-vertex: tessellate ground planes (`lt.plane(12)`) if you
  want round light pools; keep meshes coarse if you want the faceted look.

Made by the famicom/famemu project — lantern is the platform's third engine
tier (NES, SNES, and now the 3DS generation), and KORA will be its flagship
game.
