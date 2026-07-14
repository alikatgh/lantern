#!/usr/bin/env python3
"""Generate games/showcase (Lantern Night) assets — reuses gen_demo_assets art.

monk.bmp / mountains.obj / bell.wav — shared with the demo (same art).
whoosh.wav — wind gust for a lamp blowing out (filtered noise burst).

Rerun after editing: python3 tools/gen_showcase_assets.py  (from engine/).
"""
import math
import os
import random
import struct

from gen_demo_assets import write_bmp, write_bell, write_mountains, monk_px

HERE = os.path.dirname(os.path.abspath(__file__))
# the Lua and wick builds of Lantern Night are deliberately separate projects
# (they evolve side by side for language comparison) but share these assets
OUTS = [os.path.normpath(os.path.join(HERE, "..", "games", d))
        for d in ("showcase", "showcase_wick")]
for _o in OUTS:
    os.makedirs(_o, exist_ok=True)
OUT = OUTS[0]

def write_whoosh(path):
    rate, dur = 22050, 0.7
    n = int(rate * dur)
    rng = random.Random(7)                     # deterministic asset
    frames = bytearray()
    lp = 0.0
    for i in range(n):
        t = i / rate
        # low-passed noise with a swelling then dying envelope
        lp += (rng.uniform(-1, 1) - lp) * 0.18
        env = math.sin(math.pi * min(t / dur, 1.0)) ** 2
        v = max(-1.0, min(1.0, lp * env * 1.6))
        frames += struct.pack("<h", int(v * 32767))
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(frames)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2,
                                      2, 16))
        f.write(b"data" + struct.pack("<I", len(frames)) + frames)

for OUT in OUTS:
    write_bmp(os.path.join(OUT, "monk.bmp"), 16, 24, monk_px)
    write_bell(os.path.join(OUT, "bell.wav"))
    write_mountains(os.path.join(OUT, "mountains.obj"))
    write_whoosh(os.path.join(OUT, "whoosh.wav"))
    print("wrote monk.bmp bell.wav mountains.obj whoosh.wav →", OUT)
