#!/usr/bin/env python3
"""Strict frame JSON and grounded-input regressions using the real renderer."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--scratch', type=Path, required=True)
args = parser.parse_args()


def constant(value):
    raise ValueError('non-JSON numeric constant: ' + value)


def load(path):
    return json.loads(path.read_text(encoding='utf-8'), parse_constant=constant)


try:
    with tempfile.TemporaryDirectory(prefix='frame-scene-', dir=args.scratch.resolve(strict=True)) as work:
        root = Path(work)
        result = subprocess.run([str(args.binary.resolve(strict=True)), work],
                                text=True, capture_output=True, timeout=45, check=True)
        report = json.loads(result.stdout)
        assert report['negative_cases'] == 46 and report['pixel_parity_cases'] == 48, report
        ordinary = load(root / 'ordinary.json')
        assert ordinary['schema'] == 'lantern.frame-scene.v1' and ordinary['frame'] == 1234
        assert (ordinary['render_width'], ordinary['render_height']) == (400, 240)
        assert [ordinary[key] for key in ('draw_count', 'rect_count', 'sprite_count', 'glow_count', 'font_count')] == [4, 1, 3, 0, 0]
        assert len(ordinary['draws']) == ordinary['draw_count']
        assert (root / 'ordinary.json').read_bytes() == (root / 'flushed.json').read_bytes()
        assert (root / 'ordinary.json').read_bytes() == (root / 'locale.json').read_bytes()
        rect, rotated, upper, sole = ordinary['draws']
        assert [rect[key] for key in ('x', 'y', 'w', 'h', 'r', 'g', 'b', 'a')] == [1.25, 2.5, 3.75, 4.25, 0.25, 0.5, 0.75, 1]
        assert rect['rotation'] == 0 and rect['tex_path'] == '' and not rect['grounded']
        expected_path = str(root / ('controls-' + ''.join(map(chr, range(1, 32))) + '-quote"-slash\\-ы.bmp'))
        assert rotated['tex_path'] == expected_path
        assert rotated['kind'] == 'sprite_rot' and not rotated['grounded']
        assert [rotated[key] for key in ('x', 'y', 'w', 'h')] == [80, 70, 40, 20]
        angle = rotated['rotation']
        assert math.isclose(angle, 0.7, abs_tol=1e-7)
        # x/y/w/h retain their old center-box semantics; the recorded rotation
        # reconstructs the transformed corners and the wider rotated bounds.
        corners = [(100 + dx * math.cos(angle) - dy * math.sin(angle),
                    80 + dx * math.sin(angle) + dy * math.cos(angle))
                   for dx, dy in ((-20, -10), (20, -10), (20, 10), (-20, 10))]
        assert min(x for x, _ in corners) < rotated['x']
        assert max(y for _, y in corners) > rotated['y'] + rotated['h']
        assert upper['grounded'] and sole['grounded'] and upper['rotation'] == sole['rotation'] == 0
        assert [upper[key] for key in ('x', 'y', 'w', 'h', 'u0', 'u1', 'v0', 'v1')] == [10, 68, 20, 32, 1, 0.5, 0.5, 0.875]
        assert [sole[key] for key in ('x', 'y', 'w', 'h', 'v0', 'v1')] == [10, 100, 20, 10, 0.875, 1]
        assert all(draw['explicit_alpha'] is False for draw in ordinary['draws'])
        bad = load(root / 'nonfinite.json')['draws']
        assert len(bad) == 3
        assert all(bad[0][key] is None for key in ('x', 'y', 'w', 'h', 'r', 'g', 'b', 'a'))
        assert all(bad[1][key] is None for key in ('u0', 'v0', 'u1', 'v1'))
        assert bad[2]['rotation'] is None
        rejected = load(root / 'rejected.json')
        assert rejected['draw_count'] == 1 and len(rejected['draws']) == 1
        assert rejected['draws'][0]['kind'] == 'rect' and not rejected['draws'][0]['grounded']
        print(json.dumps({'status': 'passed', **report}, sort_keys=True))
except (OSError, ValueError, AssertionError, subprocess.SubprocessError) as error:
    print(json.dumps({'status': 'failed', 'error': str(error)}))
    raise SystemExit(1)
