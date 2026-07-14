#!/bin/bash
# package_test.sh <lantern> <lantern_pack> <repo_root>
#
# Built to catch: .lant format breakage. Proves the whole store delivery
# path — pack a real game, run the PACKAGE, and require the frame to be
# byte-identical to running the FOLDER; then flip one byte and require the
# runtime to refuse it (CRC). Does NOT catch: store-side curation issues.
set -euo pipefail
LANTERN="$1"; PACK="$2"; ROOT="$3"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lantpkg-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

echo "== pack showcase_wick =="
"$PACK" "$ROOT/games/showcase_wick" "$WORK/kora.lant"

echo "== folder run vs package run (must be byte-identical) =="
( cd "$ROOT" && SHOWCASE_AUTO=1 LANTERN_FIXED_DT=1 \
    LANTERN_SHOT="$WORK/folder" "$LANTERN" games/showcase_wick >/dev/null 2>&1 )
SHOWCASE_AUTO=1 LANTERN_FIXED_DT=1 LANTERN_SHOT="$WORK/packed" \
    "$LANTERN" "$WORK/kora.lant" >/dev/null 2>&1
cmp "$WORK/folder.bmp" "$WORK/packed.bmp"
echo "byte-identical"

echo "== corrupt one byte: runtime must refuse =="
SIZE=$(stat -f %z "$WORK/kora.lant" 2>/dev/null || stat -c %s "$WORK/kora.lant")
MID=$((SIZE / 2))
printf '\xFF' | dd of="$WORK/kora.lant" bs=1 seek="$MID" conv=notrunc 2>/dev/null
if "$LANTERN" "$WORK/kora.lant" >/dev/null 2>"$WORK/err.txt"; then
    echo "FAIL: corrupted package was accepted"; exit 1
fi
grep -q "corrupt" "$WORK/err.txt" || {
    echo "FAIL: refusal did not mention corruption:"; cat "$WORK/err.txt"; exit 1; }
echo "corruption refused"

echo "package_test: all assertions passed"
