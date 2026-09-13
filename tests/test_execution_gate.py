#!/usr/bin/env python3
"""Unconverted launchers must reject execution before side effects."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
build, root = map(Path, sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='graphx-p1-gate-') as directory:
    env = {**os.environ, 'GRAPHX_CONFIG': '/nonexistent', 'GRAPHX_DEMO_STATE_DIR': directory}
    scripts = subprocess.check_output(['git', 'ls-files', 'examples/*.sh', 'examples/**/*.sh', 'scripts/network-lab.sh'], cwd=root, text=True).splitlines()
    commands = [['bash', root / script, 'up'] for script in scripts if 'E_PHASE_UNAVAILABLE' in (root / script).read_text()]
    commands += [[build / app] for app in ['graphx-generator', 'graphx-transform', 'graphx-sink', 'graphx-udp-publisher', 'graphx-udp-subscriber']]
    for command in commands:
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=10)
        assert result.returncode != 0 and 'E_PHASE_UNAVAILABLE' in result.stderr, (command, result.stderr)
    assert not list(Path(directory).iterdir()), 'execution gate created state'
print(f'{len(commands)} launcher/application execution gates passed without state creation')
