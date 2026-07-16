#!/bin/bash
# path_sandbox_test.sh <lantern>
#
# Built to catch: load_* path escape regressions. Proves `..` and absolute
# paths are refused by the real lantern host (wick natives), without reading
# outside the game directory. Does NOT catch: package-mode screenshot policy
# alone (covered by package play + manual basename check).
set -euo pipefail
LANTERN="$1"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lantpath-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/game"
cat > "$WORK/game/main.wick" <<'EOF'
fn draw() {
  let t = lt.load_texture("../secret.bmp")
  lt.clear(0, 0, 0)
}
EOF

set +e
LANTERN_FIXED_DT=1 LANTERN_SHOT_FRAME=1 LANTERN_SHOT="$WORK/shot" \
  "$LANTERN" "$WORK/game" >/dev/null 2>"$WORK/err.txt"
rc=$?
set -e
grep -q "path refused" "$WORK/err.txt" || {
  echo "FAIL: .. escape was not refused:"; cat "$WORK/err.txt"; exit 1; }
grep -q "\.\." "$WORK/err.txt" || {
  echo "FAIL: refusal did not mention ..:"; cat "$WORK/err.txt"; exit 1; }
[ "$rc" -ne 0 ] || { echo "FAIL: engine exited 0 on path escape"; exit 1; }
echo "ok: .. escape refused"

cat > "$WORK/game/main.wick" <<'EOF'
fn draw() {
  let t = lt.load_texture("/etc/hosts")
  lt.clear(0, 0, 0)
}
EOF
set +e
LANTERN_FIXED_DT=1 LANTERN_SHOT_FRAME=1 LANTERN_SHOT="$WORK/shot2" \
  "$LANTERN" "$WORK/game" >/dev/null 2>"$WORK/err2.txt"
rc=$?
set -e
grep -q "path refused" "$WORK/err2.txt" || {
  echo "FAIL: absolute path was not refused:"; cat "$WORK/err2.txt"; exit 1; }
[ "$rc" -ne 0 ] || { echo "FAIL: engine exited 0 on absolute path"; exit 1; }
echo "ok: absolute path refused"

echo "path_sandbox_test: all assertions passed"
