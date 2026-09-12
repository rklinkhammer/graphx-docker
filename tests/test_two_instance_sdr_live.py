#!/usr/bin/env python3
"""Explicit two-instance SDR resilience acceptance on a ready Docker engine.

Runs real simulators/controllers. No privileged networking or QEMU is implied.
Interrupted startup uses a child-only interception of the normal launcher; there
is no production failpoint or alternative lifecycle implementation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import uuid

from test_compose_instance_live import wait_for

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / 'examples/sdr-node/two-source/graphx.yaml'
CLI = Path(os.environ.get('GRAPHX_BIN', ROOT / 'build/dev/graphx')).resolve()
NODES = ('sdr-east', 'processor-east', 'sdr-west', 'processor-west')


def execute(args, check=True, input=None, timeout=180):
    result = subprocess.run([str(x) for x in args], input=input, text=True,
                            capture_output=True, timeout=timeout)
    if check and result.returncode:
        raise AssertionError(result.stderr + result.stdout)
    return result


# Kill after Compose has started the recorded containers, before execution gates
# are released. The receipt, registrations, and Docker objects are all real.
INTERRUPT = '''import importlib.util, os, signal, sys
spec = importlib.util.spec_from_file_location('instance', sys.argv[1])
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
sys.argv = sys.argv[1:]
original = m.run
def run(args, **kwargs):
    result = original(args, **kwargs)
    if len(args) > 2 and str(args[0]) == 'docker' and str(args[1]) == 'compose' and args[-1] == 'start':
        os.kill(os.getpid(), signal.SIGKILL)
    return result
m.run = run
os.umask(0o077)
m.main()
'''

# Test injection uses stdin, never credentials in argv or evidence. The packet is
# sent inside the receiver's private namespace, including for cross-scope probes.
INJECT = '''import hashlib,hmac,json,secrets,socket,sys,time
x=json.load(sys.stdin); p=x['payload']; t=int(time.time()*1000); n=secrets.token_hex(16)
c=json.dumps(p,separators=(',',':'))
a=hmac.new(x['secret'].encode(),f'{t}.{n}.{c}'.encode(),hashlib.sha256).hexdigest()
socket.socket(socket.AF_INET,socket.SOCK_DGRAM).sendto(json.dumps({'payload':p,'auth':{'timestamp':t,'nonce':n,'signature':a}},separators=(',',':')).encode(),('127.0.0.1',9000))
'''


class Deployment:
    def __init__(self, root, name, port):
        self.name, self.port = name, port
        self.args = [CONFIG, '--graphx', CLI, '--state-root', root,
                     '--set', f'deployment.instance_id={name}', '--port', port]
        self.config = json.loads(execute([CLI, 'config', 'normalize', CONFIG,
            '--set', f'deployment.instance_id={name}']).stdout)
        self.project = json.loads(execute([CLI, 'config', 'normalize', CONFIG, '--resources',
            '--set', f'deployment.instance_id={name}']).stdout)['deployment']['project']
        self.directory = root / self.config['deployment']['resource_key']
        self.receipt_path = self.directory / 'containers.json'
        self.manifest_path = self.directory / 'identity/identities.json'

    def launch(self, action, check=True, node=None, build=False):
        return execute([sys.executable, ROOT / 'scripts/instance.py', action, *self.args,
                        *(['--node', node] if node else []), *(['--build'] if build else [])], check, timeout=1800 if build else 180)

    def receipt(self):
        return json.loads(self.receipt_path.read_text())

    def executions(self):
        return {n['id']: n.get('execution_id') for n in json.loads(self.manifest_path.read_text())['nodes']}

    def token(self):
        return (self.directory / 'identity/operator.token').read_text()

    def request(self, path, body=None, token=None, key=None):
        headers = {}
        if token:
            headers['Authorization'] = 'Bearer ' + token
        if body is not None:
            headers['Content-Type'] = 'application/json'
            headers['Idempotency-Key'] = key or uuid.uuid4().hex
        request = urllib.request.Request(f'http://127.0.0.1:{self.port}{path}',
            data=json.dumps(body).encode() if body is not None else None, headers=headers)
        try:
            response = urllib.request.urlopen(request, timeout=4)
        except urllib.error.HTTPError as response:
            return response.code, json.loads(response.read())
        with response:
            return response.status, json.loads(response.read())

    def snapshot(self):
        return self.request('/api/topology')[1]

    def ready(self, since=0):
        def observed():
            snapshot = self.snapshot()
            return snapshot['instanceId'] == self.name and all(
                snapshot['nodes'][node].get('executionId') == execution and
                (snapshot['nodes'][node].get('lastSeen') or 0) > since
                for node, execution in self.executions().items())
        wait_for(observed)

    def container(self, node):
        return next(identity for identity, labels in self.receipt()['containers'].items()
                    if labels.get('org.graphx.node') == node)

    def sequences(self):
        values = {}
        for side, frequency in [('east', 100000000), ('west', 200000000)]:
            logs = execute(['docker', 'logs', '--tail', '100', self.container('processor-' + side)]).stdout
            rows = [json.loads(line) for line in logs.splitlines() if line.startswith('{')]
            rows = [row for row in rows if row.get('frequency_hz') == frequency]
            values[side] = rows[-1]['sequence'] if rows else -1
        return values

    def advancing(self):
        before = self.sequences()
        wait_for(lambda: all(value >= 0 and value != before[side] for side, value in self.sequences().items()))

    def command(self, action, node='processor-east', execution=None, token=None, scope=None, key=None):
        return self.request('/api/control/commands', {
            'action': action, 'targetNodes': [node], 'graphId': self.config['graph']['id'],
            'instanceId': scope or self.name,
            'targetExecutions': {node: execution or self.executions()[node]}},
            token=self.token() if token is None else token, key=key)

    def accepted(self, action, node='processor-east', key=None):
        status, response = self.command(action, node, key=key)
        assert status == 202, response
        command = response['command']
        def complete():
            result = self.request('/api/control/commands/' + command['id'], token=self.token())[1]
            assert result['status'] not in ('rejected', 'timed-out'), result
            return result['status'] == 'accepted'
        wait_for(complete)

    def inject(self, marker, execution, scope=None, secret=None):
        payload = {'kind': 'trace', 'event': 'heartbeat', 'nodeId': 'sdr-east',
                   'graphId': self.config['graph']['id'], 'instanceId': scope or self.name,
                   'executionId': execution, 'timestamp': int(time.time() * 1000), 'sequence': marker}
        secret = secret or (self.directory / 'identity/sdr-east.secret').read_text()
        execute(['docker', 'exec', '-i', self.container('sdr-west'), 'python3', '-c', INJECT],
                input=json.dumps({'secret': secret, 'payload': payload}))

    def stable_resources(self):
        receipt = self.receipt()
        ids = list(receipt['containers'])
        objects = json.loads(execute(['docker', 'inspect', *ids]).stdout)
        assert all(item['State']['Running'] for item in objects)
        return {item['Id']: item['State']['StartedAt'] for item in objects}, receipt['networks']

    def history(self):
        path = next((self.directory / 'history').glob('*/history.sqlite'))
        # Read through the same Linux engine as the writer. Host SQLite locks on
        # an OrbStack bind mount must not overlap the guest writer's WAL locks.
        script = """const {DatabaseSync}=require('node:sqlite');
const d=new DatabaseSync(process.argv[1],{readOnly:true});
try { const integrity=d.prepare('PRAGMA quick_check').get().quick_check;
if(integrity!=='ok') throw new Error(integrity);
console.log(JSON.stringify({scope:d.prepare("SELECT value FROM history_metadata WHERE key='graph_id'").get().value,
rows:d.prepare("SELECT data_json FROM history_records WHERE kind='trace'").all().map(r=>JSON.parse(r.data_json))}));
} finally {d.close();}"""
        uid, gid = os.getuid() or 65532, os.getgid() if os.getuid() else 65532
        value = json.loads(execute(['docker', 'run', '--rm', '--network', 'none', '--read-only',
            '--user', f'{uid}:{gid}', '--mount', f'type=bind,src={self.directory / "history"},dst=/history',
            '--entrypoint', 'node', 'graphx-telemetry:latest', '-e', script,
            '/history/' + str(path.relative_to(self.directory / 'history'))]).stdout)
        assert json.loads(value['scope']) == [self.config['graph']['id'], self.name]
        rows = value['rows']
        assert rows and all(row.get('instanceId') == self.name for row in rows)
        return rows


def proof(root, evidence, build=False):
    suffix = uuid.uuid4().hex[:10]
    reservations = [socket.socket() for _ in range(2)]
    for endpoint in reservations:
        endpoint.bind(('127.0.0.1', 0))
    ports = [endpoint.getsockname()[1] for endpoint in reservations]
    for endpoint in reservations:
        endpoint.close()
    a, b = [Deployment(root, f'proof-{suffix}-{side}', port) for side, port in zip(('a', 'b'), ports)]
    cleanup = True
    try:
        a.launch('up', build=build); b.launch('up')
        a.ready(); b.ready(); a.advancing(); b.advancing()
        assert a.config['graph'] == b.config['graph'], 'proof must deploy the same logical topology'
        assert a.executions().keys() == b.executions().keys() == set(NODES)
        first_a, first_b = a.executions(), b.executions()
        assert set(first_a.values()).isdisjoint(first_b.values())
        assert set(a.receipt()['containers']).isdisjoint(b.receipt()['containers'])
        assert set(a.receipt()['networks']).isdisjoint(b.receipt()['networks'])
        a_secrets = {(a.directory / 'identity' / (node + '.secret')).read_text() for node in NODES}
        b_secrets = {(b.directory / 'identity' / (node + '.secret')).read_text() for node in NODES}
        assert len(a_secrets | b_secrets) == 8
        b_resources, b_manifest = b.stable_resources(), b.manifest_path.read_bytes()
        assert a.launch('up', False).returncode != 0
        assert a.executions() == first_a
        assert a.command('pause', token=b.token())[0] in (401, 403)
        assert a.command('pause', scope=b.name)[0] == 409
        a.accepted('pause', key='before-restart')
        time.sleep(1)  # drain packets sent before the acknowledged device stop
        paused = a.sequences()['east']
        b.advancing()
        time.sleep(1)
        assert a.sequences()['east'] == paused, 'pause did not stop the selected source'
        a.accepted('resume'); a.advancing()
        a.launch('restart', node='processor-east'); a.ready(); a.advancing()
        assert a.executions()['processor-east'] != first_a['processor-east']
        assert all(a.executions()[n] == first_a[n] for n in NODES if n != 'processor-east')
        assert a.command('pause', execution=first_a['processor-east'])[0] == 409
        assert a.command('pause', key='before-restart')[0] == 409
        a.accepted('pause'); a.accepted('resume')
        a.launch('restart', node='sdr-east'); a.ready(); a.advancing()
        fresh_source = a.executions()['sdr-east']
        assert fresh_source != first_a['sdr-east']
        a.inject(4000000001, fresh_source)  # valid positive control for injection/history
        a.inject(4000000002, first_a['sdr-east'])
        a.inject(4000000003, fresh_source, scope=b.name)
        a.inject(4000000004, fresh_source, secret=(b.directory / 'identity/sdr-east.secret').read_text())
        before_collector = a.executions()
        since = time.time() * 1000
        a.launch('restart', node='telemetry'); a.ready(since); a.advancing()
        assert a.executions() == before_collector
        assert b.stable_resources() == b_resources and b.manifest_path.read_bytes() == b_manifest
        a.launch('down'); b.advancing(); b.accepted('pause'); b.accepted('resume')
        rows = a.history()
        markers = {row.get('sequence') for row in rows}
        assert 4000000001 in markers, 'positive telemetry control was not ingested'
        assert not {4000000002, 4000000003, 4000000004} & markers, 'stale/cross-instance observation retained'
        assert {first_a['sdr-east'], fresh_source} <= {row.get('executionId') for row in rows}
        assert b.stable_resources() == b_resources and b.executions() == first_b
        interrupted = execute([sys.executable, '-c', INTERRUPT, ROOT / 'scripts/instance.py', 'up', *a.args], False)
        assert interrupted.returncode == -signal.SIGKILL, interrupted.stderr
        interrupted_ids = a.executions()
        assert all(interrupted_ids[n] != before_collector[n] for n in NODES)
        assert a.launch('up', False).returncode != 0
        a.launch('status')
        assert b.stable_resources() == b_resources; b.advancing()
        # A same-project replacement must not be adopted by recovery.
        original = a.container('processor-east')
        execute(['docker', 'stop', original]); execute(['docker', 'rm', original])
        replacement = execute(['docker', 'create', '--label', 'com.docker.compose.project=' + a.receipt()['project'],
            '--entrypoint', '/bin/sleep', 'graphx-demo:latest', 'infinity']).stdout.strip()
        try:
            assert a.launch('down', False).returncode != 0, 'replacement accepted during cleanup'
            assert b.stable_resources() == b_resources
        finally:
            execute(['docker', 'rm', replacement])
        a.launch('down')  # recover only recorded objects, then retire stopped executions
        assert not any(a.executions().values())
        a.launch('up'); a.ready(); a.advancing()
        assert all(a.executions()[n] != interrupted_ids[n] for n in NODES)
        assert a.command('pause', execution=before_collector['processor-east'])[0] == 409
        a.launch('down'); b.advancing()
        assert b.stable_resources() == b_resources and b.manifest_path.read_bytes() == b_manifest
        b.launch('down')
        b_rows = b.history()
        assert {row.get('executionId') for row in b_rows} <= set(first_b.values())
        evidence.update(status='passed', instances=[a.name, b.name], history_rows=[len(a.history()), len(b_rows)],
            checks=['simultaneous raw SDR traffic', 'distinct resources and credentials', 'authenticated pause/resume',
                    'cross-instance credential/target denial', 'node restart and stale target/idempotency rejection',
                    'stale/cross-instance telemetry rejection with positive control', 'collector restart and continued traffic',
                    'isolated shutdown', 'SIGKILL during startup', 'replacement refusal', 'owned cleanup and fresh reactivation'])
    except BaseException as error:
        evidence['error'] = str(error)
        evidence['diagnostics'] = {}
        for deployment in (a, b):
            if deployment.receipt_path.exists():
                logs = {}
                for identity in deployment.receipt()['containers']:
                    result = execute(['docker', 'logs', '--tail', '20', identity], False)
                    logs[identity] = (result.stdout + result.stderr)[-12000:]
                evidence['diagnostics'][deployment.name] = logs
                try:
                    evidence['diagnostics'][deployment.name]['snapshot'] = deployment.snapshot()
                except (OSError, ValueError):
                    pass
        raise
    finally:
        for deployment in (a, b):
            if deployment.receipt_path.exists():
                result = deployment.launch('down', False)
                if result.returncode:
                    cleanup = False
                    print(result.stderr, file=sys.stderr)
        for deployment in (a, b):
            containers = execute(['docker', 'ps', '-aq', '--filter', 'label=com.docker.compose.project=' + deployment.project]).stdout.strip()
            networks = execute(['docker', 'network', 'ls', '-q', '--filter', 'label=com.docker.compose.project=' + deployment.project]).stdout.strip()
            cleanup = cleanup and not containers and not networks
        evidence['cleanup'] = bool(cleanup and not a.receipt_path.exists() and not b.receipt_path.exists())
        if not evidence['cleanup']:
            print(f'Preserved recovery state: {root}', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--environment', choices=['orbstack', 'lima', 'native-linux'])
    parser.add_argument('--build', action='store_true', help='build current images before starting the proof')
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    engine = execute(['docker', 'info', '--format', '{{.OSType}}/{{.Architecture}}']).stdout.strip()
    execute(['docker', 'compose', 'version'])
    evidence = {'status': 'failed', 'environment': args.environment or ('orbstack' if platform.system() == 'Darwin' else 'linux-docker'), 'host': platform.system() + '/' + platform.machine(), 'engine': engine,
                'config_sha256': hashlib.sha256(CONFIG.read_bytes()).hexdigest(),
                'ovs': 'not exercised', 'qemu': 'not exercised'}
    parent = '/var/lib/graphx/runtime' if platform.system() == 'Linux' else None
    root = Path(tempfile.mkdtemp(prefix='graphx-two-instance-', dir=parent))
    try:
        proof(root, evidence, args.build)
    finally:
        args.evidence.parent.mkdir(parents=True, exist_ok=True)
        args.evidence.write_text(json.dumps(evidence, indent=2) + '\n')
        if evidence.get('cleanup') and evidence['status'] == 'passed':
            shutil.rmtree(root)
        else:
            print(f'Recovery state retained at {root}', file=sys.stderr)
    print('PASS: two simultaneous SDR instances, restart, interruption/recovery, and isolated shutdown')
    print(f'Evidence: {args.evidence}')


if __name__ == '__main__':
    main()
