#!/usr/bin/env python3
"""Exercise aggregate CMake example targets without invoking infrastructure."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()


def run(*command, ok=True):
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert (result.returncode == 0) == ok, (command, result.stdout, result.stderr)
    return result.stdout


with tempfile.TemporaryDirectory(prefix='graphx cmake examples ') as temporary:
    source = Path(temporary).resolve()
    (source / 'scripts').mkdir()
    (source / 'scripts/example_cli.py').write_text(
        'print("example list")\n')
    (source / 'scripts/build_examples.py').write_text(
        'print("example artifacts")\n')
    (source / 'bad.cpp').write_text('#error shutdown must not compile this file\n')
    (source / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(fixture LANGUAGES CXX)\n'
        'add_executable(graphx-cli bad.cpp)\n'
        f'include("{root}/cmake/Examples.cmake")\n')
    build = source / 'build'
    run('cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja')
    targets = run('cmake', '--build', str(build), '--target', 'help')
    assert 'examples-list:' in targets
    assert 'examples-build:' in targets and 'examples-rebuild:' in targets
    assert 'sample-pipeline-up:' not in targets and 'four-radio-vita-prepare:' not in targets
    # Aggregate artifact construction is explicit and still builds the CLI first.
    dry = run('ninja', '-C', str(build), '-t', 'commands', 'all')
    assert 'example_cli.py' not in dry and 'build_examples.py' not in dry
    dry = run('ninja', '-C', str(build), '-t', 'commands', 'examples-build')
    assert 'bad.cpp' in dry and ' artifacts build ' in dry and '--platform' in dry
    dry = run('ninja', '-C', str(build), '-t', 'commands', 'examples-rebuild')
    assert ' --fresh' in dry
print('CMake example targets: explicit aggregate builds and code-only default passed')
