#!/usr/bin/env python3
"""Bounded CTest driver for the real CPU renderer and generated BMP fixtures."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

# Exact BMP produced by this fixture linked against the untouched 0.8.0 core
# (cd0c25304d803868c6050bbe17fd66e1c1e275ce). It contains opaque textures,
# geometry, text, and one translucent untextured rectangle. New painted-alpha
# filtering intentionally changes transparent-texture edges, not this fixture.
OPAQUE_SCALE1_SHA256 = '09e1a1422f574d367b6467caae2a1616add05545e196bff00723d03600adef23'


def bmp(path: Path, scale: int) -> dict:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b'BM':
        raise ValueError(f'invalid BMP: {path.name}')
    declared = struct.unpack_from('<I', data, 2)[0]
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    compression = struct.unpack_from('<I', data, 30)[0]
    pitch = (width * 3 + 3) & ~3
    if ((width, height) != (400 * scale, 240 * scale) or bpp != 24 or
            compression != 0 or declared != len(data) or offset + pitch * height != len(data)):
        raise ValueError(f'inconsistent physical BMP dimensions/stride: {path.name}')

    def pixel(x: int, y: int) -> tuple[int, int, int]:
        at = offset + (height - 1 - y) * pitch + x * 3
        b, g, r = data[at:at + 3]
        return r, g, b

    return {'width': width, 'height': height, 'pitch': pitch,
            'sha256': hashlib.sha256(data).hexdigest(),
            'corners_rgb': [pixel(x, y) for x, y in
                            ((0, 0), (width - 1, 0), (0, height - 1), (width - 1, height - 1))]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=('hd', 'painted_alpha'), required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--scratch', type=Path, required=True)
    args = parser.parse_args()
    try:
        scratch = args.scratch.resolve(strict=True)
        binary = args.binary.resolve(strict=True)
        if not scratch.is_dir():
            raise ValueError('scratch must be an existing build directory')
        with tempfile.TemporaryDirectory(prefix='renderer-' + args.kind + '-', dir=scratch) as work:
            outputs = Path(work)
            result = subprocess.run([str(binary), str(outputs)], text=True,
                                    capture_output=True, timeout=60, check=False)
            if result.returncode:
                raise ValueError(f'{args.kind} ({result.returncode}): {result.stdout[-2000:]} {result.stderr[-2000:]}')
            report = json.loads(result.stdout.strip().splitlines()[-1])
            if report['status'] != 'passed':
                raise ValueError('C++ renderer checks did not pass')
            captures = {}
            if args.kind == 'hd':
                if report['assertions'] != 570:
                    raise ValueError('570 renderer assertions were not exercised')
                captures = {f'scale{s}': bmp(outputs / f'scale{s}.bmp', s) for s in range(1, 5)}
                if captures['scale1']['sha256'] != OPAQUE_SCALE1_SHA256:
                    raise ValueError('opaque scale1 BMP differs from the untouched reference core')
                if captures['scale2']['corners_rgb'] != [(0, 255, 0), (0, 0, 0), (0, 0, 0), (0, 0, 255)]:
                    raise ValueError('independent scale2 edge/stride markers differ')
                for scale in (3, 4):
                    if captures[f'scale{scale}']['corners_rgb'] != [(0, 0, 0)] * 3 + [(255, 0, 0)]:
                        raise ValueError('physical final-pixel markers differ')
            else:
                expected = {'assertions': 282897, 'raster_pixel_probes': 93432,
                            'opaque_bit_parity_samples': 2337, 'nearest_rgba_samples': 6,
                            'whole_frame_hidden_rgb_comparisons': 16}
                if any(report.get(key) != count for key, count in expected.items()):
                    raise ValueError('frozen painted-alpha checks were not fully exercised')
                captures = {f'scale{s}-bg{bg}': bmp(outputs / f'alpha-s{s}-bg{bg}.bmp', s)
                            for s in range(1, 5) for bg in (16, 240)}
            print(json.dumps({'kind': args.kind, **report, 'physical_captures': captures}, sort_keys=True))
        return 0
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(json.dumps({'status': 'failed', 'error': str(error)}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
