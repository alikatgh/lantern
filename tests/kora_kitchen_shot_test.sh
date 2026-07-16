#!/bin/bash
# kora_kitchen_shot_test.sh <lantern> <kora_lantern_dir> [out_prefix]
#
# Built to catch: KORA_SCENE=3 still drawing the title screen (intro not
# skipped). Drives the real lantern binary against the real KORA folder,
# captures a frame, and requires it to differ from a title-screen control
# shot (no KORA_SCENE). Does NOT catch: interactive door entry to kitchen.
set -euo pipefail
LANTERN="$1"
KORA="$2"
OUT="${3:-$(mktemp -d "${TMPDIR:-/tmp}/kora-kitchen-XXXXXX")}"
mkdir -p "$OUT"

echo "== title control (no KORA_SCENE) =="
LANTERN_FIXED_DT=1 LANTERN_SHOT="$OUT/title" LANTERN_SHOT_FRAME=20 \
  "$LANTERN" "$KORA" >/dev/null 2>"$OUT/title.log"

echo "== kitchen (KORA_SCENE=3 only — must skip intro) =="
KORA_SCENE=3 KORA_TIME=60 LANTERN_FIXED_DT=1 \
  LANTERN_SHOT="$OUT/kitchen" LANTERN_SHOT_FRAME=20 \
  "$LANTERN" "$KORA" >/dev/null 2>"$OUT/kitchen.log"

test -s "$OUT/title.bmp"
test -s "$OUT/kitchen.bmp"
cmp -s "$OUT/title.bmp" "$OUT/kitchen.bmp" && {
  echo "FAIL: kitchen frame is byte-identical to title — intro not skipped"
  exit 1
}
# crude size check (400x240 24-bit BMP ~288k)
sz=$(wc -c < "$OUT/kitchen.bmp" | tr -d ' ')
[ "$sz" -gt 10000 ] || { echo "FAIL: kitchen bmp too small ($sz)"; exit 1; }

echo "ok: kitchen frame differs from title ($sz bytes)"
echo "kora_kitchen_shot_test: all assertions passed"
echo "artifacts: $OUT/kitchen.bmp $OUT/title.bmp"
