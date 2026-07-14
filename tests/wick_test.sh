#!/bin/bash
# wick_test.sh — the wick language test suite. Two halves:
#   1. semantics: a program full of check() assertions must run clean
#   2. safety: each Lua-bug-class snippet MUST FAIL TO COMPILE with the
#      expected message (this is the whole point of wick)
# Usage: bash tests/wick_test.sh <path-to-lantern-binary>   (from engine/)
set -u
BIN="${1:-build/lantern}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PASS=0; FAIL=0

run_game() { # dir -> exit code; engine env keeps it headless + 1 frame
    LANTERN_FIXED_DT=1 LANTERN_SHOT="$TMP/shot" LANTERN_SHOT_FRAME=1 \
        "$BIN" "$1" 2>"$TMP/err.txt"
}

ok()  { PASS=$((PASS+1)); echo "  ok: $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL: $1"; sed 's/^/    /' "$TMP/err.txt"; }

# ---------- 1. semantics ----------
mkdir -p "$TMP/sem"
cat > "$TMP/sem/main.wick" <<'EOF'
// arithmetic + precedence
check(2 + 3 * 4 == 14, "precedence")
check((2 + 3) * 4 == 20, "parens")
check(7 % 3 == 1, "mod")
check(-3 + 5 == 2, "unary minus")
// bools + short circuit
check(true and true, "and")
check(false or true, "or")
check(not false, "not")
check((1 < 2) and (2 <= 2) and (3 > 2) and (3 >= 3), "comparisons")
// strings
check("a" + "b" == "ab", "concat")
check(len("hello") == 5, "len str")
check(str(42) == "42", "str(num)")
check(str(true) == "true", "str(bool)")
// optionals: num() parse failure is nil; ?? recovers
check((num("12") ?? 0) == 12, "num parse")
check((num("nope") ?? 7) == 7, "num parse failure -> ??")
check((num("") ?? 7) == 7, "empty string parse -> ?? (the Lua bug!)")
// narrowing inside functions
fn narrowed(): num {
  let s = num("5")
  if s != nil {
    return s + 1
  }
  return 0 - 1
}
check(narrowed() == 6, "if x != nil narrowing")
// lists (0-based!)
let xs: list<num> = []
for i in 0..5 { push(xs, i * i) }
check(len(xs) == 5, "list len")
check(xs[0] == 0 and xs[4] == 16, "0-based indexing")
xs[2] = 99
check(xs[2] == 99, "index set")
check((pop(xs) ?? 0 - 1) == 16, "pop")
// maps
let m = ["a": 1, "b": 2]
check((m["a"] ?? 0) == 1, "map get")
check((m["zz"] ?? 42) == 42, "map miss -> nil -> ??")
m["c"] = 3
check(len(m) == 3, "map set + len")
// while / continue / break
let n = 0
let i = 0
while i < 10 {
  i = i + 1
  if i % 2 == 0 { continue }
  if i > 7 { break }
  n = n + i
}
check(n == 1 + 3 + 5 + 7, "while continue break")
// functions + recursion
fn fib(k: num): num {
  if k < 2 { return k }
  return fib(k - 1) + fib(k - 2)
}
check(fib(10) == 55, "recursion")
// deterministic rand
srand(42)
let r1 = rand()
srand(42)
check(rand() == r1, "deterministic rand")
fn draw() { lt.clear(0, 0.2, 0) }
EOF
if run_game "$TMP/sem"; then ok "semantics suite"; else bad "semantics suite"; fi

# ---------- 2. safety: these MUST fail to compile ----------
expect_error() { # name, expected-substring, source
    mkdir -p "$TMP/neg"
    printf '%s\n' "$3" > "$TMP/neg/main.wick"
    if run_game "$TMP/neg"; then
        FAIL=$((FAIL+1)); echo "  FAIL: $1 compiled but must not"
    elif grep -q "$2" "$TMP/err.txt"; then
        ok "$1"
    else
        bad "$1 (wrong message; wanted '$2')"
    fi
}

expect_error "unchecked optional" "str?" \
'let s = lt.load_save("x")
fn draw() { lt.print(s, 4, 4, 1, 1, 1, 1) }'

expect_error "no truthiness" "must be bool" \
'fn draw() { if 1 { lt.clear(0,0,0) } }'

expect_error "no implicit globals" "no implicit globals" \
'fn update(dt: num) { speling = 5 }
let spelling = 4'

expect_error "no str+num coercion" "str(x)" \
'fn draw() { lt.print("score " + 5, 4, 4, 1, 1, 1, 1) }'

expect_error "typed native args" "argument" \
'fn draw() { lt.rect("oops", 0, 10, 10, 1, 1, 1, 1) }'

expect_error "native arity" "at least" \
'fn draw() { lt.clear(0.5) }'

expect_error "undeclared variable" "unknown variable" \
'fn draw() { lt.rect(xx, 0, 10, 10, 1, 1, 1, 1) }'

expect_error "nil needs annotation" "annotate" \
'let x = nil'

# ---------- 3. the real game: wick Kora Night self-plays ----------
# anchor to the repo, not the CWD — ctest runs from build/
ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if SHOWCASE_AUTO=1 LANTERN_FIXED_DT=1 LANTERN_SHOT="$TMP/kn" \
       LANTERN_SHOT_FRAME=120 "$BIN" "$ENGINE_DIR/games/showcase_wick" \
       2>"$TMP/err.txt"; then
    ok "showcase_wick self-play"
else
    bad "showcase_wick self-play"
fi

echo "wick tests: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
