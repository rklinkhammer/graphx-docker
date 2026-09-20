#!/usr/bin/env python3
"""P3/P4 four-radio qualification. --prepare is offline; --run requires explicit Linux authorization.

Uses the authoritative compiler and common run up/status/down ownership lifecycle.
Every destructive action rechecks recorded container/bridge identity. This harness
never provisions a VM, forwards a socket or publishes images.
"""
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import uuid

from vita_live_support import NODES, FrameObserver, assert_progress, ethernet_record, graph_document, write_json

SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE / 'scripts/release'))
from image_release import verify, VRT_PIN

CASES = {
    'P3-01': 'baseline', 'P3-02': 'mtu', 'P3-03': 'jumbo', 'P3-04': 'selection',
    'P3-05': 'passive', 'P3-06': 'saturation', 'P3-07': 'capture', 'P3-08': 'identity',
    'P3-09': 'cleanup', 'P4-01': 'available-baseline', 'P4-02': 'admission',
    'P4-03': 'unsafe', 'P4-04': 'runtime', 'P4-05': 'dependencies',
    'P4-06': 'recorder', 'P4-07': 'bridge', 'P4-08': 'interruption', 'P4-09': 'cleanup'}


def graph_id(root):
    return 'vq-' + hashlib.sha256(str(root).encode()).hexdigest()[:12]


def fixture_names(case):
    if case == 'P4-02': return [n + '-' + m for n in NODES for m in ('exit', 'stall')]
    if case == 'P4-03': return NODES + ['credentials', 'release', 'mtu', 'acl', 'identity', 'configuration', 'image']
    if case == 'P4-04': return NODES
    if case == 'P4-05': return ['-'.join(NODES[:4]), 'processor']
    if case == 'P4-06': return ['False', 'True']
    if case == 'P4-08':
        return [mode + '-' + str(n) for mode in ('graphx_test_fail_after_mutation', 'graphx_test_crash_after_mutation')
                for n in range(1, 11)] + ['restore']
    if case == 'P3-02': return ['before-release', 'graph']
    return ['graph']


def require(value, message):
    if not value: raise AssertionError(message)


def call(*command, timeout=60, env=None, ok=True):
    # Output is spooled, read only after checking a fixed bound. Docker logs use --tail.
    with tempfile.TemporaryFile() as output:
        result = subprocess.run(list(map(str, command)), stdout=output, stderr=subprocess.STDOUT,
                                timeout=timeout, env=env)
        require(output.tell() <= 8 * 1024 * 1024, 'command output exceeds 8 MiB')
        output.seek(0); text = output.read().decode(errors='replace')
    if ok: require(result.returncode == 0, f'{command}: exit {result.returncode}\n{text}')
    return result.returncode, text


def wait(predicate, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value: return value
        time.sleep(.1)
    raise AssertionError('observation deadline expired')


def inventory():
    return {key: sorted(call(*cmd)[1].splitlines()) for key, cmd in {
        'containers': ['docker', 'ps', '-aq', '--no-trunc'],
        'networks': ['docker', 'network', 'ls', '--no-trunc', '--format', '{{.ID}}'],
        'bridges': ['ovs-vsctl', 'list-br'], 'namespaces': ['ip', 'netns', 'list'],
        'links': ['ip', '-j', 'link'], 'processes': ['ps', '-eo', 'pid,comm'],
        'volumes': ['docker', 'volume', 'ls', '--format', '{{.Name}}']}.items()}


def preserved(before, after):
    for key in ('containers', 'networks', 'bridges', 'namespaces'):
        require(before[key] == after[key], 'inventory changed: ' + key)
    for name in ('links',):
        identities = lambda inv: {(p['ifindex'], p['ifname']) for p in json.loads('\n'.join(inv[name]))}
        require(identities(before) == identities(after), 'link identity inventory changed')
    selected = lambda inv: {p for p in inv['processes'] if p.split()[-1].startswith(('graphx', 'dumpcap'))}
    require(selected(before) == selected(after), 'owned process inventory changed')
    require(set(before['volumes']) <= set(after['volumes']), 'preexisting volume removed')


class Fixture:
    def __init__(self, args, root, *, available=False, capture=True, sentinel=False):
        self.args, self.root = args, root
        root.mkdir(parents=True, exist_ok=False)
        self.graph = graph_id(root)
        self.subnet = args.subnet
        self.inputs = root / 'inputs'; self.inputs.mkdir()
        shutil.copytree(args.images / 'catalog', self.inputs / 'catalog')
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0)); port = listener.getsockname()[1]
        value = graph_document(self.graph, self.subnet, port, available=available, capture=capture)
        if sentinel:
            # A small independent common-lifecycle graph, not an unowned docker run.
            value = {'version': 3, 'catalog': 'catalog/lock.json', 'graph': {'id': self.graph},
                     'platform': {'console': {'port': port}}, 'nodes': {
                         'source': {'type': 'sample.source', 'execution': {'kind': 'container'},
                                    'parameters': {'interval_ms': 100, 'max_messages': 1000000}},
                         'transform': {'type': 'sample.transform', 'execution': {'kind': 'container'}, 'parameters': {'max_messages': 1000000}},
                         'sink': {'type': 'sample.sink', 'execution': {'kind': 'container'}, 'parameters': {'max_messages': 1000000}}},
                     'connections': {'samples': {'from': 'source.samples', 'to': 'transform.samples',
                         'transport': 'tcp', 'settings': {'port': 19001}},
                         'transformed': {'from': 'transform.transformed', 'to': 'sink.transformed',
                         'transport': 'tcp', 'settings': {'port': 19002}}}}
        self.value = value
        self.authored = self.inputs / 'graphx.yml'; write_json(self.authored, value)
        self.compiled, self.state = root / 'compiled', root / 'state'
        self.credentials = root / 'credentials'
        self.options = ['--output', self.compiled, '--state-root', self.state,
                        '--images', args.images, '--credentials', self.credentials, '--allow-privileged']
        self.ledger = self.state / self.graph / 'ownership.yml'
        self.compile()
        write_json(root / 'recovery.json', {a: list(map(str, [args.cli, 'run', a, *self.options]))
                                           for a in ('status', 'down')})
        self.observer = None; self.started = False

    def compile(self):
        call(self.args.cli, 'compile', self.authored, '--target', self.args.target,
             '--catalog-root', self.inputs / 'catalog', '--source-root', self.inputs,
             '--credential-root', self.credentials, '--output', self.compiled)
        self.resolved = json.loads((self.compiled / 'resolved.json').read_text())

    def state_value(self):
        import yaml
        return yaml.safe_load(self.ledger.read_text())

    def command(self, action, *, env=None, ok=True):
        self.started |= action == 'up'
        result = call(self.args.cli, 'run', action, *self.options, timeout=240, env=env, ok=False)
        (self.root / (action + '-' + str(time.monotonic_ns()) + '.log')).write_text(result[1])
        if ok: require(result[0] == 0, result[1])
        return result

    def up(self, faults=(), mode='exit', reject=False):
        env = dict(os.environ)
        env.pop('GRAPHX_TEST_APPLICATION_NODES', None); env.pop('GRAPHX_TEST_APPLICATION_FAULT', None)
        if faults: env.update(GRAPHX_TEST_APPLICATION_NODES=','.join(faults), GRAPHX_TEST_APPLICATION_FAULT=mode)
        started = time.monotonic(); code, text = self.command('up', env=env, ok=not reject)
        require(time.monotonic() - started < 240, 'startup overall bound')
        if reject:
            require(code != 0 and not (self.ledger.parent / 'barriers/release').exists(), 'unsafe release')
            return
        for name in faults:
            output = self.logs(name)
            require('qualification-bound node=' + name in output, 'fault missed actual application binding')
            require('ready node=' + name not in output, 'fault admitted late application')
        (self.root / 'ready-ownership.yml').write_bytes(self.ledger.read_bytes())

    def container(self, name, running=None):
        state = self.state_value()
        resource = next(r for r in state['processes'] if r['kind'] == 'container' and
                        r['name'] == 'graphx-' + self.graph + '-' + name)
        obj = json.loads(call('docker', 'inspect', resource['stable_id'])[1])[0]
        require(obj['Id'] == resource['stable_id'] and obj['Image'] == resource['secondary_id'] and
                obj['Config']['Labels']['org.graphx.owner'] == state['owner_token'] and
                obj['Config']['Labels']['org.graphx.graph'] == self.graph, 'container identity mismatch')
        if running is not None: require(obj['State']['Running'] == running, 'unexpected container liveness')
        return obj

    def logs(self, name):
        return call('docker', 'logs', '--tail', '256', self.container(name)['Id'])[1]

    def counters(self, name):
        text = self.logs(name)
        patterns = {'processor': r'processor spectra=(\d+)', 'detector': r'detector received=(\d+)',
                    'recorder': r'recorder packets=(\d+)'}
        matches = re.findall(patterns.get(name, r'radio=\d+ packets=(\d+)'), text)
        return int(matches[-1]) if matches else 0

    def progress(self, names, seconds=3):
        wait(lambda: all(self.counters(n) > 0 for n in names), 20)
        before = {n: self.counters(n) for n in names}
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline: time.sleep(.1)
        after = {n: self.counters(n) for n in names}
        assert_progress(before, after, names)
        write_json(self.root / ('progress-' + str(time.monotonic_ns()) + '.json'), {'before': before, 'after': after})

    def signal(self, name, sig):
        obj = self.container(name, True)
        call('docker', 'kill', '--signal', sig, obj['Id'])
        return obj

    def start_observer(self):
        endpoints = self.state_value()['expected_endpoints']
        endpoint = next(e for e in endpoints if e['id'] == 'diagnostic:diagnostic')
        require(not endpoint.get('mirror_container') and not endpoint.get('container_id'), 'capture depends on recorder')
        self.observer = FrameObserver(endpoint['target_interface'], self.subnet, b'GRAPHX-P34-PROBE:')
        return self.observer

    def cleanup(self):
        observer_error = None
        try:
            if self.observer:
                try: write_json(self.root / 'observer.json', self.observer.close())
                except Exception as error: observer_error = error
                finally: self.observer = None
        finally:
            if self.started:
                self.command('down')
                if self.ledger.exists():
                    state = self.state_value()
                    require(not state.get('resources'), 'infrastructure remains after down')
                    retained = state.get('processes', [])
                    allowed = {'graphx-' + self.graph + '-history'} | {'graphx-' + self.graph + '-capture-' + n for n in NODES}
                    require(all(p['kind'] == 'volume' and p['name'] in allowed for p in retained), 'unexpected retained resource')
                    write_json(self.root / 'retained-history-volumes.json', retained)
                require(not (self.ledger.parent / 'barriers/release').exists(), 'barrier remains')
                self.started = False
        if observer_error: raise observer_error


def mtu(f):
    state = f.state_value()
    resources = {r['attachment_id']: r for r in state['resources'] if 'attachment_id' in r and r['kind'] != 'network_capture'}
    for e in state['expected_endpoints']:
        resource = resources[e['id']]
        host = json.loads(call('ip', '-j', 'link', 'show', e['host_interface'])[1])[0]
        require(host['mtu'] == 9000 and host['ifindex'] == resource['ifindex'], 'host MTU/identity')
        prefix = [] if not e.get('container_id') else ['nsenter', '-t', str(f.container(e['owner'], True)['State']['Pid']), '-n', '--']
        peer = json.loads(call(*prefix, 'ip', '-j', 'link', 'show', e['target_interface'])[1])[0]
        require(peer['mtu'] == 9000 and peer['ifindex'] == resource['peer_ifindex'], 'peer MTU/identity')
        for prefix_, interface in (([], e['host_interface']), (prefix, e['target_interface'])):
            text = call(*prefix_, 'ethtool', '-k', interface)[1]
            for key in ('tcp-segmentation-offload', 'generic-segmentation-offload', 'generic-receive-offload', 'tx-checksumming'):
                require(re.search(r'^' + key + r': off', text, re.M), 'offload enabled: ' + interface + ':' + key)
        for key in ('mtu_request', 'mtu'):
            require(call('ovs-vsctl', 'get', 'Interface', e['host_interface'], key)[1].strip() == '9000', 'OVS MTU')
    # Live malformed path: health fails, then only the exact interface is restored.
    endpoint = state['expected_endpoints'][0]
    call('ip', 'link', 'set', 'dev', endpoint['host_interface'], 'mtu', '1500')
    try: require(f.command('status', ok=False)[0] != 0, 'lowered MTU accepted')
    finally: call('ip', 'link', 'set', 'dev', endpoint['host_interface'], 'mtu', '9000')
    f.command('status')


def reject_startup_mtu(f, fault='mtu'):
    f.started = True
    alias_restore = None
    log = (f.root / 'held-startup.log').open('w')
    child = subprocess.Popen(list(map(str, [f.args.cli, 'run', 'up', *f.options])),
                             env={**os.environ, 'GRAPHX_TEST_HOLD_AFTER_MUTATION': '10'},
                             stdout=log, stderr=subprocess.STDOUT)
    try:
        def held():
            require(child.poll() is None, 'startup exited before hold checkpoint')
            return re.search(r'^State:\s+T', Path(f'/proc/{child.pid}/status').read_text(), re.M)
        wait(held, 120)
        state = f.state_value()
        e = next(r for r in state['resources'] if r['kind'] == 'container_veth')
        live = json.loads(call('ip', '-j', 'link', 'show', e['name'])[1])[0]
        require(live['ifindex'] == e['ifindex'], 'fault endpoint replaced')
        if fault == 'mtu':
            call('ip', 'link', 'set', 'dev', e['name'], 'mtu', '1500')
        elif fault == 'identity':
            alias_restore = (e['name'], e['ifindex'], live['ifalias'])
            call('ip', 'link', 'set', e['name'], 'alias', 'qualification-replaced')
        elif fault == 'acl':
            mirror = next(r for r in state['resources'] if r.get('attachment_id') == 'mirror')
            current = json.loads(call('ip', '-j', 'link', 'show', mirror['name'])[1])[0]
            require(current['ifindex'] == mirror['ifindex'], 'passive endpoint replaced')
            call('tc', 'filter', 'del', 'dev', mirror['name'], 'ingress', 'pref', '1')
        elif fault == 'credentials':
            volume = next(p for p in state['processes'] if p['kind'] == 'volume' and p['name'].endswith('-credentials'))
            actual = json.loads(call('docker', 'volume', 'inspect', volume['stable_id'])[1])[0]
            require(actual['Labels']['org.graphx.owner'] == state['owner_token'] and
                    actual['CreatedAt'] == volume['secondary_id'], 'credential volume replaced')
            certificate = Path(actual['Mountpoint']) / 'credentials/radio/cert.pem'
            require(certificate.is_file() and not certificate.is_symlink(), 'credential file replaced')
            # Deliberately invalidate the generation checksum in this private fixture.
            with certificate.open('ab') as stream: stream.write(b'\n')
        elif fault == 'release':
            barrier = f.ledger.parent / 'barriers/release'
            with barrier.open('x') as stream: stream.write('0' * 64)
            barrier.chmod(0o444)
        else: raise ValueError('unknown startup fault')
        child.send_signal(signal.SIGCONT)
        require(child.wait(timeout=120) != 0, 'insufficient path released applications')
        barrier = f.ledger.parent / 'barriers/release'
        if fault == 'release':
            require(barrier.read_text() == '0' * 64, 'foreign token was adopted')
            barrier.unlink()  # Exact file created above; permit ordinary owned recovery.
        else:
            require(not barrier.exists(), 'unsafe release barrier')
    finally:
        if child.poll() is None:
            child.send_signal(signal.SIGCONT); child.terminate()
            try: child.wait(timeout=20)
            except subprocess.TimeoutExpired: child.kill(); child.wait(timeout=5)
        log.close()
        if alias_restore:
            name, index, alias = alias_restore
            current = json.loads(call('ip', '-j', 'link', 'show', name)[1])[0]
            require(current['ifindex'] == index, 'refusing alias restoration on replaced endpoint')
            call('ip', 'link', 'set', name, 'alias', alias)


def jumbo(f):
    observer = f.observer or f.start_observer()
    src, dst = f.container('radio1', True), f.container('processor', True)
    addresses = {a['owner']: a['address'].split('/')[0] for a in f.resolved['network']['attachments'] if a.get('address')}
    for size in (4128, 8836):
        payload = b'GRAPHX-P34-PROBE:' + str(size).encode() + b':'
        payload += bytes((i % 251 for i in range(size - len(payload))))
        receiver_code = 'import socket,hashlib;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(("0.0.0.0",19999));s.settimeout(8);print("bound",flush=True);b=s.recv(9000);print(hashlib.sha256(b).hexdigest())'
        receiver = subprocess.Popen(['nsenter', '-t', str(dst['State']['Pid']), '-n', '--', sys.executable, '-c', receiver_code], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            # Explicit receiver readiness; bounded by its socket deadline and outer alarm.
            import select
            require(select.select([receiver.stdout], [], [], 3)[0], 'probe receiver readiness')
            require(receiver.stdout.readline().strip() == 'bound', 'probe receiver bind')
            code = 'import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.setsockopt(socket.IPPROTO_IP,10,2);s.sendto(bytes.fromhex(__import__("sys").argv[1]),(__import__("sys").argv[2],19999))'
            call('nsenter', '-t', src['State']['Pid'], '-n', '--', sys.executable, '-c', code, payload.hex(), addresses['processor'])
            out, err = receiver.communicate(timeout=10)
            require(receiver.returncode == 0 and out.strip() == hashlib.sha256(payload).hexdigest(), 'jumbo destination bytes: ' + err)
        finally:
            if receiver.poll() is None: receiver.kill(); receiver.communicate(timeout=2)
        wait(lambda: any(ethernet_record(frame).get('payload') == payload for frame in observer.probes), 3)
        time.sleep(.2)
        matches = [frame for frame in observer.probes if ethernet_record(frame).get('payload') == payload]
        require(len(matches) == 1, 'mirror duplicate-selection behavior changed')
        record = ethernet_record(matches[0])
        require(record['ip_bytes'] == size + 28 and not record['fragment'], 'fragmented jumbo')
        (f.root / ('jumbo-' + str(size) + '.hex')).write_text(matches[0].hex() + '\n')


def capture_continuity(f):
    obs = f.observer or f.start_observer()
    capture = next(r for r in f.state_value()['resources'] if r['kind'] == 'network_capture')
    directory = Path(capture['session_directory'])
    before = obs.snapshot(); death_time = time.time()
    f.signal('recorder', 'KILL'); wait(lambda: not f.container('recorder')['State']['Running'])
    f.progress(['radio1', 'radio2', 'radio3', 'radio4', 'processor', 'detector'])
    assert_progress(before, obs.snapshot(), ('data', 'context', 'spectra'))
    code, status = f.command('status', ok=False)
    require('state=capturing' in status, 'independent capture not healthy')
    if f.value['lifecycle']['startup'] == 'available': require(code == 0, status)
    # Inspect fresh packet timestamps, not retained filenames or historical counts.
    def fresh():
        for path in directory.glob('*.pcapng'):
            code, text = call('tshark', '-r', path, '-T', 'fields', '-e', 'frame.time_epoch', '-c', '256', ok=False)
            if code == 0 and any(float(line) > death_time for line in text.splitlines() if re.fullmatch(r'\d+\.\d+', line)):
                return True
        return False
    wait(fresh, 10)
    files = list(directory.glob('*.pcapng'))
    require(len(files) <= 2 and all(p.stat().st_size <= 4194304 + 9022 + 4096 for p in files), 'capture retention bound')
    write_json(f.root / 'capture-continuity.json', {'recorder_death_epoch': death_time, 'fresh_packets': True, 'files': [str(p) for p in files]})


def passive(f):
    recorder = f.container('recorder', True); pid = recorder['State']['Pid']
    status = Path(f'/proc/{pid}/status').read_text()
    for capability in ('CapInh', 'CapPrm', 'CapEff', 'CapAmb'):
        require(re.search(capability + r':\s+0+\s', status), 'recorder retained ' + capability)
    require(re.search(r'Seccomp:\s+2', status), 'recorder seccomp missing')
    host = recorder['HostConfig']
    require(host['CapAdd'] == ['NET_RAW'] and host['CapDrop'] == ['ALL'] and not host['Privileged'], 'recorder privilege policy')
    require(host['NetworkMode'] != 'host' and all('docker.sock' not in m['Destination'] and 'openvswitch' not in m['Destination'] for m in recorder['Mounts']), 'recorder host access')
    endpoint = next(e for e in f.state_value()['expected_endpoints'] if e['id'] == 'mirror')
    text = call('tc', '-s', '-j', 'filter', 'show', 'dev', endpoint['host_interface'], 'ingress')[1]
    require('drop' in text and 'matchall' in text, 'passive filter absent')
    # Independent namespace sender retains root privilege: host filter, not CAP_NET_RAW dropping, must stop it.
    marker = b'GRAPHX-P34-PROBE:forbidden'
    frame = bytes.fromhex('ffffffffffff02000000009988b5') + marker
    code = 'import socket,sys;s=socket.socket(socket.AF_PACKET,socket.SOCK_RAW);s.bind((sys.argv[1],0));s.send(bytes.fromhex(sys.argv[2]))'
    radio = f.container('radio1', True)
    radio_endpoint = next(e for e in f.state_value()['expected_endpoints'] if e['owner'] == 'radio1')
    receiver_code = '''import socket,sys,time
s=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(3));s.bind((sys.argv[1],0));s.settimeout(.1)
print("bound",flush=True);limit=time.monotonic()+2
while time.monotonic()<limit:
 try: data=s.recv(9022)
 except socket.timeout: continue
 if bytes.fromhex(sys.argv[2]) in data: sys.exit(1)
'''
    receiver = subprocess.Popen(['nsenter', '-t', str(radio['State']['Pid']), '-n', '--', sys.executable,
        '-c', receiver_code, radio_endpoint['target_interface'], marker.hex()], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        import select
        require(select.select([receiver.stdout], [], [], 3)[0] and receiver.stdout.readline().strip() == 'bound', 'passive receiver readiness')
        call('nsenter', '-t', pid, '-n', '--', sys.executable, '-c', code, endpoint['target_interface'], frame.hex())
        out, err = receiver.communicate(timeout=5)
        require(receiver.returncode == 0, 'passive injection reached application: ' + err)
    finally:
        if receiver.poll() is None: receiver.kill(); receiver.communicate(timeout=2)
    after = call('tc', '-s', '-j', 'filter', 'show', 'dev', endpoint['host_interface'], 'ingress')[1]
    def drops(value):
        return sum(int(action.get('stats', {}).get('packets', 0)) for rule in json.loads(value)
                   for action in rule.get('options', {}).get('actions', []))
    require(drops(after) > drops(text), 'passive drop counter did not increase')
    time.sleep(.5)
    require(not any(marker in frame for frame in f.observer.probes), 'recorder injected into mirror path')
    (f.root / 'passive-before.json').write_text(text); (f.root / 'passive-after.json').write_text(after)
    f.command('status')
    call('docker', 'exec', recorder['Id'], '/usr/local/libexec/graphx-vita-recorder-test')


def identity(f):
    original = f.ledger.read_bytes()
    state = f.state_value(); bridge = next(r for r in state['resources'] if r['kind'] == 'ovs_bridge')
    require(bridge['uuid'].encode() in original, 'bridge UUID evidence')
    f.ledger.write_bytes(original.replace(bridge['uuid'].encode(), b'00000000-0000-0000-0000-000000000000', 1))
    try:
        require(f.command('down', ok=False)[0] != 0, 'corrupt ownership accepted')
        call('ovs-vsctl', 'br-exists', bridge['name'])
    finally: f.ledger.write_bytes(original)
    import copy
    import yaml
    for field in ('ifindex', 'namespace_inode', 'secondary_id'):
        corrupt = copy.deepcopy(state)
        if field == 'secondary_id':
            record = next(r for r in corrupt['processes'] if r['kind'] == 'container')
            record[field] = 'sha256:' + '0' * 64
        else:
            record = next(r for r in corrupt['resources'] if r['kind'] == 'container_veth')
            record[field] += 1
        f.ledger.write_text(yaml.safe_dump(corrupt))
        try:
            require(f.command('down', ok=False)[0] != 0, 'identity substitution accepted: ' + field)
            call('ovs-vsctl', 'br-exists', bridge['name'])
        finally: f.ledger.write_bytes(original)
    endpoint = next(r for r in state['resources'] if r['kind'] == 'container_veth')
    alias = json.loads(call('ip', '-j', 'link', 'show', endpoint['name'])[1])[0]['ifalias']
    call('ip', 'link', 'set', endpoint['name'], 'alias', 'qualification-replaced')
    try: require(f.command('down', ok=False)[0] != 0, 'replaced endpoint accepted')
    finally: call('ip', 'link', 'set', endpoint['name'], 'alias', alias)
    f.command('status')


def run_case(args, case, directory):
    def fixture(name, **kw): return Fixture(args, directory / name, **kw)
    if case == 'P3-02':
        f = fixture('before-release')
        try: reject_startup_mtu(f)
        finally: f.cleanup()
    if case == 'P4-02':
        for name in NODES:
            for mode in ('exit', 'stall'):
                f = fixture(name + '-' + mode, available=True)
                try:
                    f.up([name], mode); status = f.command('status')[1]
                    require(f'application={name} admission=' in status, 'missing admission evidence')
                    f.container(name, False)
                    for other in set(NODES) - {name}:
                        require(f'application={other} admission=ready status=ready' in status, 'healthy subset not admitted')
                    if name != 'processor': f.progress([n for n in NODES if n != name])
                    require(all(f.container(n)['RestartCount'] == 0 for n in NODES), 'unexpected restart')
                finally: f.cleanup()
        return
    if case == 'P4-03':
        for name in NODES:
            f = fixture(name, available=True)
            try: f.up([name], 'invalid', reject=True)
            finally: f.cleanup()
        for fault in ('credentials', 'release', 'mtu', 'acl', 'identity'):
            f = fixture(fault, available=True)
            try: reject_startup_mtu(f, fault)
            finally: f.cleanup()
        f = fixture('configuration', available=True)
        f.value['nodes']['processor']['parameters']['rate1'] = 0
        write_json(f.authored, f.value)
        code, text = call(args.cli, 'config', 'normalize', f.authored, '--catalog-root', f.inputs / 'catalog', ok=False)
        require(code != 0 and not f.ledger.exists(), 'invalid configuration accepted')
        (f.root / 'configuration-rejected.log').write_text(text)
        f = fixture('image', available=True)
        bad = f.root / 'bad-image'; bad.mkdir()
        manifest = json.loads((args.images / 'images.json').read_bytes())
        for record in manifest['images'].values(): record['inspection']['digest'] = 'sha256:' + '0' * 64
        write_json(bad / 'images.json', manifest)
        index = f.options.index('--images') + 1; f.options[index] = bad
        try:
            code, text = f.command('up', ok=False)
            require(code != 0 and 'compiled image provenance mismatch' in text, 'substituted image identity accepted')
        finally:
            f.options[index] = args.images; f.cleanup()
        return
    if case == 'P4-04':
        for name in NODES:
            f = fixture(name, available=True)
            try:
                f.up(); f.progress(NODES); ids = {n: f.container(n)['Id'] for n in NODES}
                f.signal(name, 'KILL'); wait(lambda: not f.container(name)['State']['Running'])
                healthy = [n for n in NODES if n != name]
                if name == 'processor': healthy = [n for n in healthy if n != 'detector']
                f.progress(healthy)
                if name == 'processor': wait(lambda: 'results=stale' in f.logs('detector'))
                f.command('status')
                require(all(f.container(n)['Id'] == ids[n] and f.container(n)['RestartCount'] == 0 for n in NODES), 'respawn or replacement')
            finally: f.cleanup()
        return
    if case == 'P4-05':
        for names in (NODES[:4], ['processor']):
            f = fixture('-'.join(names), available=True)
            try:
                f.up(names); time.sleep(6)
                require('status=unavailable' in f.command('status')[1], 'missing dependency claimed available')
                require(f.counters('detector') == 0, 'fabricated spectra')
                if names == ['processor']: require(all(f.counters(n) == 0 for n in NODES[:4]), 'unconfigured radio acquired')
            finally: f.cleanup()
        return
    if case == 'P4-08':
        # Complete registered mutation matrix, including the independent diagnostic mirror.
        for mode in ('GRAPHX_TEST_FAIL_AFTER_MUTATION', 'GRAPHX_TEST_CRASH_AFTER_MUTATION'):
            for mutation in range(1, 11):
                f = fixture(mode.lower() + '-' + str(mutation), available=True)
                try:
                    code, _ = f.command('up', env={**os.environ, mode: str(mutation)}, ok=False)
                    require(code != 0, 'interruption point was not reached')
                finally: f.cleanup()
        f = fixture('restore', available=True)
        try:
            f.up(); f.progress(NODES)
            f.signal('recorder', 'STOP')
            with (f.root / 'interrupted-down.log').open('w') as log:
                child = subprocess.Popen(list(map(str, [args.cli, 'run', 'down', *f.options])), stdout=log, stderr=subprocess.STDOUT)
                try:
                    wait(lambda: not (f.ledger.parent / 'barriers/release').exists(), 20)
                    child.terminate(); child.wait(timeout=30)
                finally:
                    if child.poll() is None: child.kill(); child.wait(timeout=5)
            f.cleanup(); f.up(); f.progress(NODES)
        finally: f.cleanup()
        return
    if case == 'P4-06':
        for enabled in (False, True):
            f = fixture(str(enabled), available=True, capture=enabled)
            try:
                f.up(); f.progress(NODES)
                if enabled: capture_continuity(f)
                else:
                    require(not f.state_value().get('expected_captures'), 'diagnostics enabled implicitly')
                    f.signal('recorder', 'KILL'); f.progress(NODES[:-1]); f.command('status')
            finally: f.cleanup()
        return
    f = fixture('graph', available=case.startswith('P4'), capture=case != 'P3-08')
    try:
        f.up(); f.progress(NODES)
        if case in ('P3-01', 'P3-03', 'P3-04', 'P3-05', 'P3-06', 'P3-07'): f.start_observer()
        if case == 'P3-01':
            wait(lambda: f.observer.snapshot()['max_data_payload'] == 4128 and f.observer.snapshot()['max_spectrum_payload'] == 8836)
            require(f.observer.snapshot()['fragments'] == 0, 'production traffic fragmented')
        elif case == 'P3-02': mtu(f)
        elif case in ('P3-03', 'P3-04'):
            jumbo(f)
            if case == 'P3-04':
                # Force fresh ARP through actual application namespace then observe context/data/TCP.
                pid = f.container('radio1', True)['State']['Pid']
                call('nsenter', '-t', pid, '-n', '--', 'ip', 'neigh', 'flush', 'all')
                time.sleep(3); counts = f.observer.snapshot()
                for key in ('data', 'context', 'spectra', 'arp', 'tcp'): require(counts[key] > 0, 'missing family: ' + key)
                require(counts['outside'] == 0 and counts['fragments'] == 0, 'capture isolation/fragmentation')
        elif case == 'P3-05': passive(f)
        elif case == 'P3-06':
            before = f.observer.snapshot(); started = time.monotonic()
            recorder_before = f.logs('recorder')
            f.signal('recorder', 'STOP')
            try:
                peak = 0
                for _ in range(20):
                    f.progress(['processor', 'detector'], seconds=3)
                    pid = f.container('recorder')['State']['Pid']
                    rss = int(re.search(r'VmRSS:\s+(\d+)', Path(f'/proc/{pid}/status').read_text())[1]) * 1024
                    peak = max(peak, rss); require(rss <= 134217728, 'recorder RSS bound')
            finally: f.signal('recorder', 'CONT')
            wait(lambda: any(int(n) > 0 for n in re.findall(r'kernel_drops=(\d+)', f.logs('recorder'))))
            log = f.logs('recorder')
            require('persistence=none' in log, 'recorder archive behavior')
            buffers = re.findall(r'socket_buffer_bytes=(\d+)', log)
            require(buffers and 0 < int(buffers[-1]) <= 2097152, 'socket storage bound')
            require(int(re.findall(r'kernel_drops=(\d+)', log)[-1]) > 0, 'saturation did not exercise kernel drops')
            policy = f.container('recorder')['HostConfig']['LogConfig']
            require(policy['Type'] == 'json-file' and policy['Config'] == {'max-file': '2', 'max-size': '1m'}, 'log storage bound')
            require(not any(m['Destination'] == '/captures' for m in f.container('recorder')['Mounts']), 'recorder archive mount')
            assert_progress(before, f.observer.snapshot(), ('data', 'context', 'spectra'))
            write_json(f.root / 'saturation.json', {'seconds': time.monotonic() - started, 'peak_rss': peak,
                'nominal_iq_payload_bytes_per_second': 16000000, 'wire_before': before,
                'wire_after': f.observer.snapshot(), 'recorder_before': recorder_before, 'recorder_after': log})
        elif case == 'P3-07': capture_continuity(f)
        elif case == 'P3-08': identity(f)
        elif case == 'P4-07':
            bridge = next(r for r in f.state_value()['resources'] if r['kind'] == 'ovs_bridge')
            owner = f.state_value()['owner_token']
            require(call('ovs-vsctl', 'get', 'Bridge', bridge['uuid'], 'external_ids:graphx_owner')[1].strip().strip('"') == owner, 'bridge ownership mismatch')
            call('ovs-vsctl', 'del-br', bridge['uuid'])
            require(f.command('status', ok=False)[0] != 0, 'bridge outage reported healthy')
            wait(lambda: 'results=stale' in f.logs('detector'))
            require(all(f.container(n)['RestartCount'] == 0 for n in NODES), 'automatic repair')
    finally: f.cleanup()


def export_cli(images, manifest, destination):
    """Copy only the independently verified CLI regular-file bytes; never unpack a layer."""
    record = manifest['images']['runtime']['inspection']['files']['usr/local/bin/graphx']
    with tarfile.open(images / 'runtime.oci.tar') as archive:
        index = json.load(archive.extractfile('index.json'))
        image = json.load(archive.extractfile('blobs/sha256/' + index['manifests'][0]['digest'][7:]))
        found = None
        for descriptor in image['layers']:
            with tarfile.open(fileobj=archive.extractfile('blobs/sha256/' + descriptor['digest'][7:]), mode='r|') as layer:
                for member in layer:
                    if member.name.removeprefix('./') == 'usr/local/bin/graphx':
                        require(member.isfile() and member.size < 128 * 1024 * 1024, 'invalid CLI member')
                        found = layer.extractfile(member).read()
        require(found and hashlib.sha256(found).hexdigest() == record['sha256'], 'CLI content identity mismatch')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('xb') as stream: stream.write(found)
    destination.chmod(0o555)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--prepare', action='store_true'); action.add_argument('--run', action='store_true')
    action.add_argument('--list-cases', action='store_true')
    parser.add_argument('--allow-privileged', action='store_true')
    parser.add_argument('--images', type=Path); parser.add_argument('--cli', type=Path)
    parser.add_argument('--preparation', type=Path, help='reviewed preparation.json required for a live run')
    parser.add_argument('--output', type=Path); parser.add_argument('--target', choices=('lima', 'native-linux'), default='lima')
    parser.add_argument('--run-root', type=Path, default=Path('/var/lib/graphx/verification/p34/run'), help='reviewed future guest run root for --prepare')
    parser.add_argument('--subnet', default='10.79.0.0/24'); parser.add_argument('--case', choices=list(CASES), action='append')
    args = parser.parse_args()
    if args.list_cases: print(json.dumps(CASES, indent=2)); return
    if not args.images or not args.output: parser.error('--images and --output are required')
    if args.run and (not args.allow_privileged or sys.platform != 'linux' or os.geteuid() != 0):
        parser.error('--run requires explicit --allow-privileged on the authorized Linux root runner')
    args.images = args.images.resolve(); args.output = args.output.resolve()
    if args.run and (not args.output.is_relative_to(Path('/var/lib/graphx')) or args.output == Path('/var/lib/graphx')):
        parser.error('live evidence must use a fresh child of /var/lib/graphx')
    require(not args.output.exists(), 'output must be absent')
    manifest = verify(args.images)
    require('vita' in manifest['images'] and manifest.get('qualification_hooks'), 'verified private VITA qualification images required')
    args.output.mkdir(parents=True)
    if args.prepare:
        export_cli(args.images, manifest, args.output / 'bin/graphx')
        require(args.run_root.is_absolute() and args.run_root.is_relative_to(Path('/var/lib/graphx')), 'planned run root must be guest-local')
        shutil.copytree(args.images / 'catalog', args.output / 'catalog')
        planned = {case: {name: graph_id(args.run_root / case / name) for name in fixture_names(case)} for case in CASES}
        for case, fixtures in planned.items():
            for name, identifier in fixtures.items():
                path = args.output / 'authored' / case / name
                path.mkdir(parents=True)
                authored = graph_document(identifier, args.subnet, 18080,
                    available=case.startswith('P4'), capture=case != 'P3-08' and name != 'False')
                authored['catalog'] = '../../../catalog/lock.json'
                write_json(path / 'graphx.yml', authored)
        write_json(args.output / 'preparation.json', {'source_commit': manifest['commit'], 'vrt_pin': VRT_PIN,
            'platform': manifest['platform'], 'dirty_candidate': manifest['dirty_candidate'],
            'images': {k: v['inspection']['digest'] for k, v in manifest['images'].items()},
            'cli_sha256': manifest['images']['runtime']['inspection']['files']['usr/local/bin/graphx']['sha256'],
            'harness_sha256': {p: hashlib.sha256((SOURCE / p).read_bytes()).hexdigest() for p in
                ('tests/test_vita_live.py', 'tests/vita_live_support.py', 'scripts/release/image_release.py', 'scripts/release/release_common.py')},
            'case_ids': list(CASES), 'run_root': str(args.run_root), 'planned_graph_ids': planned,
            'sentinel_graph_id': graph_id(args.run_root / 'sentinel'), 'subnet': args.subnet,
            'management_ports': 'ephemeral loopback ports selected and recorded by the compiler',
            'privileged_acceptance': 'NOT RUN', 'nominal_iq_payload_bytes_per_second': 16000000})
        print('Verified CLI extracted; no runtime resources created. Stage images and this directory on the guest.')
        return
    require(args.preparation is not None, '--run requires reviewed --preparation')
    prepared = json.loads(args.preparation.read_bytes())
    require(prepared['run_root'] == str(args.output) and prepared['subnet'] == args.subnet, 'run selection differs from reviewed preparation')
    require(prepared['images'] == {k: v['inspection']['digest'] for k, v in manifest['images'].items()}, 'images differ from reviewed preparation')
    for path, digest in prepared['harness_sha256'].items():
        require(path in ('tests/test_vita_live.py', 'tests/vita_live_support.py', 'scripts/release/image_release.py', 'scripts/release/release_common.py'), 'unexpected preparation source')
        require(hashlib.sha256((SOURCE / path).read_bytes()).hexdigest() == digest, 'harness changed since preparation: ' + path)
    require(args.cli is not None, '--run requires the extracted --cli')
    args.cli = args.cli.resolve()
    require(hashlib.sha256(args.cli.read_bytes()).hexdigest() == manifest['images']['runtime']['inspection']['files']['usr/local/bin/graphx']['sha256'], 'CLI differs from verified image')
    expected = 'linux/arm64' if os.uname().machine in ('aarch64', 'arm64') else 'linux/amd64'
    require(manifest['platform'] == expected, 'image/host architecture mismatch')
    import yaml  # Guest prerequisite, never required to list cases or verify offline images.
    for tool in ('docker', 'ovs-vsctl', 'ip', 'tc', 'nft', 'ethtool', 'dumpcap', 'tshark', 'nsenter'):
        require(shutil.which(tool), 'missing guest tool: ' + tool)
    call('docker', 'info'); call('docker', 'compose', 'version'); call('ovs-vsctl', '--timeout=5', 'show')
    network = ipaddress.ip_network(args.subnet)
    routes = json.loads(call('ip', '-j', 'route', 'show', 'table', 'all')[1])
    require(not any(r.get('dst') not in (None, 'default') and '/' in r['dst'] and
                    ipaddress.ip_network(r['dst'], strict=False).overlaps(network) for r in routes), 'subnet overlaps existing routes')
    before = inventory(); write_json(args.output / 'before.json', before)
    report = {case: {'status': 'NOT RUN'} for case in CASES}
    sentinel = Fixture(args, args.output / 'sentinel', sentinel=True)
    def interrupted(signum, _frame): raise InterruptedError(f'signal {signum}')
    signal.signal(signal.SIGTERM, interrupted); signal.signal(signal.SIGINT, interrupted)
    signal.signal(signal.SIGALRM, interrupted); signal.alarm(3300)
    try:
        sentinel.up(); sentinel_ids = {n: sentinel.container(n)['Id'] for n in ('source', 'transform', 'sink', 'platform')}
        for case in args.case or CASES:
            sentinel_before = sentinel.logs('sink')
            current = inventory(); directory = args.output / case; directory.mkdir()
            start = time.monotonic()
            try:
                run_case(args, case, directory)
                preserved(current, inventory())
                require(all(sentinel.container(n, True)['Id'] == v for n, v in sentinel_ids.items()), 'sentinel changed')
                require(sentinel.logs('sink') != sentinel_before, 'sentinel stopped making progress')
                report[case] = {'status': 'PASS', 'seconds': time.monotonic() - start}
            except Exception as error:
                report[case] = {'status': 'FAIL', 'reason': str(error), 'seconds': time.monotonic() - start}
                raise
            finally: write_json(args.output / 'results.json', report)
    finally:
        signal.alarm(0)
        try: sentinel.cleanup()
        finally:
            after = inventory(); write_json(args.output / 'after.json', after)
            write_json(args.output / 'results.json', report)
            preserved(before, after)
    require(all(report[c]['status'] == 'PASS' for c in args.case or CASES), 'requested cases incomplete')
    print('Requested cases passed; see results.json. Unselected cases remain NOT RUN.')


if __name__ == '__main__':
    try: main()
    except (Exception, KeyboardInterrupt) as error:
        print('graphx-vita-live: ' + str(error), file=sys.stderr); sys.exit(1)
