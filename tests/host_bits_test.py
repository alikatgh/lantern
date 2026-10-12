#!/usr/bin/env python3
"""Bounded native ABI/build-info checks; synthetic width cases are explicit."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--binary', type=Path, required=True)
p.add_argument('--probe', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--scratch', type=Path, required=True)
p.add_argument('--cmake', type=Path, required=True)
p.add_argument('--cache', type=Path, required=True)
a = p.parse_args()
source = a.source.resolve()
checks = 0


def run(command, *, cwd=None, env=None, timeout=20):
    return subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                          capture_output=True, text=True, timeout=timeout)


def require(condition, detail):
    global checks
    assert condition, detail
    checks += 1


def cmake_string(value):
    return '"' + str(value).replace('\\', '/').replace('"', '\\"') + '"'


# Preserve the parent toolchain/target flags in the actual compiler checks.
# Do not infer target width from the Python interpreter or build machine.
cache = {}
for line in a.cache.read_text(encoding='utf-8').splitlines():
    if line and not line.startswith(('#', '//')) and ':' in line and '=' in line:
        key_type, value = line.split('=', 1)
        cache[key_type.split(':', 1)[0]] = value
forward_keys = ['CMAKE_TOOLCHAIN_FILE', 'CMAKE_C_COMPILER', 'CMAKE_CXX_COMPILER',
                'CMAKE_C_COMPILER_TARGET', 'CMAKE_CXX_COMPILER_TARGET',
                'CMAKE_C_FLAGS', 'CMAKE_CXX_FLAGS', 'CMAKE_SYSROOT',
                'CMAKE_OSX_ARCHITECTURES', 'CMAKE_OSX_SYSROOT',
                'CMAKE_OSX_DEPLOYMENT_TARGET', 'CMAKE_MAKE_PROGRAM']
configure = [a.cmake, '-G', cache['CMAKE_GENERATOR']]
for key in forward_keys:
    if cache.get(key):
        configure.append(f'-D{key}:STRING={cache[key]}')
for key, option in [('CMAKE_GENERATOR_PLATFORM', '-A'),
                    ('CMAKE_GENERATOR_TOOLSET', '-T')]:
    if cache.get(key):
        configure.extend([option, cache[key]])

with tempfile.TemporaryDirectory(prefix='wick-host-bits-', dir=a.scratch) as temporary:
    root = Path(temporary)
    private_home = root / 'private-home'
    private_home.mkdir()
    game = root / 'game'
    game.mkdir()
    main = game / 'main.wick'
    main.write_text('while true {}\nlt.save("must-not-save", "changed")\n', encoding='utf-8')
    original_hash = hashlib.sha256(main.read_bytes()).hexdigest()
    environment = dict(os.environ, SDL_VIDEODRIVER='intentionally-invalid',
                       SDL_AUDIODRIVER='intentionally-invalid',
                       LANTERN_RENDER_SCALE='intentionally-invalid',
                       HOME=str(private_home), TMPDIR=str(root),
                       LANTERN_SHOT=str(root / 'must-not-render'))

    native_probe = run([a.probe], env=environment)
    require(native_probe.returncode == 0 and native_probe.stderr == '', native_probe)
    actual = json.loads(native_probe.stdout)
    native_bits = actual['host_bits']
    require(native_bits in (32, 64) and actual['target_bits'] == native_bits, actual)

    result = run([a.binary, '--build-info', '--json'], cwd=game, env=environment)
    require(result.returncode == 0 and result.stderr == '', result)
    info = json.loads(result.stdout)
    require(info['schema'] == 'lantern.build-info.v1' and info['ok'] is True, info)
    require(info['diagnostics'] == [], info)
    for key in ('host_bits', 'target_bits', 'num'):
        require(info[key] == actual[key], (key, info, actual))
    require(info['architecture'] in ('aarch64', 'x86_64', 'x86', 'arm', 'unknown'), info)
    require(info.get('render_capabilities') == ['native-render-v1'], info)
    for arguments in [[], ['--bad'], ['--json', str(game)], ['--json', '--json']]:
        result = run([a.binary, '--build-info', *arguments], cwd=game, env=environment)
        require(result.returncode == 2 and result.stderr == '', result)
        error = json.loads(result.stdout)
        require(error['schema'] == 'lantern.build-info.v1' and error['ok'] is False,
                error)
        require(len(error['diagnostics']) == 1 and
                error['diagnostics'][0]['code'] == 'LANTERN_BUILD_INFO_USAGE' and
                error['diagnostics'][0]['phase'] == 'usage', error)
    require(hashlib.sha256(main.read_bytes()).hexdigest() == original_hash,
            'build-info modified source')
    require(list(private_home.rglob('*')) == [] and not list(root.glob('must-not-render*')),
            'build-info initialized game/save/renderer')

    # Real top-level configuration: failures must occur before dependency lookup.
    # Deliberately disabling PkgConfig supplies an observable discovery sentinel.
    cases = [('wrong-native', str(96 - native_bits), 'does not match the compiler'),
             ('invalid', '16', 'must be exactly 64 or 32'),
             ('empty', '', 'must be exactly 64 or 32'),
             ('padded', '064', 'must be exactly 64 or 32')]
    for name, selected, diagnostic in cases:
        result = run([*configure, '-S', source, '-B', root / name,
                      f'-DWICK_HOST_BITS:STRING={selected}',
                      '-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig:BOOL=TRUE'])
        output = result.stdout + result.stderr
        require(result.returncode != 0 and diagnostic in output, output)
        require('find_package for module PkgConfig' not in output,
                'invalid ABI reached dependency discovery: ' + output)
    default_build = root / 'actual-default'
    result = run([*configure, '-S', source, '-B', default_build,
                  '-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig:BOOL=TRUE'])
    output = result.stdout + result.stderr
    require('WICK_HOST_BITS:STRING=64' in (default_build / 'CMakeCache.txt').read_text(),
            'fresh CMake selection is not 64')
    require(result.returncode != 0 and
            ('find_package for module PkgConfig' if native_bits == 64 else
             'does not match the compiler') in output, output)

    # Standalone consumers do not inherit the wick target definitions: check
    # public-header defaults, explicit native selection and compile negatives
    # using this build's real compiler/toolchain, not fake predefined macros.
    header_source = root / 'header-source'
    header_source.mkdir()
    cpp = '#include "wick.hpp"\nint main() { return wick::HOST_BITS == ' + str(native_bits) + ' ? 0 : 1; }'
    fixture = [
        'cmake_minimum_required(VERSION 3.20)',
        'project(wick_header_contract LANGUAGES CXX)',
        'set(CMAKE_CXX_STANDARD 20)',
        'set(CMAKE_CXX_STANDARD_REQUIRED ON)',
        'include(CheckCXXSourceCompiles)',
        'set(CMAKE_REQUIRED_INCLUDES ' + cmake_string(source / 'wick') + ')',
    ]
    for name, definition, expected in [
            ('standalone_default', None, native_bits == 64),
            ('standalone_native', native_bits, True),
            ('standalone_mismatch', 96 - native_bits, False),
            ('standalone_invalid', 16, False)]:
        fixture.append('set(CMAKE_REQUIRED_DEFINITIONS ' +
                       ('' if definition is None else f'-DWICK_HOST_BITS={definition}') + ')')
        fixture.append('check_cxx_source_compiles(' + cmake_string(cpp) + f' {name})')
        fixture.append(f'if({"NOT " if expected else ""}{name})')
        fixture.append(f'  message(FATAL_ERROR "unexpected public-header result: {name}")')
        fixture.append('endif()')
    (header_source / 'CMakeLists.txt').write_text('\n'.join(fixture) + '\n', encoding='utf-8')
    result = run([*configure, '-S', header_source, '-B', root / 'header-build'])
    require(result.returncode == 0, result.stdout + result.stderr)

    # Synthetic policy branches only: no 32-bit compiler, linker or runtime is
    # claimed by passing a size into a cmake -P fixture. Actual cases are above.
    synthetic = [('default64', None, 8, None),
                 ('explicit64', '64', 8, None),
                 ('explicit32', '32', 4, None),
                 ('default-on32', None, 4, 'does not match the compiler'),
                 ('wrong32', '32', 8, 'does not match the compiler'),
                 ('wrong64', '64', 4, 'does not match the compiler'),
                 ('invalid-selection', '16', 8, 'must be exactly 64 or 32'),
                 ('invalid-pointer-size', '64', 16, 'compiler-detected')]
    for name, selected, pointer_bytes, diagnostic in synthetic:
        script = root / (name + '.cmake')
        contents = f'set(CMAKE_SIZEOF_VOID_P {pointer_bytes})\n'
        if selected is not None:
            contents += f'set(WICK_HOST_BITS "{selected}" CACHE STRING "")\n'
        contents += 'include(' + cmake_string(source / 'cmake/WickHostBits.cmake') + ')\n'
        if diagnostic is None:
            contents += 'if(NOT WICK_HOST_BITS STREQUAL "' + (selected or '64') + '")\n'
            contents += '  message(FATAL_ERROR "selection changed")\nendif()\n'
        script.write_text(contents, encoding='utf-8')
        result = run([a.cmake, '-P', script])
        require((result.returncode == 0) if diagnostic is None else
                (result.returncode != 0 and diagnostic in result.stdout + result.stderr),
                result.stdout + result.stderr)

print(f'Wick host contract: {checks} checks passed; real native{native_bits} API/build-info, '
      'real compiler/configuration negatives, synthetic 32/64 CMake branches')
