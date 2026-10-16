#!/usr/bin/env python3
"""Exercise the real Lua mouse binding with dummy SDL and isolated fixtures."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--scratch", type=Path, required=True)
args = parser.parse_args()
env = {k: v for k, v in os.environ.items() if not k.startswith("LANTERN_")}
env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
cases = (
    ("valid", 'for _, button in ipairs({"left", "middle", "right", "unknown", ""}) do '
     'assert(type(lt.mouse_pressed(button)) == "boolean") '
     'assert(not lt.mouse_pressed(button)) end lt.quit()', True),
    ("numeric", 'lt.mouse_pressed(1)', False),
    ("boolean", 'lt.mouse_pressed(true)', False),
    ("missing", 'lt.mouse_pressed()', False),
)
with tempfile.TemporaryDirectory(prefix="mouse-lua-", dir=args.scratch.resolve()) as directory:
    game = Path(directory)
    # Lua deliberately keeps its error screen open. Use the existing engine
    # verification exit so bad-type cases finish after rendering that error.
    env.update(LANTERN_FIXED_DT="1", LANTERN_SHOT=str(game / "shot"), LANTERN_SHOT_FRAME="1")
    for name, source, expected in cases:
        (game / "main.lua").write_text(source)
        result = subprocess.run([str(args.binary.resolve()), str(game)], env=env,
                                capture_output=True, text=True, timeout=10)
        if (result.returncode == 0) != expected:
            raise AssertionError(name + ": " + result.stdout + result.stderr)
        if not expected and "string expected" not in result.stderr:
            raise AssertionError(name + " failed without the required argument-type error: " + result.stderr)
print("Lua mouse input: 4 cases passed")
