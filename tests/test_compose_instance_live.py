#!/usr/bin/env python3
"""Explicit Docker acceptance for the shared instance launcher (no privileged OVS)."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[1]
CLI = Path(os.environ.get('GRAPHX_BIN', ROOT / 'build/dev/graphx')).resolve()
LAUNCHER = ROOT / 'scripts/instance.py'


def execute(args, check=True):
    value = subprocess.run([str(x) for x in args], text=True, capture_output=True)
    if check and value.returncode:
        raise AssertionError(value.stderr + value.stdout)
    return value


def wait_for(check, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if check():
                return
        except (OSError, ValueError):
            pass
        time.sleep(0.5)
    raise AssertionError('runtime condition timed out')


def main():
    for example in ('sample-pipeline', 'sdr-node/two-source'):
        with tempfile.TemporaryDirectory(prefix='graphx-compose-instance-') as raw:
            directory = Path(raw)
            with socket.socket() as endpoint:
                endpoint.bind(('127.0.0.1', 0))
                port = endpoint.getsockname()[1]
            selected = 'acceptance-' + uuid.uuid4().hex[:12]
            common = [ROOT / 'examples' / example / 'graphx.yaml', '--graphx', CLI,
                      '--state-root', directory, '--set', f'deployment.instance_id={selected}', '--port', port]
            def launch(action, check=True, extra=()):
                result = execute([sys.executable, LAUNCHER, action, *common, *extra], False)
                if check and result.returncode:
                    for receipt_path in directory.glob('*/containers.json'):
                        for identity in json.loads(receipt_path.read_text())['containers']:
                            logs = execute(['docker', 'logs', '--tail', '15', identity], False)
                            print(logs.stdout, logs.stderr, file=sys.stderr)
                    raise AssertionError(result.stderr + result.stdout)
                return result
            def get(path):
                with urllib.request.urlopen(f'http://127.0.0.1:{port}{path}', timeout=3) as response:
                    return response.read().decode()
            try:
                launch('up')
                state = next(directory.glob('*/containers.json'))
                receipt = json.loads(state.read_text())
                manifest = state.parent / 'identity/identities.json'
                before = manifest.read_bytes()
                assert launch('up', False).returncode != 0, 'duplicate start accepted'
                assert manifest.read_bytes() == before, 'duplicate start changed registrations'
                collision = [str(x).replace(f'deployment.instance_id={selected}', f'deployment.instance_id={selected}-other') for x in common]
                assert execute([sys.executable, LAUNCHER, 'up', *collision], False).returncode != 0, 'occupied published port accepted'
                assert manifest.read_bytes() == before, 'port collision changed live registrations'
                wait_for(lambda: json.loads(get('/api/ready'))['status'] == 'ready')
                wait_for(lambda: all(n.get('executionId') for n in json.loads(get('/api/topology'))['nodes'].values()))
                if example == 'sample-pipeline':
                    wait_for(lambda: any(line.startswith('graphx_edge_messages_total') and float(line.rsplit(' ', 1)[1]) > 1
                                         for line in get('/metrics').splitlines()))
                else:
                    for side, frequency in [('east', 100000000), ('west', 200000000)]:
                        identity = next(identity for identity, labels in receipt['containers'].items()
                                        if labels.get('org.graphx.node') == 'processor-' + side)
                        wait_for(lambda: f'"frequency_hz":{frequency}' in execute(['docker', 'logs', identity]).stdout)
                selected_node = 'generator' if example == 'sample-pipeline' else 'processor-east'
                launch('restart', extra=['--node', selected_node])
                restarted = {n['id']: n['execution_id'] for n in json.loads(manifest.read_text())['nodes']}
                assert restarted[selected_node] != receipt['executions'][selected_node]
                assert all(value == receipt['executions'][node] for node, value in restarted.items() if node != selected_node)
                wait_for(lambda: json.loads(get('/api/topology'))['nodes'][selected_node].get('executionId') == restarted[selected_node])
                restarted_at = time.time() * 1000
                launch('restart', extra=['--node', 'telemetry'])
                assert restarted == {n['id']: n['execution_id'] for n in json.loads(manifest.read_text())['nodes']}
                wait_for(lambda: json.loads(get('/api/ready'))['status'] == 'ready')
                wait_for(lambda: all((n.get('lastSeen') or 0) > restarted_at for n in json.loads(get('/api/topology'))['nodes'].values()))
                launch('down')
                assert not state.exists()
                assert not any(n.get('execution_id') for n in json.loads(manifest.read_text())['nodes'])
                launch('up')
                replacement = json.loads(manifest.read_text())
                assert all(node['execution_id'] != receipt['executions'][node['id']] for node in replacement['nodes'])
                print(f'PASS: {example}: traffic, registration, duplicate/port refusal, node/collector restart, shutdown, fresh reactivation')
            finally:
                if list(directory.glob('*/containers.json')):
                    launch('down')


if __name__ == '__main__':
    main()
