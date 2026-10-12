#!/usr/bin/env python3
"""Run the real affine compositor against fresh, bounded temporary fixtures."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--scratch', type=Path, required=True)
args = parser.parse_args()
try:
    with tempfile.TemporaryDirectory(prefix='painted-compositor-', dir=args.scratch.resolve(strict=True)) as work:
        result = subprocess.run([str(args.binary.resolve(strict=True)), work],
                                text=True, capture_output=True, timeout=80, check=False)
        if result.returncode:
            raise ValueError(f'{result.returncode}: {result.stdout[-2000:]} {result.stderr[-2000:]}')
        report = json.loads(result.stdout.strip().splitlines()[-1])
        if (report.get('status') != 'passed' or report.get('worker_frame_comparisons') != 72
                or report.get('occlusion_frame_comparisons') != 336
                or report.get('sampler_oracle_pixels', 0) < 200000
                or report.get('max_legacy_fixture_channel_delta', 99) > 4):
            raise ValueError('compositor acceptance fixtures were not fully exercised')
        print(json.dumps(report, sort_keys=True))
except (OSError, ValueError, subprocess.TimeoutExpired) as error:
    print(json.dumps({'status': 'failed', 'error': str(error)}))
    raise SystemExit(1)
