"""Real SDL dummy-backend contract checks, not native display/cadence proof."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--binary", required=True)
parser.add_argument("--scratch", required=True)
parser.add_argument("--host", required=True)
parser.add_argument("--stats-probe", required=True)
args = parser.parse_args()
environment = os.environ.copy()
for key in ("LANTERN_FRAME_STATS", "LANTERN_SAVE_ROOT", "LANTERN_SHOT",
            "LANTERN_FIXED_DT", "LANTERN_RENDER_NATIVE", "LANTERN_RENDER_SCALE",
            "LANTERN_PAINTED_2D", "LANTERN_RENDER_WORKERS"):
    environment.pop(key, None)
environment.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   SDL_RENDER_DRIVER="software", LANTERN_NOVSYNC="1")

probe = subprocess.run([args.stats_probe], capture_output=True, text=True, timeout=30)
assert probe.returncode == 0, probe.stdout + probe.stderr
probe_reports = [json.loads(line) for line in probe.stdout.splitlines() if line.startswith('{"schema":')]
assert len(probe_reports) == 1
assert probe_reports[0]["poll_to_present_ms"]["median"] == 22.5
assert probe_reports[0]["poll_to_present_ms"]["p95"] == 250
assert probe_reports[0]["cadence_ms"]["median"] == 40
print(next(line for line in probe.stdout.splitlines() if line.startswith("native_frame_stats_comma_locale:")))


def run(mode, value, sample_setting=None):
    env = environment.copy()
    if sample_setting is not None:
        env["LANTERN_FRAME_STATS"] = sample_setting
    return subprocess.run([args.binary, mode, str(value)], env=env,
                          capture_output=True, text=True, timeout=30)


result = run("saves", str(Path(args.scratch).resolve()))
assert result.returncode == 0, result.stdout + result.stderr
assert '"schema":"lantern.native-frame-stats.v1"' not in result.stderr
print(result.stdout.strip())

result = run("frames", 35)
assert result.returncode == 0 and json.loads(result.stdout)["presented_frames"] == 35
assert '"schema":"lantern.native-frame-stats.v1"' not in result.stderr, result.stderr

for bad in ("", "0", "01", "3601", "99999999999999999999", "garbage", "+1", "1 "):
    result = run("frames", 1, bad)
    assert result.returncode == 2 and "LANTERN_FRAME_STATS must" in result.stderr, result.stderr
    assert '"schema":"lantern.native-frame-stats.v1"' not in result.stderr

for target, frames, samples, warmup, reason in ((3, 36, 3, 30, "target"),
        (10, 35, 5, 30, "shutdown"), (10, 20, 0, 20, "shutdown"),
        (1, 32, 1, 30, "target"), (3600, 3635, 3600, 30, "target")):
    result = run("frames", frames, str(target))
    assert result.returncode == 0, result.stdout + result.stderr
    assert json.loads(result.stdout)["presented_frames"] == frames, "stats must never quit the game"
    reports = [json.loads(line) for line in result.stderr.splitlines() if line.startswith('{"schema":')]
    assert len(reports) == 1, result.stderr
    report = reports[0]
    assert report["schema"] == "lantern.native-frame-stats.v1"
    assert (report["target_samples"], report["samples"], report["warmup_completed"],
            report["warmup_target"], report["reason"]) == (target, samples, warmup, 30, reason)
    assert report["video_driver"] == "dummy" and report["renderer"] == "software"
    assert report["display_scanout_measured"] is False and report["clock"] == "steady_clock"
    for name in ("poll_to_present_ms", "cadence_ms"):
        summary = report[name]
        if not samples:
            assert summary is None
        else:
            assert 0 <= summary["min"] <= summary["median"] <= summary["max"]
            assert summary["min"] <= summary["p95"] <= summary["p99"] <= summary["max"]
            assert 0 <= summary["over_60hz_budget"] <= samples
print("native_validation_sdl: saves/default-off/strict-settings/target/partial/cap passed (dummy backend)")

with tempfile.TemporaryDirectory(prefix="native-wick-save-", dir=args.scratch) as temp:
    root = Path(temp)
    game = root / "game"
    saves = root / "saves"
    game.mkdir()
    saves.mkdir()
    outside = root / "outside"
    outside.write_text("preserve", encoding="utf-8")
    (saves / "link").symlink_to(outside)
    (game / "main.wick").write_text('''
check(lt.load_save("missing") == nil, "missing save is nil")
check(lt.save("empty", ""), "write zero-byte save")
check((lt.load_save("empty") ?? "missing") == "", "existing empty save is empty string")
check(lt.save("slot", "private"), "write private slot")
check((lt.load_save("slot") ?? "missing") == "private", "read private slot")
check(not lt.save("../outside", "x"), "refuse traversal")
check(not lt.save("link", "x"), "refuse leaf symlink")
check(lt.load_save("link") == nil, "refuse symlink read")
fn update(dt: num) { lt.quit() }
fn draw() { lt.clear(0, 0, 0) }
''', encoding="utf-8")
    env = environment.copy()
    env.update(LANTERN_SAVE_ROOT=str(saves.resolve()), LANTERN_FRAME_STATS="1")
    result = subprocess.run([args.host, str(game)], env=env, capture_output=True,
                            text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert outside.read_text(encoding="utf-8") == "preserve"
    assert (saves / "slot").read_text(encoding="utf-8") == "private"
    assert (saves / "empty").read_bytes() == b""
    reports = [json.loads(line) for line in result.stderr.splitlines() if line.startswith('{"schema":')]
    assert len(reports) == 1 and reports[0]["reason"] == "shutdown"
    assert reports[0]["samples"] == 0 and reports[0]["warmup_completed"] == 1
print("native_validation_wick: exact host private saves/missing nil/empty string/symlink rejection passed (dummy backend)")
