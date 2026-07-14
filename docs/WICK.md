# wick — the lantern engine's own language

*The wick is the part of the lantern that carries the flame. ~2k lines of
C++ we fully own: lexer → one-pass typed compiler → bytecode → stack VM.
Open source with the engine. Lua remains available; wick is the native
tongue.*

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
- Comments `//`. No classes, no closures, no metatables in v0.1 — a game
  script language, ruthlessly small, like Lua was at 1.0.

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

## v0.1 scope honesty

No closures, classes, generics beyond `list/map`, multi-file imports,
string methods, or varargs — deliberately. Every one of those is addable
behind the same typed front end. The bar for v0.1: run real lantern games,
catch the bug classes Lua couldn't, stay under ~2.5k lines.
