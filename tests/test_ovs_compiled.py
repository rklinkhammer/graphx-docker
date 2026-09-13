#!/usr/bin/env python3
"""P7 compiled resource plans, immutable input and privilege gates; no networking commands."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, root = map(lambda p: Path(p).resolve(), sys.argv[1:])
examples = ['macvlan', 'ipvlan-l2', 'ipvlan-l3', 'mixed-network', 'network-observability',
            'static-route-policy', 'sdr-node/external']
with tempfile.TemporaryDirectory(prefix='graphx-p7-plan-') as temporary:
    directory = Path(temporary).resolve()
    gated_output = directory / 'privileged-evidence'
    gated = subprocess.run([sys.executable, root / 'tests/test_ovs_execution_live.py', '--target', 'lima',
                            '--images', directory / 'missing-images', '--output', gated_output, '--case', 'macvlan'],
                           capture_output=True, text=True, timeout=10)
    assert gated.returncode != 0 and 'requires explicit --allow-privileged' in gated.stderr
    assert not gated_output.exists(), 'privileged harness created state without opt-in'
    for example in examples:
        source = root / 'examples' / example / 'graphx.yml'
        output = directory / example.replace('/', '-')
        result = subprocess.run([cli, 'compile', source, '--output', output, '--source-root', source.parent,
                                 '--credential-root', directory / 'credentials', '--target', 'lima'],
                                capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, result.stderr
        state = directory / 'state'
        args = [cli, 'run', 'plan', '--output', output, '--state-root', state]
        result = subprocess.run(args, capture_output=True, text=True, timeout=15, env={**os.environ, 'PATH': ''})
        assert result.returncode == 0, result.stderr
        plan = result.stdout
        assert not state.exists(), 'planning created ownership state'
        assert '/ownership.yml' in plan and '.yaml' not in plan
        resolved = json.loads((output / 'resolved.json').read_text())
        network = resolved['network']
        for switch in network['switches']:
            assert f'add-br {switch["name"]}' in plan
        for attachment in network['attachments']:
            if attachment['kind'] != 'external':
                assert attachment['interface'] in plan
            for alias in attachment.get('aliases', []):
                assert f'address replace {alias}' in plan
        if example == 'static-route-policy':
            assert plan.count('ip netns add ') == 4
            assert plan.count('type veth peer name') == 6
            assert 'install=manual' in plan
            assert plan.index('policy=established-return') < plan.index('policy=allow-left-middle') < plan.index('policy=deny-middle-left')
            right = next(n for n in resolved['nodes'] if n['node_id'] == 'right')
            assert right['bindings']['routed'][0]['settings']['bind'] == '10.64.30.10'
        if example.startswith('ipvlan'):
            assert 'ip-dst=' in plan
        if example == 'network-observability':
            assert 'ip tuntap add' in plan and 'duration-seconds' not in plan
        if any(n['execution']['kind'] == 'container' for n in resolved['nodes']):
            assert 'before=application-bind' in plan
        # Corrupted compiled bytes must fail before a runtime tool or directory is touched.
        path = output / 'ovs-plan.json'
        original = path.read_bytes()
        path.write_bytes(original + b' ')
        corrupt = subprocess.run(args, capture_output=True, text=True, timeout=15)
        assert corrupt.returncode != 0 and 'E_COMPILE_IDENTITY' in corrupt.stderr
        path.write_bytes(original)
        if example != 'static-route-policy':
            args[2] = 'up'
            gated = subprocess.run(args, capture_output=True, text=True, timeout=15, env={**os.environ, 'PATH': ''})
            assert gated.returncode != 0 and 'E_PRIVILEGED_AUTHORIZATION' in gated.stderr, gated.stderr
            assert not state.exists()
print('P7 S07–S12/S14 plans, aliases, policy order, immutable compilation and privilege gates passed')
