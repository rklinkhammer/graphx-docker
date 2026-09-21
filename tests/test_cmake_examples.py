#!/usr/bin/env python3
"""Exercise generated example targets without invoking any infrastructure."""
import json
from pathlib import Path
import shutil
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
        'import json,sys\nfrom pathlib import Path\n'
        'Path(__file__).with_name("called.json").write_text(json.dumps(sys.argv[1:]))\n')
    for example in (root / 'examples').rglob('graphx.yml'):
        target = source / example.relative_to(root)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(example, target)
    (source / 'bad.cpp').write_text('#error shutdown must not compile this file\n')
    (source / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.25)\nproject(fixture LANGUAGES CXX)\n'
        'add_executable(graphx-cli bad.cpp)\nset(GRAPHX_BUILD_EXAMPLES ON)\n'
        f'include("{root}/cmake/Examples.cmake")\n')
    build = source / 'build'
    run('cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja')
    targets = run('cmake', '--build', str(build), '--target', 'help')
    for example in (root / 'examples').rglob('graphx.yml'):
        stem = str(example.parent.relative_to(root / 'examples')).replace('/', '-')
        assert stem + '-prepare:' in targets and stem + '-down:' in targets
    for action in ('down', 'status', 'logs', 'open', 'scenario-clear'):
        run('cmake', '--build', str(build), '--target', 'sample-pipeline-ovs-' + action)
        args = json.loads((source / 'scripts/called.json').read_text())
        assert args[args.index('--source') + 1] == str(source)
        assert 'sample-pipeline/ovs' in args and '--allow-privileged' not in args
    # Runtime commands never enter the default build; preparing does build the CLI.
    dry = run('ninja', '-C', str(build), '-t', 'commands', 'all')
    assert 'example_cli.py' not in dry
    assert 'build_examples.py' in dry and '--platform' in dry
    dry = run('ninja', '-C', str(build), '-t', 'commands', 'four-radio-vita-prepare')
    assert 'bad.cpp' in dry and '--fresh-images' in dry and '--restart' in dry, dry
    run('cmake', '-S', str(source), '-B', str(build),
        '-DGRAPHX_EXAMPLE_ALLOW_PRIVILEGED=ON', '-DGRAPHX_EXAMPLE_TARGET=lima',
        '-DGRAPHX_EXAMPLE_SCENARIO=iq-loss-jitter', '-DGRAPHX_EXAMPLE_NODE=detector',
        '-DGRAPHX_EXAMPLE_IMAGES=/var/lib/graphx/images with spaces',
        '-DGRAPHX_EXAMPLE_CONTROL=generator:pause,resume;collector:reset')
    run('cmake', '--build', str(build), '--target', 'four-radio-vita-scenario-run')
    args = json.loads((source / 'scripts/called.json').read_text())
    assert '--allow-privileged' in args and args[args.index('--target') + 1] == 'lima'
    assert args[args.index('--action') + 1] == 'iq-loss-jitter'
    assert args[args.index('--operation') + 1] == 'run'
    assert args[args.index('--images') + 1] == '/var/lib/graphx/images with spaces'
    assert args.count('--control') == 2
    dry = run('ninja', '-C', str(build), '-t', 'commands', 'four-radio-vita-prepare')
    assert '--fresh-images' not in dry
print('CMake example targets: discovery, quoting, opt-in and cleanup dependencies passed')
