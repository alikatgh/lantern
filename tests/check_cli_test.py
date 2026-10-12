#!/usr/bin/env python3
"""Bounded compile-only CLI checks against the real host; stdlib only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--scratch', type=Path, required=True)
a = p.parse_args()
binary = a.binary.resolve()
checks = 0
with tempfile.TemporaryDirectory(prefix='wick-check-', dir=a.scratch) as temporary:
    root = Path(temporary)
    home = root / 'home'
    home.mkdir()
    environment = dict(os.environ, SDL_VIDEODRIVER='intentionally-invalid',
                       SDL_AUDIODRIVER='intentionally-invalid',
                       LANTERN_RENDER_SCALE='intentionally-invalid',
                       HOME=str(home), TMPDIR=str(root),
                       LANTERN_SHOT=str(root / 'must-not-render'))
    def run(arguments, exit_code, code=None, line=None, source=None, phase=None):
        global checks
        result = subprocess.run([str(binary), *arguments], env=environment,
                                capture_output=True, timeout=5)
        assert result.returncode == exit_code, (arguments, result.returncode, result.stdout, result.stderr)
        assert result.stderr == b'', result.stderr
        # json.loads rejects trailing/multiple objects; stdout is one object.
        assert result.stdout.startswith(b'{') and result.stdout.endswith(b'\n')
        data = json.loads(result.stdout)
        assert data['schema'] == 'lantern.check.v1' and data['language'] == 'wick'
        assert data['ok'] == (exit_code == 0)
        assert isinstance(data['diagnostics'], list)
        if source is not None:
            assert data['source'] == str(source.resolve()), data
        if exit_code:
            assert len(data['diagnostics']) == 1, data
            diagnostic = data['diagnostics'][0]
            assert diagnostic['severity'] == 'error'
            assert diagnostic['code'] == code, data
            assert diagnostic['line'] == line, data
            if phase:
                assert diagnostic['phase'] == phase, data
            if source is not None:
                assert diagnostic['file'] == str(source.resolve()), data
        else:
            assert data['diagnostics'] == []
        checks += 1
        return data
    def check(path, exit_code=0, **expected):
        return run(['check', '--json', str(path)], exit_code, **expected)
    game = root / 'game'
    game.mkdir()
    main = game / 'main.wick'
    # Infinite top-level loop precedes save/texture calls. Executing this source
    # would time out even before reaching a native; compilation must finish.
    main.write_text('while true {}\nlt.save("must-not-save", "changed")\n'
                    'lt.load_texture("missing.bmp")\nlet width = lt.W + lt.H\n', encoding='utf-8')
    protected = hashlib.sha256(main.read_bytes()).hexdigest()
    check(game, source=main)
    check(main, source=main)
    assert hashlib.sha256(main.read_bytes()).hexdigest() == protected
    assert list(home.rglob('*')) == []
    assert not list(root.glob('must-not-render*'))
    # Verify the actual host registry, including optional arguments and errors.
    main.write_text('lt.rect(0, 0, 2, 2, 1, 0, 0)\nlt.sprite_ex(1, 0, 0)\n'
                    'let saved = lt.load_save("never-read")\n', encoding='utf-8')
    check(main)
    main.write_text('let painted: bool = lt.render_painted()\n'
                    'let workers: num = lt.render_workers()\n', encoding='utf-8')
    check(main)
    main.write_text('let painted: num = lt.render_painted()\n', encoding='utf-8')
    check(main, 1, code='WICK_COMPILE_ERROR', line=1)
    main.write_text('let workers = lt.render_workers(1)\n', encoding='utf-8')
    check(main, 1, code='WICK_COMPILE_ERROR', line=1)
    main.write_text('// line one\nlt.rect("bad", 0, 2, 2, 1, 0, 0)\n', encoding='utf-8')
    check(main, 1, code='WICK_COMPILE_ERROR', line=2, phase='compile')
    main.write_text('// one\nlet x: num = true\n', encoding='utf-8')
    check(main, 1, code='WICK_COMPILE_ERROR', line=2)
    main.write_text('// one\nlet x = (1 + )\n', encoding='utf-8')
    check(main, 1, code='WICK_COMPILE_ERROR', line=2)
    main.write_text('// one\nlet x = "unterminated', encoding='utf-8')
    check(main, 1, code='WICK_COMPILE_ERROR', line=2)
    name = root / 'quote"-Саха-\\-\n-control\x01.wick'
    name.write_text('let x = unknown\n', encoding='utf-8')
    data = check(name, 1, code='WICK_COMPILE_ERROR', line=1, source=name)
    assert "'unknown'" in data['diagnostics'][0]['message']
    # Compiler messages can also include quotes, slash and control characters.
    name.write_text('record X { x: num }\nlet x = X { x: "\\t" }\n', encoding='utf-8')
    check(name, 1, code='WICK_COMPILE_ERROR', line=2, source=name)
    check(root / 'missing.wick', 2, code='LANTERN_CHECK_IO', phase='io')
    check(root / 'missing-parent' / 'missing.wick', 2, code='LANTERN_CHECK_IO')
    check(root / 'wrong.lua', 2, code='LANTERN_CHECK_SOURCE')
    check(root / 'package.lant', 2, code='LANTERN_CHECK_SOURCE')
    empty = root / 'empty'; empty.mkdir()
    check(empty, 2, code='LANTERN_CHECK_IO')
    link = root / 'linked.wick'; link.symlink_to(main)
    check(link, 2, code='LANTERN_CHECK_SOURCE')
    # Ancestor aliases are allowed; only the final source symlink is rejected.
    alias = root / 'alias'; alias.symlink_to(game, target_is_directory=True)
    main.write_text('let n = abs(0)\n', encoding='utf-8')
    check(alias, source=main)
    fifo = root / 'pipe.wick'; os.mkfifo(fifo)
    check(fifo, 2, code='LANTERN_CHECK_SOURCE')
    for arguments in [[], ['--json'], ['--json', str(main), 'extra'],
                      ['--bad', str(main)], [str(main)], ['--json', '']]:
        run(['check', *arguments], 2, code='LANTERN_CHECK_USAGE')
    run(['--check', '--json', str(main)], 2, code='LANTERN_CHECK_USAGE')
    boundary = root / 'boundary.wick'
    with boundary.open('wb') as output:
        output.write(b'//' + b' ' * (8 * 1024 * 1024 - 2))
    check(boundary)
    with boundary.open('ab') as output: output.write(b' ')
    check(boundary, 2, code='WICK_SOURCE_LIMIT', phase='io')
    boundary.unlink()  # exactly owned fixture, release its 8MiB before next case
    main.write_text('let x = ' + '(' * 300 + '0' + ')' * 300, encoding='utf-8')
    check(main, 1, code='WICK_COMPLEXITY_LIMIT', line=1)
    main.write_text('abs(0)\n' * 65536, encoding='utf-8')
    check(main)
    prefix = 'abs(0)\n' * 65536
    for suffix in ['lt.plane()\n', 'let s = "x"\n',
                   'let m = ["k":true]\n', 'let width = lt.W\n']:
        main.write_text(prefix + suffix, encoding='utf-8')
        check(main, 1, code='WICK_CONSTANT_LIMIT', line=65537)
    main.write_text('let start = 0\nlet end = 0\n' + 'abs(0)\n' * 65534 +
                    'for i in start..end {}\n', encoding='utf-8')
    check(main, 1, code='WICK_CONSTANT_LIMIT', line=65537)
    main.write_text(prefix, encoding='utf-8')
    with main.open('a') as output: output.write('abs(0)\n')
    check(main, 1, code='WICK_CONSTANT_LIMIT', line=65537)
    assert list(home.rglob('*')) == []
    assert not list(root.glob('must-not-render*'))
print(f'Lantern check CLI: {checks} cases passed; source/save/resource non-execution verified')
