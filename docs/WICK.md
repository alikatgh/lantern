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
