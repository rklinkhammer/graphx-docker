#!/usr/bin/env python3
"""Native binding semantics; temporary node objects extracted from the C++ normalizer.

These are application tests, not compiler/orchestrator acceptance. Every listener,
release file, log and capture is confined to the test's temporary resources.
"""
import copy
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
import time

build, root = map(lambda p: Path(p).resolve(), sys.argv[1:])
cli = build / 'graphx'
apps = {'sample.source': 'graphx-generator', 'sample.transform': 'graphx-transform',
        'sample.sink': 'graphx-sink', 'udp.publisher': 'graphx-udp-publisher',
        'udp.subscriber': 'graphx-udp-subscriber',
        'discovery.publisher': 'graphx-udp-publisher', 'discovery.subscriber': 'graphx-udp-subscriber'}
env = {k: v for k, v in os.environ.items() if not k.startswith(('GRAPHX_', 'SDR_'))}


def normalize(relative):
    return json.loads(subprocess.check_output([cli, 'config', 'normalize', root / relative], text=True))


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def fixtures(graph, directory, prefix):
    nodes = copy.deepcopy(graph['nodes'])
    ports = {c['id']: free_port() for c in graph['connections']}
    for n in nodes:
        n['node_id'] = prefix + '-' + n['node_id']
        n['telemetry']['credential'] = None
        n['telemetry']['host'] = '127.0.0.1'
        n['capture']['directory'] = str(directory)
        n['startup']['max_wait_ms'] = 3000
        if 'max_messages' in n['parameters']:
            n['parameters']['max_messages'] = 4
        if 'interval_ms' in n['parameters']:
            n['parameters']['interval_ms'] = 20
        if 'factor' in n['parameters']:
            n['parameters']['factor'] = 7 if prefix == 'alpha' and not n['node_id'].endswith('-west') else 3
        for peers in n['bindings'].values():
            for b in peers:
                original = b['connection']
                b['connection'] = prefix + '-' + original
                b['source_address'] = '127.0.0.1'
                b['destination_address'] = '127.0.0.1'
                s = b['settings']
                if b['transport'] == 'shared_memory':
                    s['segment'] = '/gx' + prefix + str(os.getpid()) + str(ports[original])
                else:
                    s['port'] = ports[original]
                    s['bind'] = '127.0.0.1'
                    if b['transport'] == 'tcp':
                        s['host'] = '127.0.0.1'
                    else:
                        s['interface'] = '127.0.0.1'
                        s['destination'] = '239.255.71.12' if s['mode'] == 'multicast' else '127.0.0.1'
                        if s['mode'] == 'multicast':
                            s['bind'] = '0.0.0.0'
                            s['ttl'] = 0
    return nodes


def publish_release(path, token):
    staged = path.with_suffix('.pending')
    staged.write_text(token)
    staged.replace(path)


def run_nodes(nodes, directory, continuous=False):
    release = directory / 'release'
    token = secrets.token_hex(32)
    processes = []
    try:
        for n in nodes:
            config = directory / (n['node_id'] + '.json')
            config.write_text(json.dumps(n))
            log = directory / (n['node_id'] + '.log')
            with log.open('w') as output:
                process = subprocess.Popen([build / apps[n['type']], '--node', n['node_id'],
                    '--config', config, '--release-file', release, '--release-token', token],
                    stdout=output, stderr=subprocess.STDOUT, env=env)
            processes.append((process, log, n))
        deadline = time.monotonic() + 5
        while not all('ready node=' in log.read_text() for _, log, _ in processes):
            assert time.monotonic() < deadline, [(p.poll(), f.read_text()) for p, f, _ in processes]
            assert all(p.poll() is None for p, _, _ in processes), [f.read_text() for _, f, _ in processes]
            time.sleep(0.02)
        assert all(' value=' not in f.read_text() for _, f, _ in processes), 'traffic escaped release barrier'
        publish_release(release, token)
        if continuous:
            deadline = time.monotonic() + 5
            while not all(' seq=30 ' in log.read_text() for _, log, _ in processes):
                assert time.monotonic() < deadline, [f.read_text() for _, f, _ in processes]
                assert all(p.poll() is None for p, _, _ in processes), [f.read_text() for _, f, _ in processes]
                time.sleep(0.02)
            assert all(p.poll() is None for p, _, _ in processes)
            # Stop downstream first so its blocking receive is interrupted explicitly.
            sink = next((p, f) for p, f, n in processes if n['type'] == 'sample.sink')
            sink[0].terminate()
            assert sink[0].wait(timeout=2) == 130, sink[1].read_text()
            return
        for process, log, n in processes:
            assert process.wait(timeout=8) == 0, log.read_text()
            text = log.read_text()
            values = [line for line in text.splitlines() if line.startswith('node=')]
            assert len(values) == 4, text
            if n['type'] == 'sample.sink':
                factor = 7 if n['node_id'].startswith('alpha-') and not n['node_id'].endswith('-west') else 3
                assert all(f' seq={i} value={i * factor} ' in values[i - 1] for i in range(1, 5)), text
            assert all(f"node={n['node_id']} " in line for line in values)
            other = 'beta-' if n['node_id'].startswith('alpha-') else 'alpha-'
            assert other not in text, text
            if n['capture']['enabled']:
                capture = directory / (n['node_id'] + '.pcapng')
                assert capture.stat().st_size > 100 and capture.read_bytes()[:4] == b'\x0a\x0d\x0d\x0a'
    finally:
        for process, _, _ in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


scenarios = [('S01', 'sample-pipeline'), ('S02', 'shared-memory'), ('S03', 'udp-unicast'),
             ('S04', 'udp-multicast'), ('S05-bindings', 'udp-broadcast'), ('S06', 'capture')]
for label, scenario in scenarios:
    graph = normalize(f'examples/{scenario}/graphx.yml')
    with tempfile.TemporaryDirectory(prefix='graphx-p2-') as tmp:
        directory = Path(tmp).resolve()
        run_nodes(fixtures(graph, directory, 'alpha'), directory)
    print(f'{label}: native application semantics passed')

graph = normalize('examples/sample-pipeline/graphx.yml')
assert all(n['parameters']['max_messages'] == 0 for n in graph['nodes'])
with tempfile.TemporaryDirectory(prefix='graphx-continuous-') as tmp:
    directory = Path(tmp).resolve()
    nodes = fixtures(graph, directory, 'continuous')
    for node in nodes:
        node['parameters']['max_messages'] = 0
    run_nodes(nodes, directory, continuous=True)
print('Continuous sample: traffic beyond finite default and explicit shutdown passed')
with tempfile.TemporaryDirectory(prefix='graphx-p2-two-') as tmp:
    directory = Path(tmp).resolve()
    run_nodes(fixtures(graph, directory, 'alpha') + fixtures(graph, directory, 'beta'), directory)
with tempfile.TemporaryDirectory(prefix='graphx-p2-t01-') as tmp:
    directory = Path(tmp).resolve()
    run_nodes(fixtures(normalize('examples/variants/renamed-multi-source/graphx.yml'), directory, 'alpha'), directory)
print('T01: authored east/west variant and two concurrent independent renamed pipelines passed')

# The raw SDR programs use the C++ node validator too. Bind all three local
# listeners before releasing the feedback cycle; no sample/status readiness probe.
with tempfile.TemporaryDirectory(prefix='graphx-p2-sdr-') as tmp:
    directory = Path(tmp).resolve()
    common = root / 'examples/sdr-node/common'
    tls = directory / 'tls'
    subprocess.run([common / 'generate_tls.sh', tls], check=True, capture_output=True)
    raw_nodes = fixtures(normalize('examples/sdr-node/simulated/graphx.yml'), directory, 'alpha')
    scripts = {'sdr.simulator': 'sdr_simulator.py', 'sdr.processor': 'processor.py', 'sdr.result-sink': 'sink.py'}
    processes = []
    release = directory / 'release'
    token = secrets.token_hex(32)
    try:
        for n in raw_nodes:
            for peers in n['bindings'].values():
                for peer in peers:
                    if peer['schema'] == 'RawSdrControl':
                        peer['security']['server_name'] = 'sdr-node'
            config = directory / (n['node_id'] + '.json')
            config.write_text(json.dumps(n))
            log = directory / (n['node_id'] + '.log')
            identity = 'sdr-node' if n['type'] == 'sdr.simulator' else 'processor'
            runtime_env = {**env, 'GRAPHX_CLI': str(cli), 'SDR_TLS_CA': str(tls / 'ca.pem'),
                'SDR_TLS_CLIENT_CA': str(tls / 'ca.pem'), 'SDR_TLS_CERT': str(tls / (identity + '.pem')),
                'SDR_TLS_KEY': str(tls / (identity + '.key'))}
            with log.open('w') as output:
                process = subprocess.Popen([sys.executable, common / scripts[n['type']],
                    '--node', n['node_id'], '--config', config, '--release-file', release,
                    '--release-token', token], stdout=output, stderr=subprocess.STDOUT, env=runtime_env)
            processes.append((process, log))
        deadline = time.monotonic() + 5
        while not all('ready node=' in log.read_text() for _, log in processes):
            assert time.monotonic() < deadline and all(p.poll() is None for p, _ in processes), [f.read_text() for _, f in processes]
            time.sleep(0.02)
        assert 'result 1 ' not in (directory / 'alpha-sink.log').read_text()
        publish_release(release, token)
        deadline = time.monotonic() + 5
        while 'result 4 ' not in (directory / 'alpha-sink.log').read_text():
            assert time.monotonic() < deadline, [f.read_text() for _, f in processes]
            time.sleep(0.02)
        # The control client is also bound to the renamed instance's control port.
        control = subprocess.run([sys.executable, common / 'sdrctl.py', '--node', 'alpha-processor',
            '--config', directory / 'alpha-processor.json', 'tune', '433920000'],
            env={**runtime_env, 'SDR_TLS_CERT': str(tls / 'processor.pem'),
                 'SDR_TLS_KEY': str(tls / 'processor.key')}, capture_output=True, text=True, timeout=5)
        assert control.returncode == 0 and json.loads(control.stdout)['frequency_hz'] == 433920000, control.stderr
    finally:
        for process, _ in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
print('SDR: renamed raw feedback cycle, local readiness, results and authenticated control passed')

with tempfile.TemporaryDirectory(prefix='graphx-p2-negative-') as tmp:
    directory = Path(tmp).resolve()
    node = fixtures(graph, directory, 'alpha')[0]
    config = directory / 'node.json'
    command = [cli, 'node-settings', '--node', node['node_id'], '--config', config]

    def reject(value, code):
        config.write_text(json.dumps(value))
        result = subprocess.run(command, capture_output=True, text=True, timeout=3)
        assert result.returncode and code in result.stderr, result.stderr

    for field, value, code in [('node_id', 'wrong', 'E_NODE_ID'), ('contract_version', 1, 'E_VERSION'),
                               ('type_revision', 999, 'E_NODE_TYPE'), ('extra', True, 'E_')]:
        changed = copy.deepcopy(node)
        changed[field] = value
        reject(changed, code)
    for field, value in [('schema', 'Wrong'), ('encoding', 'raw'), ('role', 'listen'), ('source_address', 'ambiguous-host'), ('destination_address', 'invalid-address')]:
        changed = copy.deepcopy(node)
        changed['bindings']['samples'][0][field] = value
        reject(changed, 'E_BINDING')
    for peers in [[], node['bindings']['samples'] * 2]:
        changed = copy.deepcopy(node)
        changed['bindings']['samples'] = peers
        reject(changed, 'E_PORT_CARDINALITY')
    changed = copy.deepcopy(node)
    changed['parameters']['max_messages'] = -1
    reject(changed, 'E_')
    changed = copy.deepcopy(node)
    del changed['bindings']['samples']
    reject(changed, 'E_BINDING')
    config.write_text(json.dumps(node))
    assert subprocess.run(command, capture_output=True).returncode == 0
    for args in [[], ['--node', node['node_id']], ['--config', str(config)],
                 ['--node', node['node_id'], '--config', str(config), '--node', 'duplicate']]:
        result = subprocess.run([build / 'graphx-generator', *args], capture_output=True, text=True)
        assert result.returncode and 'E_ARGUMENT' in result.stderr
    token = secrets.token_hex(32)
    release = directory / 'release'
    runtime = [build / 'graphx-generator', '--node', node['node_id'], '--config', config,
               '--release-file', release, '--release-token', token]
    release.write_text('wrong token')
    result = subprocess.run(runtime, capture_output=True, text=True, timeout=3)
    assert 'E_RELEASE_IDENTITY' in result.stderr
    release.unlink()
    release.symlink_to(config)
    result = subprocess.run(runtime, capture_output=True, text=True, timeout=3)
    assert result.returncode != 0
    release.unlink()
    changed = copy.deepcopy(node)
    changed['startup']['max_wait_ms'] = 60
    config.write_text(json.dumps(changed))
    result = subprocess.run(runtime, capture_output=True, text=True, timeout=3)
    assert 'E_READINESS_TIMEOUT' in result.stderr
    config.write_text(json.dumps(node))
    for released in [False, True]:
        if released:
            publish_release(release, token)
        process = subprocess.Popen(runtime, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        assert 'ready node=' in process.stdout.readline()
        time.sleep(0.1)
        start = time.monotonic()
        process.terminate()
        stdout, stderr = process.communicate(timeout=2)
        assert time.monotonic() - start < 1.5 and process.returncode == 130, (stdout, stderr)
print('Node identity/schema/cardinality/arguments/barrier and connection interruption negatives passed')
