#!/usr/bin/env python3
"""Generate games/room assets — the ALBW-style interior demo.

floor.bmp  — warm plank floor (per-plank tint, seams, nail dots, knots)
wall.bmp   — darker vertical wood paneling
carpet.bmp — taupe carpet, dark border, red corner ornament + medallion
heart.bmp  — 8x8 HUD heart, magenta-keyed

Deterministic (seeded), no deps. Rerun: python3 tools/gen_room_assets.py
"""
import os
import random

from gen_demo_assets import write_bmp, KEY

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "games", "room"))
os.makedirs(OUT, exist_ok=True)

rng = random.Random(11)

# ── floor: horizontal planks, 8px tall, offset seams ─────────────────────
W = H = 64
plank_tint = [rng.uniform(-1, 1) for _ in range(H // 8 + 1)]
seam_x = [[rng.randrange(0, W) for _ in range(2)] for _ in range(H // 8 + 1)]
knots = {(rng.randrange(W), rng.randrange(H)) for _ in range(10)}

def floor_px(x, y):
    plank = y // 8
    base = (198, 154, 100)
    t = plank_tint[plank] * 10
    r, g, b = base[0] + t, base[1] + t * 0.8, base[2] + t * 0.6
    if y % 8 == 0:                              # seam between planks
        return (138, 100, 62)
    if any(abs(x - sx) < 1 for sx in seam_x[plank]):  # plank-end seam
        return (150, 110, 68)
    if (x, y) in knots:
        return (160, 118, 72)
    if (x * 7 + y * 3) % 23 == 0:               # sparse grain
        r, g, b = r - 12, g - 10, b - 8
    return (int(r), int(g), int(b))

# ── wall: vertical boards, darker ────────────────────────────────────────
def wall_px(x, y):
    if x % 8 == 0:
        return (108, 74, 44)
    base = (156, 110, 66)
    t = ((x // 8) * 13 % 7) - 3
    if (y * 5 + x) % 19 == 0:
        t -= 8
    return (base[0] + t, base[1] + t, base[2] + int(t * 0.7))

# ── carpet: taupe field, double border, red ornament ─────────────────────
def carpet_px(x, y):
    bx = min(x, W - 1 - x, y, H - 1 - y)        # distance to edge
    if bx < 2:
        return (104, 88, 84)
    if bx < 5:
        return (128, 110, 104)
    if bx == 5:
        return (104, 88, 84)
    # corner curl ornament (simple red diamonds near each corner)
    for cx, cy in ((12, 12), (W - 13, 12), (12, H - 13), (W - 13, H - 13)):
        if abs(x - cx) + abs(y - cy) in (3, 4):
            return (150, 60, 52)
    # center medallion
    cx, cy = W // 2, H // 2
    d = abs(x - cx) + abs(y - cy)
    if d in (6, 7) or d == 0:
        return (150, 60, 52)
    base = (168, 150, 142)
    if (x + y) % 2 == 0:
        return (base[0] - 5, base[1] - 5, base[2] - 5)
    return base

# ── heart: 8x8, red with darker shade, keyed ─────────────────────────────
HEART = [
    "........",
    ".XX..XX.",
    "XXXXXXXX",
    "XXXXXXXX",
    "XSSXXSSX" ,
    ".SSSSSS.",
    "..SSSS..",
    "...SS...",
]
def heart_px(x, y):
    c = HEART[y][x]
    if c == "X":
        return (214, 48, 46)
    if c == "S":
        return (156, 30, 34)
    return KEY

write_bmp(os.path.join(OUT, "floor.bmp"), W, H, floor_px)
write_bmp(os.path.join(OUT, "wall.bmp"), 32, 32, wall_px)
write_bmp(os.path.join(OUT, "carpet.bmp"), W, H, carpet_px)
write_bmp(os.path.join(OUT, "heart.bmp"), 8, 8, heart_px)
print("wrote floor.bmp wall.bmp carpet.bmp heart.bmp →", OUT)
