# wick — the lantern engine's own language

*The wick is the part of the lantern that carries the flame. ~2k lines of
C++ we fully own: lexer → one-pass typed compiler → bytecode → stack VM.
Open source with the engine. Lua remains available; wick is the native
tongue.*

## The governing principle

**wick contains only what we truly need — nothing for the sake of having
it.** A feature enters the language only when real game code demonstrably
hurt without it; the side-by-side Kora Night builds (`games/showcase` vs
`games/showcase_wick`) are the standing evidence mechanism. Optionals are
in because a shipped save-file crash demanded them. **Records** are in
because KORA's kitchen props became five parallel lists. Closures and
modules are not "scheduled" — they are candidates that have not yet earned
admission. Feature-parity with other languages is explicitly not a reason.

## Feature evidence ledger

Candidates awaiting admission, each with the real pain that would justify
it. A feature crosses the bar only when a shipped or in-progress game is
concretely worse without it.

| Candidate | Evidence (where it hurt) | Status |
|---|---|---|
| String indexing / iteration | KORA-on-lantern (`kora/lantern`) can't parse its ASCII tile maps in-game; the world is baked to number lists at build time by a Python tool. | Recorded, not yet admitted — baking is a clean static workaround. |
| Structs / records | KORA kitchen props: five parallel lists. | **Admitted (flat `record`)** — named fields, `list<record>`, field get/set; no methods. |
| Bits / register widths | Processor-building game: masks, ALU results, address rollover, register display (`games/bitlab`). | **Admitted (0.3)** — checked functions and prefixed literals. |
| Closures / first-class fns | No shipped program has paid a price yet. | Candidate, no evidence. |

## Why (each Lua disadvantage, fixed by design)

| Lua pain | wick answer |
|---|---|
| nil errors at runtime (`tonumber("")` crash class) | **Static types + `T?` optionals.** An optional cannot be used until narrowed (`if x != nil`) or defaulted (`x ?? fallback`). The 0-byte-save bug is a *compile error*. |
| globals-by-default (typo → silent nil global) | **`let` declarations only.** Assigning an undeclared name is a compile error. |
| 1-based indexing, no `continue`, `#` quirks | 0-based `[ ]`, `continue`/`break`, `len(x)`. |
| truthiness (`""` and `0` are truthy) | Conditions must be `bool`. `if x` on a non-bool doesn't compile. |
| GC hitches mid-frame | The host runs collection **between frames only** — the one point where a stall is invisible. |
| stack-index FFI ("argument 14") | Engine API registered with **typed signatures**; arity/type errors are compile-time, resolved to native ids (no runtime table lookups). |
| interpreter-only speed | Static types → **specialized opcodes** (numeric ADD vs string CONCAT decided at compile time), locals resolved to slots. No JIT anywhere — iOS-legal by construction. |
| nondeterministic `math.random` | `rand()`/`srand(n)`: engine-owned xorshift — deterministic replays and CI. |

## The language in one screen

```wick
// template, in wick — note load_save returns str?, and the compiler
// will NOT let you use it unchecked
let best = num(lt.load_save("best") ?? "") ?? 0
let x = 200.0
let y = 120.0

fn update(dt: num) {
  if lt.key("left")  { x = x - 120 * dt }
  if lt.key("right") { x = x + 120 * dt }
  if lt.pressed("z") {
    best = best + 1
    lt.save("best", str(best))
  }
}

fn draw() {
  lt.clear(0.1, 0.1, 0.2)
  lt.rect(x - 4, y - 4, 8, 8, 1, 0.85, 0.3, 1)
  lt.print("BEST " + str(best), 4, 4, 1, 1, 1, 1)
}
```

- Types: `num` (double), `bool`, `str`, `list<T>`, `map<T>` (string keys),
  and `T?` optionals. Inference on `let`; parameter types are written out.
- `fn update(dt: num)` / `fn draw()` — same frame contract as Lua games.
- Operators: `+ - * / %`, `== != < <= > >=`, `and or not`, `??`
  (nil-coalesce), `+` concatenates **str+str only** (no implicit coercion —
  `"x" + 1` is a compile error; use `str(1)`).
- Control: `if/elif/else`, `while`, `for i in a..b` (exclusive), `break`,
  `continue`, `return`.
- Literals: `[1, 2, 3]`, `["a": 1, "b": 2]`; empty ones need a type:
  `let xs: list<num> = []`.
- Built-ins: `len push pop str num floor abs min max sqrt sin cos atan
  rand srand`. `num(s: str): num?` — parse failure is `nil`, and the type
  system makes you handle it.
- Declare before use (C-style, one pass). Top-level statements run at load.
- Comments `//`. **Records** (flat, no methods):
  `record Pt { x: num, y: num }` then `Pt { x: 1, y: 2 }`, `p.x`, `p.x = 3`,
  `list<Pt>`. Field types are num/bool/str only. No classes, no closures,
  no metatables — a game script language, ruthlessly small.

## Desktop pointer and left clicks

```wick
lt.cursor_visible(true) // native desktop arrow; also visible by default
if lt.pointer_inside() {
  let hoverX = lt.pointer_x()
  let hoverY = lt.pointer_y()
  // Test hover against your UI's logical bounds.
}
if lt.touch_pressed() and lt.touch_press_inside() {
  let clickX = lt.touch_press_x()
  let clickY = lt.touch_press_y()
  // Activate once using the click location, even after subsequent hover.
}
```

`pointer_x(): num`, `pointer_y(): num` and `pointer_inside(): bool` describe
desktop hover independently of touch. Coordinates stay clamped to the logical
400×240 screen at every framebuffer density and in native mode. `pointer_inside`
is false in letterbox margins, outside the window, before boot and after
shutdown. Window points are mapped through the current HiDPI backing size and
presentation rectangle, including after resize. Test `pointer_inside` before
hover hit testing; clamped coordinates alone cannot identify the letterbox.

The existing touch API keeps its behavior: desktop left presses/held drags update
`touch_x/y`, released hover leaves that position unchanged, `touch_down` is held
state, and sequence-based `touch_pressed` lasts one frame even for a fast
press/release. The additive `touch_press_x/y(): num` and
`touch_press_inside(): bool` instead snapshot the latest accepted begin, projected
through the presentation rectangle when the event is processed. Drag, release,
hover, leave and later resize do not change that snapshot. Read it together with
`touch_pressed` for activation; use the old `touch_x/y` for dragging. Several
begins in one poll choose the last begin and advance the same existing sequence.
Letterbox/outside begins retain `inside=false` even if the pointer later enters;
an inside begin remains valid after leaving. Foreign-window/unscoped desktop
events and unavailable renderer geometry cannot generate press edges. Before boot
and after shutdown the new getters return zero/false. iOS applies the same begin
snapshot to drawable-pixel UITouch coordinates; this is separate from desktop
hover. `touch-press-v1` in build-info and `LT_TOUCH_PRESS_V1` in the C header identify
the additive interface.

`mouse_pressed(button: str): bool` independently reports desktop `"left"`,
`"middle"` and `"right"` DOWN edges accepted inside the projected game content.
It retains a fast DOWN+UP in one poll and lasts exactly one frame. Held input,
hover, release, leave and later resize cannot repeat or invalidate the edge.
Unknown names return false; iOS has no desktop mouse and returns false.
All three buttons reject letterbox/outside DOWN and foreign-window events.
This deliberately differs from legacy `touch_pressed`, which still reports an
outside left begin together with `touch_press_inside=false`; its drag semantics
and snapshot remain unchanged. Right/middle clicks never alter left touch state.
The C ABI is `lt_mouse_pressed(const char*)`; `LT_MOUSE_BUTTONS_V1` and
`mouse-buttons-v1` in build-info identify the additive interface. Lua exposes
the same function and requires a string argument.
`cursor_visible(bool)` changes only native cursor visibility; input still tracks
while hidden. An iOS host has no desktop pointer (`pointer_inside` is always
false; coordinates are zero) and cursor visibility is a no-op. This API does not
implement iPad hover. The desktop build advertises `desktop-pointer-v1` in the
additive `input_capabilities` field of `lantern --build-info --json`.

The `pointer` CTest uses the real SDL event/backend, C ABI and Wick bindings with
dummy video/audio drivers. It checks hover without a press, letterbox/HiDPI/native
mapping, densities 1/2/4, resize, leave/reentry, click edges, old touch behavior and
cursor state. These checks do not establish the cursor's appearance on a native
desktop or a game's modal interactions.

## Optionals — the headline feature

```wick
let s = lt.load_save("hi")     // s: str?
// lt.print(s, ...)            // COMPILE ERROR: str? where str expected
if s != nil {
  lt.print(s, 4, 4, 1, 1, 1, 1) // ok: narrowed to str inside this block
}
let t = s ?? "none"             // ok: t is str
```

## Embedding (how lantern hosts it)

`wick::VM` + `addNative(vm, "lt", "load_save(str): str?", fn)` — signatures
are parsed once, checked at compile time, dispatched by id at run time.
`load()` compiles+runs top-level; `call(vm, "update", dt)` each frame;
`collect(vm)` at frame end. A game is a folder with `main.wick`; the host
prefers it over `main.lua` when both exist. Hot-reload and the error screen
work exactly as with Lua — compile errors show file:line on screen.

### Native host width

Wick defaults to a 64-bit native pointer ABI. The public `wick/wick.hpp` defaults
`WICK_HOST_BITS` to `64`, accepts `64` or `32`, and statically rejects a value
that differs from `sizeof(void*) * CHAR_BIT`. `wick::HOST_BITS` exposes the
validated choice as a compile-time constant. Pointer width comes from the
compiler target, independently of the machine running the build.

With CMake, configure `-DWICK_HOST_BITS=32` only with a real 32-bit toolchain;
Lantern additionally needs matching SDL/Lua libraries and platform support.
The CMake `wick` target publishes the selected definition to its consumers.
Standalone embedders must supply `-DWICK_HOST_BITS=32` consistently to the
compiler/VM sources and every consuming translation unit for a 32-bit build.
Omitting it retains the 64-bit default, which intentionally fails on a 32-bit
compiler. This option never supplies architecture compiler flags or dependencies.

`lantern --build-info --json` reports the compiled pointer and target widths,
compiler architecture and `num` representation without loading/executing a game,
extracting a package or initializing SDL/audio. It emits one
`lantern.build-info.v1` JSON object: success has `ok: true`, `host_bits`,
`target_bits`, `architecture`, `num` (`storage_bits`, `mantissa_bits`, `radix`)
and an empty `diagnostics` array. Invalid arguments have `ok: false`, a
`LANTERN_BUILD_INFO_USAGE` diagnostic and exit 2. Success exits 0.

Host width does not introduce an integer type: `num` is still `double`, with
its existing precision and arithmetic. The unsigned 32-bit bit helpers, API
signatures, bytecode and gameplay behavior stay unchanged. Policy fixtures for
32-bit targets are synthetic unless run with an actual 32-bit compiler/runtime;
they do not establish a native 32-bit or Windows/Linux release.

## Files

- `wick/wick.hpp` — the embed API (Value, VM, natives, ~no dependencies)
- `wick/wick_front.cpp` — lexer + one-pass typed compiler → bytecode
- `wick/wick_vm.cpp` — stack VM, frame-boundary mark/sweep GC, built-ins
- `src/wick_host.cpp` — the typed `lt.*` bindings + game host
- `games/template_wick/` — the starter, in wick

## Bits and register widths (v0.3)

Hexadecimal `0xFF` and binary `0b1010` literals accept unsigned 32-bit
integers. `bit_and`, `bit_or`, `bit_xor`, `bit_not`, `bit_shl`, and `bit_shr`
operate on checked unsigned 32-bit values; shifts require counts 0..31.
Fractions, nonfinite values, and out-of-domain values produce a runtime
error, never an unchecked host cast. `u8(n)` and `u16(n)` wrap safe integers
(including negatives) modulo 256 and 65536. `hex(n, width=1)` and
`bin(n, width=1)` pad without truncation; widths are 1..8 and 1..32.

These are functions on `num`, not new numeric types or operators. Rename
user functions colliding with these ten new built-in call names. See
[the full contract](https://learn.wick.aulenor.com/bits/) and run
`./build/lantern games/bitlab` for the processor workbench.

## Scope honesty (v0.3 surface)

**In:** optionals, locals-only, typed `lt.*`, frame-boundary GC, deterministic
`rand`, flat **records** + `list<record>`, path-sandboxed loads.

**Not yet:** closures, classes, nested containers (`list<list<…>>`), multi-file
imports, string indexing/methods, varargs, branded handle types (`tex`/`snd`).
Every one of those is addable behind the same typed front end — when a game
pays the cost. The bar: run real lantern games (Lantern Night, KORA), catch
the bug classes Lua couldn't, stay small enough to read in an afternoon
(a small dependency-free implementation).

## Compile-only validation

`lantern check --json <directory|source.wick>` is a non-executing entry point.
It uses the same `lt.*` registration as the runtime, so native argument/default
checking does not depend on a second signature list. It selects `main.wick` for
a directory or reads an explicit `.wick` regular file. It does not read `.lant`
packages, initialize a platform, load assets or touch saves. Invalid check
arguments return usage failure rather than being interpreted as game paths.

Example success (the source path is resolved at runtime):

```json
{"schema":"lantern.check.v1","ok":true,"language":"wick","source":"/games/demo/main.wick","diagnostics":[]}
```

A failure has one diagnostic with `severity: "error"`, `phase`, `code`, `file`,
`line` and `message`. `source`/`file` are `null` if unresolved and `line` is `null`
if unavailable. Compiler file, line and message come directly from the failing
compiler operation, not by parsing its legacy formatted error string. Valid
Unicode and quotes, backslashes, newlines and control characters are JSON escaped.
Invalid UTF-8 bytes are represented as byte-valued Unicode escapes.

| Exit | Meaning | Codes |
|---|---|---|
| 0 | Compilation succeeded | Empty diagnostics |
| 1 | Compilation failed (`compile`) | `WICK_COMPILE_ERROR`, `WICK_SOURCE_LIMIT`, `WICK_CONSTANT_LIMIT`, `WICK_COMPLEXITY_LIMIT` |
| 2 | Usage (`usage`), file policy/read (`io`) or environment (`host`) failure | `LANTERN_CHECK_USAGE`, `LANTERN_CHECK_SOURCE`, `LANTERN_CHECK_IO`, `WICK_SOURCE_LIMIT`, `LANTERN_CHECK_HOST` |

The source read opens with `O_NOFOLLOW`, `O_NONBLOCK` and `O_CLOEXEC`, checks
regular-file status on that same descriptor, and reads at most 8 MiB plus a
single sentinel byte. Ancestor symlinks (including macOS `/tmp`) may resolve;
the final source symlink is rejected. Exactly 8 MiB is accepted. Files and saves
are preserved. These POSIX checks cover the current desktop host.

Embedders can use:

```cpp
wick::Diagnostic diagnostic;
bool ok = wick::check(environment, source, chunkName, diagnostic);
```

`environment` may be `nullptr` for builtins only. Otherwise the check VM copies
native types, primitive defaults and numeric constants, with no callable native
function pointers, caller program records or heap values. Program-dependent
native types or heap defaults are rejected as host errors. Compiler temporaries
are destroyed afterward; no caller program, globals, stack, heap or RNG are
changed. Success clears the diagnostic. `Diagnostic::line == 0` means unavailable.
The API checks the 8 MiB bound before constructing compiler temporaries.

Both `load` and `check` now reject constant entry 65,537 before insertion while
accepting all 65,536 u16 indices. Tests compile and load 65,536 real `abs(0)`
statements, inspect the final encoded index 65535, and reject the next statement
before a top-level trap can run. No controlled prefilled pool is needed.

The compiler allows 256 nested recursive parser entries across statements,
expressions, unary operators, types and `elif`. This is a parser budget, not a
promise of 256 nested parentheses (one pair enters several parser functions).
Flat statements do not accumulate depth. Existing limits for locals, fields,
argument/container counts and jump offsets are not expanded by this increment.
Compile-only validation does not establish that a program will terminate, stay
within runtime memory, or render correctly. Instruction/call/heap budgets,
per-VM RNG, state inspection and deterministic input/save tooling remain future
work; no complete agent execution sandbox or native/mobile validation is claimed.

## Native audio buses and stable voices

Dialogue lines and music are ordinary local WAV assets. Lantern owns loading,
resampling and the mixer; it does not run speech/music models or make network
requests. Authored VoiceStudio dialogue and YuE music can be prepared outside
of the engine, reviewed, then packaged as WAVs. Subtitles and game progression
should continue when optional media is missing or the device is unavailable.

```wick
let line = lt.try_load_sound("audio/dialogue/arrival.wav")
let speaking = -1
if line >= 0 { speaking = lt.voice_play(line, lt.SPEECH) }
lt.ducking(0.3, 0.08, 0.3)
let score = lt.try_load_sound("audio/music/north.wav")
let music = -1
if score >= 0 { music = lt.music(score, 0.7, 1, 1.5) }
// When dialogue ends/skips, stop before releasing its decoded storage.
if lt.voice_active(speaking) { lt.voice_stop(speaking) }
if line >= 0 { lt.sound_unload(line) }
```

The runtime and `lantern check` expose the same typed signatures and constants:

| Wick call | Result and behavior |
|---|---|
| `lt.try_load_sound(path: str)` | `num`: sound ID, or -1 for missing/invalid/over-budget WAV. Sandbox violations still raise a runtime error. `lt.load_sound` retains its explicit load-failure error. |
| `lt.sound_unload(sound: num)` | `bool`: frees an unused sound on the calling thread. Refuses active or paused users. Completed/stopped voices no longer retain it. |
| `lt.voice_play(sound, bus=0, volume=1, loop=0, fade_seconds=0)` | `num`: a new stable positive voice handle, or -1. `lt.SFX=0`, `lt.MUSIC=1`, `lt.SPEECH=2`. Initial fade is linear. |
| `lt.voice_stop(voice, fade_seconds=0)` | `bool`: immediate stop, or linear fade from the current gain to silence and retirement. |
| `lt.voice_pause(voice, paused=1)` | `bool`: 1 pauses, 0 resumes. Pausing emits silence and freezes both sample position and that voice's fade. |
| `lt.voice_active(voice)` | `bool`: true for playing, fading and paused voices. False after stop/completion/reset, or for an unknown handle. |
| `lt.music(sound, volume=1, loop=1, crossfade_seconds=0.5)` | `num`: starts a music-bus voice and replaces all other new music-bus voices atomically on the mixer sample clock. |
| `lt.bus_volume(bus, volume, ramp_seconds=0)` | `bool`: independent linear bus-gain ramp; SFX, music and speech default to 1. |
| `lt.ducking(music_level, attack_seconds=0.08, release_seconds=0.3)` | `bool`: music gain while speech is audible, and linear attack/release times. Default level 1 disables attenuation. |

Numeric handles and bus IDs must be finite integral values in the C `int`
range; -1 is an accepted unavailable-handle sentinel. New loop/pause flags must
be exactly 0 or 1. Gains are finite 0..1, times finite 0..60 seconds. Wick
validates before narrowing numbers and reports `invalid audio argument` for
out-of-domain values; wrong argument types/arity fail compilation. C play/music
return -1, controls return 0 for invalid inputs, and leave existing state intact.
A valid unknown handle returns false without a Wick error. Ramp times round to
the nearest 48 kHz frame, with any positive time using at least one frame; each
ramp advances before mixing a sample and reaches its target on the final frame.

All old and new playback shares 16 mixer slots. A paused voice retains its slot
and asset. Positive voice handles and nonnegative sound IDs are monotonic for
the process lifetime, including hot reload and shutdown/restart; terminal `int`
exhaustion fails instead of wrapping. A stale stable handle cannot stop or pause
a reused slot. Legacy `lt.play`, `lt.stop`, `lt.channel_volume` and `lt.volume`
remain available. Legacy playback retains channel IDs 0..15 and its ordinary
volume/master mix, bypassing the new buses/ducking. Legacy channel controls
cannot manipulate new voices; do not pass stable handles to those old controls.

Music replacement uses equal-power sine/cosine envelopes, with no update-loop
timing or game-side gain steps. Independent equal-level outgoing/incoming
signals preserve summed power through the transition; correlated signals can
still sum louder and the final stereo mix is clipped to -1..1. A crossfade
reserves its new slot before changing outgoing voices: invalid assets, a silent
device, ID exhaustion or a full pool leave the old music unchanged. A paused
outgoing music voice is already silent and retires immediately on replacement.
An ordinary fade-stop of a paused voice waits until resume; use a zero-second
stop to discard it immediately. Pausing a crossfading incoming voice freezes its
envelope; other voices and global bus/duck ramps continue on the audio clock.

Ducking responds to an unpaused speech voice whose current sample, voice gain,
speech bus and master gain make a nonzero contribution. Zero samples, zero gain,
muted buses, pause, stop and completion release it. Several speech voices use
one shared music attenuation rather than multiplying it. SFX, speech itself and
legacy channels are unaffected. Attack/release begin on the corresponding sample;
a new level retargets on the next sample, while changed times apply to the next
transition. This is signal-triggered ducking; recorded silent gaps can release
the music, so choose release time for the pacing of the recording.

Hot reload/resource reset stops all voices, unloads every sound, and restores
bus gains and ducking defaults while preserving legacy master volume. Shutdown
stops/joins the device callback before resetting assets, and restores master 1.
Decoding is serialized with device start/shutdown and remains outside the mixer
lock; a concurrent resource reset discards any in-flight decode. Render/control,
unload and reset share a mutex. The callback performs no file access, allocations
or asset deallocation. Device-start failure remains a silent, nonfatal engine;
play returns -1 instead of creating a fake active handle.

WAV decoding accepts regular RIFF/WAVE files only: PCM 8/16/24/32 or IEEE float32,
mono/stereo, 8–192 kHz, converted to 48 kHz stereo with linear interpolation.
RIFF/chunk lengths, padding, frame/block alignment and byte rate must agree.
All float samples, including skipped resampling inputs, must be finite -1..1.
Limits are 64 MiB encoded input, 64 MiB decoded per sound, 128 MiB aggregate
decoded samples and 256 simultaneously loaded sounds. Output size is checked
before the expansion allocation; serialized loads also bound transient decode
storage to one encoded input and one decoded asset. An unloaded slot and its
byte allowance can be reused without reusing its public ID. At 48 kHz stereo,
64 MiB holds about 174 seconds: **streaming and compressed codecs are not yet
implemented**. Use shorter reviewed music cues and a bounded per-line dialogue
cache. A generated test-tone/PCM fixture is test material only, never authored
AI dialogue or music.
