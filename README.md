# lantern — a 3DS-class 2D/3D game engine

Make games that look and feel like the Nintendo 3DS generation: low-poly 3D
with per-vertex lighting, pixel-perfect 2D, one 400×240 logical screen. The
constraints are the aesthetic — nothing here is a filter.

**The renderer is ours, all the way down.** No GPU API: every pixel of the
frame is computed by the engine's own software rasterizer —
transform, clipping, perspective-correct rasterization, depth, bilinear
sampling, lighting, fog, blending. SDL is OS plumbing only (window, input,
audio device). Run with `LANTERN_FIXED_DT=1` for deterministic frame steps;
the real CPU framebuffer is what the engine screenshot-tests in CI.

- **2D and 3D in the same frame**: depth-tested gouraud-lit meshes, then
  batched sprites/text composited on top. The default framebuffer is 400×240;
  opt-in density renders up to 1600×960 while game coordinates stay fixed.
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
  8×8 font. BMP textures support explicit painted alpha and legacy magenta keys.
- **Audio**: 48 kHz 16-channel WAV mixer with looping.
- **Input**: keyboard + game controller merged into one namespace, held and
  edge-triggered, plus rumble; **single-point touch, 3DS-style** (mouse on
  desktop, real touch on iOS). **Save data**: binary-safe per-name storage.
- **Runs on iPhone/iPad**: the platform layer has a native iOS backend —
  Metal-presented (the rasterizer stays ours), UITouch, AVAudio, and
  GCController gamepads. `tools/build_ios_sim.sh [game_dir]` builds a
  simulator app with a **wick game bundled** (Lantern Night by default —
  wick has no JIT, so it's iOS-legal by construction). The new density/alpha
  paths still need native iOS presentation/input validation.
- Headroom to spare: ~10k lit mesh draw calls fit the 60 fps budget
  (`games/stress/`) — a real 3DS-class scene needs a few hundred.
- **The dev loop**: save `main.lua` while the engine runs and it hot-reloads;
  a Lua error shows an in-engine error screen (file:line) instead of
  crashing — fix the file, save, keep playing.
- Built-in verification: `LANTERN_SHOT=/path/prefix ./lantern <game>` runs
  60 frames (override with `LANTERN_SHOT_FRAME=N`), saves the physical
  framebuffer as BMP, exits — screenshot your game in CI.

## Compile-only Wick checks

```sh
./build/lantern check --json path/to/game
./build/lantern check --json path/to/source.wick
```

This command compiles with the real Lantern `lt.*` signatures and Wick builtins,
then exits. It runs before package extraction, SDL/audio initialization or game
loading. It never executes top-level statements, native functions, asset loads
or saves. A directory selects `main.wick`; an explicit source must have the
`.wick` extension. `.lant` checks are not supported in this increment.

Stdout contains one `lantern.check.v1` JSON object. Exit 0 means compilation
succeeded, 1 means a compiler error, and 2 means usage, source I/O or host failure.
Diagnostics include stable codes, phase, file, line and message; an unavailable
file/line is `null`. Paths are resolved through ancestor aliases, but a final
source symlink is rejected. Only regular files are read, with an 8 MiB limit.
Compiler nesting is bounded to 256 recursive parser entries and the constant
pool accepts indices 0–65535, rejecting the next entry before it can wrap.

The public `wick::check` API uses a temporary VM with native signature metadata
and numeric constants; it preserves the caller's program, globals, stack, heap
and RNG state. It is compile validation, not a sandbox for executing untrusted
Wick: runtime instruction, call and heap budgets, per-VM RNG and state inspection
are separate future work. Other existing bytecode operand limits are unchanged.
See [docs/WICK.md](docs/WICK.md#compile-only-validation) for the schema and API.

## Build & run

```sh
brew install sdl2 lua cmake pkg-config   # macOS; Linux: same packages
cmake -B build && cmake --build build -j8
./build/lantern games/room               # THE SHOP — ALBW-style interior
./build/lantern games/showcase           # LANTERN NIGHT — a complete mini-game
./build/lantern games/demo               # monastery-at-dusk feature demo
./build/lantern games/template           # minimal starter
./build/lantern_hello_c                  # the same engine from pure C
```

## Render density and painted transparency

`LANTERN_RENDER_SCALE` is read once at engine startup. Omit it for the existing
400×240 framebuffer, or set exactly `1`, `2`, `3` or `4` for 400×240, 800×480,
1200×720 or 1600×960 physical pixels. Empty, padded, signed and out-of-range
values are rejected before platform/framebuffer allocation.

```sh
LANTERN_RENDER_SCALE=2 ./build/lantern games/room
```

Game/UI coordinates and logical input remain 400×240; camera, collision logic
and atlas UVs do not change. More density can expose detail in higher-resolution
assets; it does not create missing source detail. Screenshots use the physical
dimensions. SDL and iOS presentation receive that physical size and pitch, but
native SDL/Retina, iOS/Metal, touch mapping and frame cadence need platform
validation for this increment. CPU regression results do not establish those
native behaviors.

For painted textures, use a 32-bit BI_BITFIELDS BMP with an explicit alpha mask,
such as BMP V4, containing straight RGBA. Every color is retained, including
opaque magenta. Bilinear sampling filters premultiplied RGB and alpha, then
returns straight RGB to the existing compositor so alpha is applied once.
Fully transparent RGB values cannot color the edge. Opaque sampling retains
the original arithmetic, nearest sampling is unchanged, and atlas samples
remain clamped to their selected cell. The existing 2D coverage cutoff of
0.004 remains: alpha 1/255 survives loading/sampling but is below that cutoff.

24-bit BMPs and 32-bit BGRX/BITFIELDS BMPs without an alpha mask keep the legacy
pure-magenta key and edge bleed. Existing games default to density 1.

Python 3 is needed for the renderer regression driver. With the normal build,
run `ctest --test-dir build -L renderer --output-on-failure`. The tests generate
synthetic fixtures, use the real BMP loader and software rasterizer and
replace OS presentation/input with deterministic stubs; no game art is needed.

**Lantern Night** (`games/showcase/`, ~200 lines of Lua) is the proof the API
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
([format spec](docs/PACKAGE.md)). Nested asset paths (`assets/foo.bmp`) are
supported; load paths refuse directory escape; package-mode screenshots
cannot write arbitrary host paths. Store packages are wick-only: a wick
game's only exits are the typed `lt.*` natives. Sell yours on the
[lantern store](https://famemu.aulenor.com/store/) — developers keep 85%.

**New: Wick 0.3 / Lantern 0.8.0** adds checked bit operations, hex/binary
literals, byte/word wrapping and register displays. Try
`./build/lantern games/bitlab`; see [the changelog](CHANGELOG.md).

## wick — lantern's own language

lantern ships its own scripting language: **wick** — Lua's size and feel
with the sharp edges designed out. Statically typed with `T?` optionals
(the "empty save file crashes the game" class of bug is a *compile error*),
**flat records** (`record Prop { x: num, y: num }`), locals-only, 0-based
indexing, strict bool conditions, typed engine bindings, deterministic
`rand()`, and GC that runs only between frames. The compiler + VM are
a small implementation in `wick/`, zero dependencies, same zlib license.

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

**Docs & blog:** [docs/WICK.md](docs/WICK.md) · [CHANGELOG](CHANGELOG.md) ·
public site [wick.famemu.aulenor.com](https://wick.famemu.aulenor.com/)
(reference + [blog](https://wick.famemu.aulenor.com/docs/blog/)).

Try it: `./build/lantern games/wicklab`, or **Lantern Night in wick** —
`./build/lantern games/showcase_wick` (side-by-side with the Lua build so
the languages stay comparable). CI: `tests/wick_test.sh` (semantics +
must-fail-to-compile + self-play), `tests/path_sandbox_test.sh`,
`tests/package_test.sh`.

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

- 400×240 is the logical screen contract. Physical density is bounded to 1–4
  and chosen at startup; arbitrary window-resolution rendering is unsupported.
- Textures are BMP, **bilinear-filtered** (the real 3DS look; the built-in
  font and the final integer upscale stay nearest). Explicit-alpha BMPs keep
  all RGBA colors; alpha-less legacy images key pure magenta (255,0,255).
  Keep assets appropriate for the selected density and memory budget.
- Lighting is per-vertex: tessellate ground planes (`lt.plane(12)`) if you
  want round light pools; keep meshes coarse if you want the faceted look.

Made by the famicom/famemu project — lantern is the platform's third engine
tier (NES, SNES, and now the 3DS generation), and KORA will be its flagship
game.
