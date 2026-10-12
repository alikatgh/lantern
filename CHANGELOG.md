# Changelog — lantern & wick

All notable changes to the engine and its language. Format inspired by
[Keep a Changelog](https://keepachangelog.com/); versioning is informal
while the stack is pre-1.0 (`engine=0.7` in `.lant` manifests).

## [Unreleased]

### Optional painted 2D compositor

- Add strict `LANTERN_PAINTED_2D=1` and optional `LANTERN_RENDER_WORKERS=1..8`,
  plus additive `painted-2d-v1` build-info metadata. Legacy rendering remains
  the default. Malformed settings fail before platform allocation.
- Expose the active mode and actual worker count through typed C/Wick getters,
  including serial fallback and unbooted state, for agents and diagnostics.
- Sample axis-aligned painted images directly, retaining alpha-weighted
  bilinear filtering, clamped atlas cells, tint and painter order. Opaque images
  and solid rectangles have dedicated paths. Small color-byte differences from
  legacy triangle interpolation are explicit; serial/parallel output matches.
- Use a persistent, bounded per-instance row pool with serial fallback on
  thread/allocation failure. Reuse workers and stack work descriptors; defer
  depth clearing until a 3D draw needs it. Transformed 2D draws synchronize with
  the reference renderer. Shutdown, resize, reset and moved instances are tested.
- Skip earlier pixels hidden by one later opaque untextured rectangle in each
  painted axis batch, retaining visible borders and exact RGBA. A true unculled
  painted reference checks 336 frame comparisons across workers 1/2/8, including fractional
  clipping, transparency, textured covers and transformed boundaries.
- Measure 120 warmed CPU frames per benchmark scene. Native presentation and
  sustained 60 FPS remain separate, unverified acceptance criteria.

### Experimental native rendering and physical framebuffer APIs

- Accept strict `LANTERN_RENDER_SCALE=1..10`, preserving the unset 400×240 default.
  Add mutually exclusive `LANTERN_RENDER_NATIVE=1` using SDL/Metal backing pixels,
  bounded 4000×2400 aspect-preserving fit, resize updates and upfront allocation.
- Add typed C/Wick physical width, height and native-mode getters, plus additive
  `native-render-v1` build-info capability metadata. Logical coordinates stay
  400×240. No new numeric width, game physics or source-art detail is implied.
- Optimize axis-aligned 2D rectangles while retaining exact legacy pixels and
  transformed-draw fallback. Differential and failure/resize tests cover both.
- Keep native rendering experimental: representative 3600×2160 CPU work measured
  181 ms before upload. Native display cadence and device behavior need review.


### Native host width

- Default Lantern/Wick to a 64-bit native pointer ABI. `WICK_HOST_BITS=32`
  explicitly permits a matching 32-bit toolchain; CMake rejects invalid values
  and compiler-width mismatches before dependency discovery. The public Wick
  header applies the same default and compile-time checks to standalone embeds,
  and the CMake target propagates the choice to consumers.
- Add `wick::HOST_BITS` and non-executing `lantern --build-info --json` output
  (`lantern.build-info.v1`) for actual pointer width, selected target width,
  compiler architecture and `num` storage/mantissa bits and radix. Invalid
  build-info arguments produce a structured usage error and exit 2.
- Preserve `num` as `double`, existing unsigned 32-bit helpers, bytecode, API
  signatures and renderer/game behavior. Tests distinguish real native compiler
  and API/build-info checks from synthetic 32/64-bit policy branches; these do
  not claim native 32-bit or Windows/Linux release verification.

### Native audio

- Add three independent SFX/music/speech buses, sample-clock gain ramps, stable
  voice handles, pause/resume, fade-stop, equal-power music replacement and
  audible-speech music ducking in the C ABI and typed Wick host. Legacy channel
  playback retains its default mix and bypasses the additive bus controls.
- Add optional `lt.try_load_sound` and bounded `sound_unload` for per-line
  dialogue caches. Unload refuses active/paused data; stale voice and sound IDs
  never alias replacements, including after reload/shutdown. Exhaustion fails.
- Harden WAV decoding with regular-file checks, exact RIFF/chunk/frame validation,
  finite normalized float samples, 8–192 kHz input, checked resampling expansion,
  64 MiB per decoded asset, 128 MiB aggregate and 256 loaded assets. Device failure
  remains nonfatal. All asset I/O/allocation/deallocation stays off the callback.
- Add deterministic signal tests against the real mixer/C ABI/Wick registry for
  bus isolation, fades/equal-power crossfades, duck attack/release, exact paused
  position, lifetime/stale handles, saturation/exhaustion, malformed/expanding
  WAV bounds, optional media and concurrent control/reset. These replace the OS
  backend only; native hardware playback and AI-authored content are not proven.
- This increment plays reviewed local WAVs; model inference, network access,
  streaming and compressed codecs are outside the runtime.

### Wick compile validation

- `lantern check --json <directory|source.wick>` validates with the real host
  signatures before engine startup, without running top-level code or natives.
  Versioned JSON diagnostics and exit codes distinguish compiler, usage and I/O
  failures. Regular source reads are capped at 8 MiB; final symlinks are refused.
- Public `wick::check` compiles in an isolated temporary VM, preserving caller
  program, globals, stack, heap and RNG. Compiler failures carry structured
  file/line/message metadata directly, alongside the legacy load error string.
- Constant indices 0–65535 remain valid; entry 65,537 now fails before wrapping
  in both check and load. Unterminated strings at EOF are rejected. A 256-entry
  recursive parser budget returns a diagnostic for excessive nesting.
- API/CLI regression tests cover non-execution and caller preservation, exact
  diagnostics, JSON escaping, source limits, file policy and constant boundaries.
  This is compile-only tooling; bounded untrusted execution is not implemented.

### Renderer

- Opt-in `LANTERN_RENDER_SCALE=1..4` sets physical raster density at startup.
  Default 1 remains 400×240; 2, 3 and 4 render at 800×480, 1200×720 and
  1600×960. Game coordinates, logical input, camera and atlas UV contracts
  stay unchanged. Invalid values fail before platform/framebuffer allocation.
- 32-bit BI_BITFIELDS BMPs with an explicit alpha mask (including BMP V4)
  preserve straight RGBA and legitimate magenta. Bilinear filtering weights
  RGB by alpha, then returns straight color for one compositing operation;
  fully transparent hidden RGB cannot fringe painted edges.
- Opaque filtering keeps its exact arithmetic; nearest sampling is unchanged.
  Alpha-less 24-bit/BGRX BMPs retain legacy magenta color-key behavior and
  atlas taps stay within their UV cell. Existing 2D coverage ≤0.004 rejection
  remains in place.
- CPU regression tests cover bounded density/stride, invalid settings,
  flips/rotation/UVs, untouched opaque-frame parity, explicit alpha, legacy
  keys and hidden-RGB invariance on dark/light backgrounds at scales 1–4.
  Native SDL/Retina and iOS/Metal presentation, touch mapping and frame cadence
  still need platform validation; no native parity is claimed for this change.

## [0.8.0] — 2026-10-06

### Wick 0.3.0

- Hexadecimal/binary integer literals (0x/0b), checked through 32 bits.
- Typed bit_and/or/xor/not/shl/shr functions with checked unsigned inputs
  and shift counts; no C++ undefined shifts or float-to-int conversion.
- Explicit u8/u16 wrapping for safe integers, including negative values.
- hex/bin display helpers: uppercase hex, minimum-width zero padding,
  no truncation, checked width and value domains.
- Bit Lab: a Wick register/ALU workbench with bit toggles, ADD/AND/XOR,
  processor flags and program-counter rollover. Not a full CPU emulator.
- Standalone compiler/VM tests exercise byte identities, 64 Ki memory,
  boundary/error cases and reload alongside existing engine regression tests.
- Book edition 0.3, reference, feature ledger and release blog updated together.

### Build

- iOS build script now links the shared path-sandbox implementation used by
  the Wick host, fixing previously unresolved symbols.

### Compatibility

- num remains double; no new operators, types or dependencies.
- The ten new built-in call names reject same-named user function declarations; rename
  collisions. Existing game/package formats and callbacks are unchanged.

## [0.7] — 2026-07-14

### wick language

- **`record` types** (flat, no methods): named fields of `num` / `bool` /
  `str`, construction with named fields, field get/set, `list<record>`.
  Admitted because KORA's kitchen props needed five parallel lists.
- Feature ledger updated: records **admitted**; string indexing still
  deferred (world baking remains the clean workaround).
- CI language suite: 12 checks (semantics + must-fail-to-compile +
  showcase_wick self-play), including record construct/get/set and
  must-fail cases for unknown fields / bad field types.

### Security & packaging

- **Path sandbox** for every game-relative load (`load_texture`,
  `load_mesh`, `load_sound`) in both wick and Lua hosts: refuses `..`,
  absolute paths, and hidden segments. Shared helper in
  `src/path_sandbox.cpp`.
- **Package-mode screenshots** confined to the extract directory
  (basename only) — store games cannot write arbitrary host paths.
- **Nested `.lant` names**: packages may contain `assets/tileset.bmp`
  (still no `..`). `lantern_pack` walks recursively; extract creates
  parent directories. Flat games (Lantern Night) pack unchanged.
- Durable tests: `tests/path_sandbox_test.sh`, existing
  `tests/package_test.sh` (folder↔package byte-identical frames).

### KORA (flagship wick game)

- Kitchen (scene 3) enterable; props use `list<Prop>` records.
- `KORA_SCENE=N` forces scene **and** skips title/prologue (CI path).
- `game.info` for store packaging; kitchen folder frame byte-identical
  to kitchen-from-`.lant`.

### Docs

- `docs/WICK.md`, `docs/PACKAGE.md`, bug journals updated in the same
  change set. Public site: [wick.famemu.aulenor.com](https://wick.famemu.aulenor.com/)
  and the [wick blog](https://wick.famemu.aulenor.com/docs/blog/).

## [0.6] and earlier

See git history for the initial software rasterizer, iOS host, Lua + wick
hosts, Lantern Night dual build, and first `.lant` packer.
