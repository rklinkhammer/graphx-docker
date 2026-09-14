#!/usr/bin/env python3
"""Explicitly authorized local Linux/Lima P7 acceptance. Never provisions a VM or engine."""
import argparse
import json
import hashlib
import os
from pathlib import Path
import shutil
import socket
import select
import re
import subprocess
import sys
import time
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--allow-privileged', action='store_true')
parser.add_argument('--target', choices=['native-linux', 'lima'], required=True)
parser.add_argument('--images', type=Path, required=True)
parser.add_argument('--release', type=Path)
parser.add_argument('--crash-interruptions', action='store_true', help='exercise durable recovery after immediate process exit')
parser.add_argument('--all-interruptions', action='store_true', help='inject failure after every registered infrastructure mutation')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--case', choices=['macvlan', 'ipvlan-l2', 'ipvlan-l3', 'mixed-network',
                                      'network-observability', 'static-route-policy'], required=True)
args = parser.parse_args()
if not args.allow_privileged or sys.platform != 'linux' or os.geteuid() != 0:
    parser.error('requires explicit --allow-privileged and an authorized local Linux root runner')
# The guarded harness alone uses YAML for deliberate corruption and fixture edits;
# every authored graph still passes the authoritative C++ loader/compiler.
import yaml
source = Path(__file__).resolve().parents[1]
cli = args.release / 'bin/graphx' if args.release else source / 'build/dev/graphx'
root = args.output.resolve()
if not root.is_relative_to(Path('/var/lib/graphx')):
    parser.error('Linux runtime evidence must remain under /var/lib/graphx')
root.mkdir(parents=True, exist_ok=False)
args.images = args.images.resolve()

def call(*argv, ok=True, timeout=120, env=None):
    result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=timeout,
                            env=env, cwd=source)
    if ok:
        assert result.returncode == 0, (argv, result.stdout, result.stderr)
    else:
        assert result.returncode != 0, (argv, result.stdout)
    return result


def inventory():
    return {key: call(*command).stdout.splitlines() for key, command in {
        'containers': ['docker', 'ps', '-aq', '--no-trunc'],
        'networks': ['docker', 'network', 'ls', '--no-trunc', '--format', '{{.ID}}'],
        'volumes': ['docker', 'volume', 'ls', '--format', '{{.Name}}'],
        'bridges': ['ovs-vsctl', 'list-br'],
        'namespaces': ['ip', 'netns', 'list'],
        'links': ['ip', '-o', 'link', 'show'],
        'capture_processes': ['ps', '-eo', 'pid,comm'],
    }.items()}


def preserved(before, after):
    for kind in ['containers', 'networks', 'bridges', 'namespaces']:
        assert sorted(before[kind]) == sorted(after[kind]), (kind, before[kind], after[kind])
    links = lambda values: {line.split(':', 2)[1].split('@')[0].strip() for line in values}
    assert links(before['links']) == links(after['links']), 'host link inventory changed'
    processes = lambda values: {line.strip() for line in values if line.split()[-1] in
                                ('graphx', 'dumpcap', 'graphx-diagnosti', 'graphx-diagnost')}
    assert processes(before['capture_processes']) == processes(after['capture_processes']), 'owned process remains'
    assert set(before['volumes']) <= set(after['volumes']), 'pre-existing volume removed'


call('docker', 'info')
call('docker', 'compose', 'version')
call('ovs-vsctl', '--timeout=5', 'show')
before = inventory()
(root / 'before.json').write_text(json.dumps(before, indent=2) + '\n')
graph = 'p7-' + uuid.uuid4().hex[:12]
authored = root / 'workspace/examples' / args.case / 'graphx.yml'
authored.parent.mkdir(parents=True)
original = source / 'examples' / args.case / 'graphx.yml'
value = yaml.safe_load(original.read_text())
value['graph']['id'] = graph
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    console_port = listener.getsockname()[1]
value.setdefault('platform', {})['console'] = {'port': console_port}
if args.case == 'network-observability':
    value['network']['captures'][0].update(max_file_bytes=65536, max_files=2)
normalized = json.loads(call(cli, 'config', 'normalize', original, '--target', args.target, '--catalog-root', source / 'config/catalog').stdout)
for node in normalized['nodes']:
    if 'max_messages' in node['parameters']:
        value['nodes'][node['node_id']].setdefault('parameters', {})['max_messages'] = 10000
shutil.copytree(args.images / 'catalog', root / 'workspace/config/catalog')
authored.write_text(yaml.safe_dump(value))
compiled, state_root = root / 'compiled', root / 'state'
call(cli, 'compile', authored, '--target', args.target, '--catalog-root', root / 'workspace/config/catalog',
     '--source-root', root / 'workspace', '--credential-root', root / 'credentials', '--output', compiled)
resolved = json.loads((compiled / 'resolved.json').read_text())
options = ['--output', compiled, '--state-root', state_root, '--images', args.images, '--allow-privileged']
if args.release:
    options += ['--release', args.release.resolve()]
ledger = state_root / graph / 'ownership.yml'
checks = []
try:
    (root / 'up.log').write_text(call(cli, 'run', 'up', *options).stdout)
    active = yaml.safe_load(ledger.read_text())
    (root / 'ready-ownership.yml').write_text(ledger.read_text())
    (root / 'status.log').write_text(call(cli, 'run', 'status', *options).stdout)
    checks.append('owned infrastructure ready')
    # Corruption must reject the entire cleanup before deleting a bridge or endpoint.
    original_ledger = ledger.read_bytes()
    corrupted = yaml.safe_load(original_ledger)
    bridge = next(r for r in corrupted['resources'] if r['kind'] == 'ovs_bridge')
    ledger.write_bytes(original_ledger.replace(bridge['uuid'].encode(),
                      b'00000000-0000-0000-0000-000000000000', 1))
    try:
        rejected = call(cli, 'run', 'down', *options, ok=False)
        (root / 'corruption-rejected.log').write_text(rejected.stdout + rejected.stderr)
        assert 'replaced' in rejected.stdout + rejected.stderr, rejected.stderr
        assert call('ovs-vsctl', 'br-exists', bridge['name']).returncode == 0
    finally:
        ledger.write_bytes(original_ledger)
    checks.append('corrupted bridge identity refuses cleanup')
    if args.case == 'network-observability':
        import fcntl
        import struct
        tap = next(a for a in resolved['network']['attachments'] if a['kind'] == 'qemu_tap')
        interface = tap['interface']
        assert 'netem' not in call('tc', 'qdisc', 'show', 'dev', interface).stdout
        with open('/dev/net/tun', 'r+b', buffering=0) as device:
            fcntl.ioctl(device, 0x400454ca, struct.pack('16sH', interface.encode(), 0x1002))
            # One broadcast ARP request on the identity-owned lab TAP, with no guest boot.
            mac = bytes.fromhex('027700000010')
            frame = b'\xff' * 6 + mac + b'\x08\x06' + struct.pack('!HHBBH', 1, 0x0800, 6, 4, 1)
            frame += mac + socket.inet_aton(tap['address'].split('/')[0]) + b'\x00' * 6 + socket.inet_aton('10.77.0.1')
            frame = frame.ljust(1400, b'\x00')
            for _ in range(1000):
                device.write(frame)
                time.sleep(.001)
        checks.append('owned TAP frame injection with no baseline netem or guest boot')
    if args.case == 'static-route-policy':
        namespaces = {a['owner']: a['namespace'] for a in resolved['network']['attachments'] if 'namespace' in a}
        router = resolved['network']['routers'][0]
        destination = router['routes'][0]['destination']
        assert destination not in call('ip', 'netns', 'exec', router['namespace'], 'ip', 'route', 'show', destination).stdout
        def send(owner, destination, port, token):
            script = "import socket,sys; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); t=sys.argv[3].encode(); s.sendto(b'GXR1'+len(t).to_bytes(2,'big')+t,(sys.argv[1],int(sys.argv[2])))"
            call('ip', 'netns', 'exec', namespaces[owner], sys.executable, '-c', script, destination, port, token)
        send('left', '10.64.2.10', 18601, 'allow')
        send('middle', '10.64.1.10', 18602, 'deny')
        send('left', '10.64.30.10', 18603, 'before-route')
        time.sleep(.3)
        route = router['routes'][0]
        call('ip', 'netns', 'exec', router['namespace'], 'ip', 'route', 'add', route['destination'], 'via', route['via'], 'dev', route['device'])
        send('left', '10.64.30.10', 18603, 'after-route')
        time.sleep(.3)
        logs = state_root / graph / 'logs'
        assert 'token=allow' in (logs / 'middle.log').read_text()
        assert 'token=deny' not in (logs / 'left.log').read_text()
        assert 'token=before-route' not in (logs / 'right.log').read_text()
        assert 'token=after-route' in (logs / 'right.log').read_text()
        checks.append('ordered policy and deferred route packet behavior')
    container_nodes = [n for n in resolved['nodes'] if n['execution']['kind'] == 'container']
    if container_nodes:
        deadline = time.monotonic() + 15
        for application in container_nodes:
            if not application['type'].startswith('sample.'):
                continue
            while True:
                log = call('docker', 'logs', f"graphx-{graph}-{application['node_id']}").stdout
                if ' seq=' in log:
                    (root / (application['node_id'] + '-traffic.log')).write_text(log)
                    break
                assert time.monotonic() < deadline, ('no application traffic', application['node_id'], log)
                time.sleep(.2)
        checks.append('sample application traffic reaches every container application')
        node = container_nodes[0]['node_id']
        metadata = json.loads(call('docker', 'inspect', f'graphx-{graph}-{node}').stdout)[0]
        pid = metadata['State']['Pid']
        platform = json.loads(call('docker', 'inspect', f'graphx-{graph}-platform').stdout)[0]
        platform_ip = platform['NetworkSettings']['Networks'][f'graphx-{graph}-mg-{node}']['IPAddress']
        probe = "import urllib.request,sys; assert urllib.request.urlopen(sys.argv[1],timeout=2).status==200"
        def management_ready():
            call('nsenter', '-t', pid, '-n', '--', sys.executable, '-c', probe, f'http://{platform_ip}:{console_port}/api/ready')
        management_ready()
        # A listening but forbidden platform port proves the management ACL,
        # rather than relying on a closed application socket to reject a connection.
        server_script = "import socket; s=socket.socket(); s.bind(('0.0.0.0',0)); s.listen(); print(s.getsockname()[1],flush=True); s.settimeout(2);\ntry: s.accept(); print('unexpected-management-connection',flush=True)\nexcept TimeoutError: pass"
        server = subprocess.Popen(['nsenter', '-t', str(platform['State']['Pid']), '-n', '--',
                                   sys.executable, '-c', server_script], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            assert select.select([server.stdout], [], [], 3)[0], 'management test listener did not start'
            forbidden_port = int(server.stdout.readline())
            connect = "import socket,sys; socket.create_connection((sys.argv[1],int(sys.argv[2])),timeout=.5)"
            call('nsenter', '-t', pid, '-n', '--', sys.executable, '-c', connect, platform_ip, forbidden_port, ok=False, timeout=3)
            stdout, stderr = server.communicate(timeout=5)
            assert server.returncode == 0 and 'unexpected-management-connection' not in stdout, (stdout, stderr)
        finally:
            if server.poll() is None:
                server.terminate()
                server.wait(timeout=3)
        checks.append('listening non-allowed management port is blocked by ACL')
        assert call('nsenter', '-t', pid, '-n', '--', 'sysctl', '-n', 'net.ipv4.ip_forward').stdout.strip() == '0'
        endpoint = next(a for a in resolved['network']['attachments'] if a['kind'] == 'container_veth' and a['owner'] == node)
        routes = json.loads(call('nsenter', '-t', pid, '-n', '--', 'ip', '-j', 'route', 'show', 'dev', endpoint['interface']).stdout)
        assert routes
        for route in routes:
            call('nsenter', '-t', pid, '-n', '--', 'ip', 'route', 'del', route['dst'], 'dev', endpoint['interface'])
        try:
            management_ready()
            connection = next(c for c in resolved['connections'] if c['from']['node'] == node or c['to']['node'] == node)
            peer = connection['destination_address'] if connection['from']['node'] == node else connection['source_address']
            call('nsenter', '-t', pid, '-n', '--', 'ping', '-c', '1', '-W', '1', peer, ok=False, timeout=5)
            mg_route = json.loads(call('nsenter', '-t', pid, '-n', '--', 'ip', '-j', 'route', 'get', platform_ip).stdout)[0]
            call('nsenter', '-t', pid, '-n', '--', 'ip', 'route', 'add', peer + '/32', 'via', platform_ip, 'dev', mg_route['dev'])
            try:
                call('nsenter', '-t', pid, '-n', '--', 'ping', '-c', '1', '-W', '1', peer, ok=False, timeout=5)
                management_ready()
            finally:
                call('nsenter', '-t', pid, '-n', '--', 'ip', 'route', 'del', peer + '/32')
            checks.append('explicit peer route through management cannot bypass the data plane')
        finally:
            for route in routes:
                command = ['nsenter', '-t', pid, '-n', '--', 'ip', 'route', 'replace', route['dst']]
                if 'gateway' in route:
                    command += ['via', route['gateway']]
                command += ['dev', endpoint['interface']]
                call(*command)
        checks.append('data-route removal breaks peer reachability while management remains reachable')
    if resolved['network']['captures']:
        deadline = time.monotonic() + 15
        while True:
            snapshots = list((state_root / graph / active['handoff_name']).glob('*.pcapng'))
            if len(snapshots) == len(resolved['network']['captures']):
                decoded = [call('capinfos', '-c', '-E', snapshot).stdout for snapshot in snapshots]
                if all('Ethernet' in report and int(re.search(r'Number of packets:\s+(\d+)', report)[1]) > 0 for report in decoded):
                    (root / 'capture-decode.log').write_text('\n'.join(decoded))
                    break
            assert time.monotonic() < deadline, 'sealed capture handoff timeout'
            time.sleep(.2)
        for snapshot in snapshots:
            assert snapshot.read_bytes()[:4] == b'\x0a\x0d\x0d\x0a'
            assert snapshot.stat().st_mode & 0o777 == 0o444
        if args.case == 'network-observability':
            capture = next(r for r in yaml.safe_load(ledger.read_text())['resources'] if r['kind'] == 'network_capture')
            files = list(Path(capture['session_directory']).glob('*.pcapng'))
            assert 1 <= len(files) <= 2, files
            assert all(path.stat().st_size <= 65536 + 65535 + 4096 for path in files)
            checks.append('capture ring remains bounded after traffic exceeds its configured capacity')
        checks.append('sealed Ethernet capture handoff')
finally:
    (root / 'down.log').write_text(call(cli, 'run', 'down', *options).stdout)
    after = inventory()
    (root / 'after.json').write_text(json.dumps(after, indent=2) + '\n')
    preserved(before, after)
    # Retained history/capture volumes and sealed files are intentional evidence.
    (root / 'results.json').write_text(json.dumps({'result': 'incomplete', 'target': args.target, 'case': args.case,
                                                  'checks': checks, 'guest_boot': 'not tested'}, indent=2) + '\n')
mutations = len(resolved['network']['switches']) + len({a.get('namespace', a['owner']) for a in resolved['network']['attachments'] if a['kind'] == 'namespace_veth'})
mutations += sum(a['kind'] != 'external' for a in resolved['network']['attachments']) + len(resolved['network']['captures'])
for mutation in (range(1, mutations + 1) if args.all_interruptions else [1]):
    interrupted = call(cli, 'run', 'up', *options, ok=False,
                       env={**os.environ, ('GRAPHX_TEST_CRASH_AFTER_MUTATION' if args.crash_interruptions else 'GRAPHX_TEST_FAIL_AFTER_MUTATION'): str(mutation)})
    (root / f'interruption-{mutation}.log').write_text(interrupted.stdout + interrupted.stderr)
    assert (interrupted.returncode == 99) if args.crash_interruptions else ('injected interruption' in interrupted.stdout + interrupted.stderr)
    call(cli, 'run', 'down', *options)
    observed = inventory()
    preserved(before, observed)
checks.append('interruption rollback at ' + ('every infrastructure mutation' if args.all_interruptions else 'first bridge mutation'))
checks.append('container, network, bridge, namespace, host-link and process inventories restored; existing volumes preserved')
(root / 'results.json').write_text(json.dumps({'result': 'pass', 'target': args.target, 'case': args.case,
                                              'checks': checks, 'guest_boot': 'not tested',
                                              'mutation_points_tested': list(range(1, mutations + 1)) if args.all_interruptions else [1],
                                              'interruption_mode': 'crash' if args.crash_interruptions else 'rollback',
                                              'cli_sha256': hashlib.sha256(cli.read_bytes()).hexdigest(),
                                              'harness_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}, indent=2) + '\n')
print('P7 privileged case passed; retained evidence:', root)
