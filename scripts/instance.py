#!/usr/bin/env python3
"""Compose lifecycle consuming the authoritative GraphX normalized contract.

Generated Compose and the container receipt are runtime artifacts, never topology
inputs. The existing runtime identity manifest remains the registration authority.
"""
import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import socket
import stat
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'infrastructure/compose/services.yaml'
COMMANDS = {
    **{f'graphx-{name}': ('native', [f'/usr/local/bin/graphx-{name}'])
       for name in ('generator', 'transform', 'sink', 'udp-publisher', 'udp-subscriber')},
    'sdr-source': ('sdr', ['python3', 'common/sdr_simulator.py']),
    'sdr-controller': ('sdr', ['python3', 'common/processor.py']),
}
# A container can start its registered execution only once, even if someone uses
# docker restart outside this launcher. OVS containers wait until attachment.
GATE = '''set -eu
mkdir /gate/started || { echo 'Execution already consumed; use instance down then up' >&2; exit 1; }
while [ ! -f /gate/ready ]; do sleep 0.1; done
exec "$@"
'''


def run(args, *, capture=False, env=None):
    return subprocess.run([str(x) for x in args], check=True, text=True,
                          stdout=subprocess.PIPE if capture else None, env=env).stdout


def write(path, value, mode=0o600):
    """Replace a generated artifact atomically inside our protected directory."""
    temporary = path.with_name(path.name + '.' + secrets.token_hex(8))
    fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    os.fchmod(fd, mode)
    with os.fdopen(fd, 'w') as stream:
        stream.write(value if isinstance(value, str) else json.dumps(value, indent=2) + '\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def protected_directory(path):
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    info = path.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
        raise ValueError(f'expected owned mode-0700 directory: {path}')


@contextlib.contextmanager
def lock(path):
    fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_uid != os.geteuid() or info.st_mode & 0o077:
            raise ValueError('unsafe instance lock')
        fcntl.flock(fd, fcntl.LOCK_EX)
        yield
    finally:
        os.close(fd)


def load(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd) as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.geteuid() or info.st_nlink != 1 or info.st_mode & 0o022:
            raise ValueError(f'unsafe runtime artifact: {path}')
        raw = stream.read(4 * 1024 * 1024 + 1)
        if len(raw) > 4 * 1024 * 1024:
            raise ValueError('runtime artifact too large')
        return json.loads(raw)


def validate(config):
    if config.get('contract_version') != 1 or not config['deployment'].get('instance_id'):
        raise ValueError('an explicit deployment.instance_id is required')
    if not config['deployment'].get('project'):
        raise ValueError('deployment.project is required')
    nodes = {n['id']: n for n in config['graph']['nodes']}
    services = config['deployment']['services']
    if {s['node_id'] for s in services} != set(nodes) or len(services) != len(nodes):
        raise ValueError('deployment.services must select exactly one service per node')
    if {'telemetry', 'namespace'} & set(nodes):
        raise ValueError('telemetry and namespace are reserved runtime service names')
    for service in services:
        node = nodes[service['node_id']]
        if service['command'] not in COMMANDS:
            raise ValueError(f"unsupported process adapter: {service['command']}")
        if service['command'] == 'sdr-source' and 'sdr' not in node:
            raise ValueError('sdr-source requires typed node.sdr settings')
        if service['command'] == 'sdr-controller' and sum(
                n.get('sdr') is not None and next(e for e in config['graph']['edges'] if e['id'] == n['sdr']['control_edge'])['from']['node'] == node['id']
                for n in nodes.values()) != 1:
            raise ValueError('sdr-controller must select exactly one typed source')
        if node['runtime'] not in ('process', 'container') or node['lifecycle'] != 'managed':
            raise ValueError('this Compose launcher requires managed process/container nodes')
    network = config['network']
    if any(a['kind'] != 'container_veth' for a in network['attachments']) or network['routers'] or network['captures'] or network['faults']:
        raise ValueError('specialized namespace, TAP, capture, and fault labs use their documented launchers')
    edges = {e['id']: e for e in config['graph']['edges']}
    for source in nodes.values():
        if 'sdr' not in source:
            continue
        settings = source['sdr']
        for credentials in (settings['credentials'], edges[settings['control_edge']]['transport']['tls']):
            if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.-]{0,252}', credentials['server_name']):
                raise ValueError('managed SDR server_name must be a DNS name')
            for name in ('ca_file', 'certificate_file', 'private_key_file'):
                path = Path(credentials[name])
                if len(path.parts) != 4 or path.parts[1] != 'run' or path.parts[2] not in nodes or '..' in path.parts:
                    raise ValueError('managed SDR credentials must be /run/NODE/FILENAME')
    sdr = any('sdr' in node for node in nodes.values())
    # Loopback SDR examples share one private network namespace per instance.
    shared = sdr and not network['attachments']
    if shared:
        ports = {('tcp', 8080), ('udp', 9000)}
        for edge in edges.values():
            transport = edge['transport']
            host = transport.get('host', transport.get('destination'))
            key = (transport['kind'], transport.get('port'))
            if host != '127.0.0.1' or transport.get('bind') != '127.0.0.1' or key in ports:
                raise ValueError('shared SDR namespace requires distinct loopback transport endpoints')
            ports.add(key)
    for edge in edges.values():
        if edge['transport']['kind'] not in ('tcp', 'udp'):
            raise ValueError('shared-memory and Unix socket examples use their dedicated launchers')
    return shared


def render(config, resolved, directory, port, executions=None, capture=True, history=True):
    shared = validate(config)
    capture = capture and bool(config['observability']['capture']['provider'])
    executions = executions or {}
    uid = os.getuid() or 65532
    gid = os.getgid() if os.getuid() else 65532
    project = resolved['deployment']['project']
    identity = config['deployment']['instance_id']
    graph = config['graph']['id']
    def mount(source, target, readonly=True):
        return {'type': 'bind', 'source': str(source), 'target': target, 'read_only': readonly,
                'bind': {'create_host_path': False}}
    def service(kind):
        dockerfile = {'native': 'Dockerfile', 'sdr': 'examples/sdr-node/Dockerfile.services', 'telemetry': 'docker/telemetry.Dockerfile'}[kind]
        return {'extends': {'file': str(BASE), 'service': kind},
                'build': {'context': str(ROOT), 'dockerfile': dockerfile,
                          'secrets': ['graphx_ca', 'graphx_cert_installer'],
                          'args': {'GRAPHX_BUILD_TRUST_FINGERPRINT': os.environ.get('GRAPHX_BUILD_TRUST_FINGERPRINT', 'graphx-trust-v1-none')}},
                'user': f'{uid}:{gid}',
                'labels': {'org.graphx.graph': graph, 'org.graphx.instance': identity},
                'networks': ['management']}
    collector = service('telemetry')
    collector['labels']['org.graphx.role'] = 'telemetry'
    collector['ports'] = [f'127.0.0.1:{port}:8080']
    collector['environment'] = {
        'GRAPHX_NORMALIZED_CONFIG': '/run/config/normalized.json',
        'GRAPHX_RUNTIME_IDENTITY_FILE': '/run/identity/identities.json',
        'GRAPHX_CONTROL_POLICY_FILE': '/run/identity/policy.json',
        'GRAPHX_HTTP_BIND': '0.0.0.0', 'GRAPHX_TELEMETRY_BIND': '0.0.0.0',
        'GRAPHX_ALLOW_INSECURE_REMOTE': 'true',
        'GRAPHX_ALLOWED_ORIGINS': f'http://127.0.0.1:{port},http://localhost:{port}',
        'GRAPHX_HISTORY_ENABLED': str(history).lower(),
        'GRAPHX_HISTORY_DATABASE_FILE': '/history/history.sqlite',
        'GRAPHX_CAPTURE_ENABLED': str(capture).lower(), 'GRAPHX_CAPTURE_DIR': '/captures',
    }
    collector['volumes'] = [mount(directory / 'normalized.json', '/run/config/normalized.json'),
                            mount(directory / 'identity', '/run/identity'),
                            mount(directory / 'history', '/history', False),
                            mount(directory / 'captures', '/captures')]
    services = {'telemetry': collector}
    if shared:
        holder = service('native')
        holder['labels']['org.graphx.role'] = 'namespace'
        holder['entrypoint'] = ['/bin/sleep']
        holder['command'] = ['infinity']
        holder['ports'] = collector.pop('ports')
        services['namespace'] = holder
        collector.pop('networks')
        collector['network_mode'] = 'service:namespace'
        collector['depends_on'] = {'namespace': {'condition': 'service_started'}}
    edges = {e['id']: e for e in config['graph']['edges']}
    nodes = {n['id']: n for n in config['graph']['nodes']}
    for selected in config['deployment']['services']:
        node = selected['node_id']
        kind, command = COMMANDS[selected['command']]
        item = service(kind)
        item['image'] = selected['image']
        execution = executions.get(node, 'PENDING')
        item['labels'].update({'org.graphx.node': node, 'org.graphx.execution': execution})
        item['entrypoint'] = ['/bin/sh', '-c', GATE, 'graphx-execution']
        item['command'] = command
        item['environment'] = {
            'GRAPHX_CONFIG': '/etc/graphx/graphx.yaml',
            'GRAPHX_OVERRIDES': os.environ.get('GRAPHX_OVERRIDES', ''),
            'GRAPHX_NORMALIZED_CONFIG': '/run/config/normalized.json',
            'GRAPHX_NODE_ID': node, 'GRAPHX_EXECUTION_ID': execution, 'PYTHONUNBUFFERED': '1',
            'GRAPHX_TELEMETRY_SHARED_SECRET_FILE': '/run/node.secret',
            'GRAPHX_CAPTURE_ENABLED': str(capture).lower(), 'GRAPHX_CAPTURE_DIR': '/captures',
        }
        item['volumes'] = [mount(directory / 'graphx.yaml', '/etc/graphx/graphx.yaml'),
                           mount(directory / 'normalized.json', '/run/config/normalized.json'),
                           mount(directory / 'identity' / (node + '.secret'), '/run/node.secret'),
                           mount(directory / 'gates' / execution / node, '/gate', False),
                           mount(directory / 'captures', '/captures', False)]
        credentials = []
        if kind == 'sdr':
            source = nodes[node] if 'sdr' in nodes[node] else next(n for n in nodes.values() if n.get('sdr') and edges[n['sdr']['control_edge']]['from']['node'] == node)
            settings = source['sdr']
            credentials = [settings['credentials'] if source['id'] == node else edges[settings['control_edge']]['transport']['tls']]
        for credential in credentials:
            for key in ('ca_file', 'certificate_file', 'private_key_file'):
                target = credential[key]
                item['volumes'].append(mount(directory / 'tls' / target.removeprefix('/run/'), target))
        item['depends_on'] = {'telemetry': {'condition': 'service_healthy'}}
        if shared:
            item.pop('networks')
            item['network_mode'] = 'service:namespace'
        services[node] = item
    return {'name': project, 'services': services, 'networks': {'management': {'driver': 'bridge'}},
            'secrets': {'graphx_ca': {'file': os.environ.get('GRAPHX_CA_CERT', '/dev/null')},
                        'graphx_cert_installer': {'file': os.environ.get('GRAPHX_CERT_INSTALL_SCRIPT', '/dev/null')}}}


def provision_tls(config, directory):
    edges = {edge['id']: edge for edge in config['graph']['edges']}
    for node in config['graph']['nodes']:
        if 'sdr' not in node:
            continue
        settings = node['sdr']
        ca = directory / 'tls' / node['id'] / 'authority'
        ca.mkdir(parents=True, mode=0o700)
        def openssl(*args):
            subprocess.run(['openssl', *map(str, args)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        openssl('req', '-x509', '-newkey', 'rsa:2048', '-sha256', '-nodes', '-days', '7',
                '-subj', '/CN=GraphX-instance-SDR', '-addext', 'basicConstraints=critical,CA:TRUE',
                '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
                '-keyout', ca / 'ca.key', '-out', ca / 'ca.pem')
        for server, credentials in [(True, settings['credentials']), (False, edges[settings['control_edge']]['transport']['tls'])]:
            paths = {key: directory / 'tls' / credentials[key].removeprefix('/run/')
                     for key in ('ca_file', 'certificate_file', 'private_key_file')}
            for path in paths.values():
                path.parent.mkdir(parents=True, exist_ok=True)
            extension = ca / 'extensions'
            write(extension, ('subjectAltName=DNS:' + credentials['server_name'] + '\nextendedKeyUsage=serverAuth\n') if server else 'extendedKeyUsage=clientAuth\n')
            openssl('req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=GraphX-node',
                    '-keyout', paths['private_key_file'], '-out', ca / 'request.csr')
            openssl('x509', '-req', '-sha256', '-days', '7', '-in', ca / 'request.csr',
                    '-CA', ca / 'ca.pem', '-CAkey', ca / 'ca.key', '-CAcreateserial',
                    '-extfile', extension, '-out', paths['certificate_file'])
            write(paths['ca_file'], (ca / 'ca.pem').read_text(), 0o644)
            for path in paths.values():
                os.chmod(path, 0o644)  # only selected files are mounted into each node


class Runtime:
    def __init__(self, args):
        self.args = args
        self.cli = Path(args.graphx).resolve()
        self.config_path = Path(args.config).resolve()
        self.config = json.loads(run([self.cli, 'config', 'normalize', self.config_path], capture=True))
        self.resolved = json.loads(run([self.cli, 'config', 'normalize', self.config_path, '--resources'], capture=True))
        validate(self.config)
        root = Path(args.state_root).absolute()
        self.directory = root / self.config['deployment']['resource_key']
        self.receipt = self.directory / 'containers.json'
        self.compose_file = self.directory / 'compose.json'
        self.project = self.resolved['deployment']['project']
        self.managed = bool(self.config['network']['attachments'])
        self.digest = hashlib.sha256(self.config_path.read_bytes() + json.dumps(self.config, sort_keys=True).encode()).hexdigest()
        self.compose = ['docker', 'compose', '-p', self.project, '-f', self.compose_file]

    def preflight(self):
        if platform.system() == 'Darwin':
            if self.managed:
                raise ValueError('OVS instances must run inside the GraphX Lima guest; see docs/compose-runtime.md')
            if run(['docker', 'context', 'show'], capture=True).strip() != 'orbstack':
                raise ValueError('portable macOS Compose workloads require the orbstack context')
        if self.managed and (platform.system() != 'Linux' or os.geteuid() != 0):
            raise ValueError('OVS startup requires an explicitly authorized root invocation on Linux or Lima')
        run(['docker', 'info'], capture=True)
        run(['docker', 'compose', 'version'], capture=True)

    def register(self, node, retire=False, execution=None):
        command = [self.cli, 'runtime', 'retire' if retire else 'activate', self.config_path,
                   '--identity-file', self.directory / 'identity/identities.json', '--node', node]
        if retire:
            command += ['--execution-id', execution]
        result = run(command, capture=True).strip()
        os.chmod(self.directory / 'identity/identities.json', 0o644)
        return result

    def manifest(self):
        manifest = load(self.directory / 'identity/identities.json')
        if (not isinstance(manifest, dict) or set(manifest) != {'version', 'graph_id', 'instance_id', 'nodes'} or
                manifest['version'] != 2 or manifest['graph_id'] != self.config['graph']['id'] or
                manifest['instance_id'] != self.config['deployment']['instance_id'] or
                not isinstance(manifest['nodes'], list)):
            raise ValueError('registration scope or format changed; refusing mutation')
        seen = set()
        for node in manifest['nodes']:
            if (not isinstance(node, dict) or set(node) - {'id', 'secret_file', 'execution_id'} or
                    not isinstance(node.get('id'), str) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]{0,63}', node['id']) or
                    node['id'] in seen or node.get('secret_file') != node['id'] + '.secret'):
                raise ValueError('registration node or credential mapping changed; refusing mutation')
            seen.add(node['id'])
            execution = node.get('execution_id')
            if execution is not None and (not isinstance(execution, str) or
                    not re.fullmatch(r'[0-9a-f]{32}', execution) or execution == '0' * 32):
                raise ValueError('invalid registered execution')
        return manifest

    def inventory(self):
        ids = run(['docker', 'ps', '-aq', '--filter', f'label=com.docker.compose.project={self.project}'], capture=True).split()
        return json.loads(run(['docker', 'inspect', *ids], capture=True)) if ids else []

    def networks(self):
        ids = run(['docker', 'network', 'ls', '-q', '--filter', f'label=com.docker.compose.project={self.project}'], capture=True).split()
        return json.loads(run(['docker', 'network', 'inspect', *ids], capture=True)) if ids else []

    def verify_receipt(self, receipt):
        if receipt['digest'] != self.digest or receipt['project'] != self.project:
            raise ValueError('runtime configuration drift; use the original config and overrides')
        current = {item['Id']: item for item in self.inventory()}
        if set(current) - set(receipt['containers']):
            raise ValueError('unrecorded/replaced project containers; refusing mutation')
        for identity, labels in receipt['containers'].items():
            if identity in current:
                actual = current[identity]['Config']['Labels']
                if any(actual.get(key) != value for key, value in labels.items()):
                    raise ValueError('container identity mismatch; refusing mutation')
        return current

    def up(self):
        if self.receipt.exists() or self.inventory() or self.networks():
            raise ValueError('instance already active or interrupted; use status/down before up')
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as endpoint:
            endpoint.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            endpoint.bind(('127.0.0.1', self.args.port))
            endpoint.listen(1)
        # An unfinished registration without a receipt must not be silently overwritten.
        manifest = self.directory / 'identity/identities.json'
        if manifest.exists() and any(n.get('execution_id') for n in self.manifest()['nodes']):
            raise ValueError('active registrations without a receipt; refusing takeover')
        for name in ('identity', 'captures', 'history', 'gates'):
            path = self.directory / name
            path.mkdir(exist_ok=True)
            os.chmod(path, 0o755)
            if os.geteuid() == 0 and name in ('captures', 'history'):
                os.chown(path, 65532, 65532)
        write(self.directory / 'graphx.yaml', self.config_path.read_text(), 0o644)
        write(self.directory / 'normalized.json', self.config, 0o644)
        nodes = self.config['graph']['nodes']
        for node in nodes:
            secret = self.directory / 'identity' / (node['id'] + '.secret')
            if not secret.exists():
                write(secret, secrets.token_hex(32), 0o644)
        write(manifest, {'version': 2, 'graph_id': self.config['graph']['id'],
                         'instance_id': self.config['deployment']['instance_id'],
                         'nodes': [{'id': n['id'], 'secret_file': n['id'] + '.secret'} for n in nodes]}, 0o644)
        if not (self.directory / 'identity/operator.token').exists():
            write(self.directory / 'identity/operator.token', secrets.token_hex(32), 0o644)
            write(self.directory / 'identity/policy.json', {
                'version': 2, 'graph_id': self.config['graph']['id'],
                'instance_id': self.config['deployment']['instance_id'],
                'principals': [{'id': 'operator', 'token_file': 'operator.token',
                                'permissions': ['pause', 'resume', 'reset', 'commands:read:any', 'audit:read'], 'nodes': ['*']}]}, 0o644)
        staging = self.directory / ('.tls-' + secrets.token_hex(8))
        try:
            provision_tls(self.config, staging)
            if (staging / 'tls').exists():
                if (self.directory / 'tls').exists():
                    shutil.rmtree(self.directory / 'tls')
                (staging / 'tls').rename(self.directory / 'tls')
        finally:
            if staging.exists():
                shutil.rmtree(staging)
        receipt = {'version': 1, 'digest': self.digest, 'project': self.project, 'executions': {}, 'containers': {}, 'networks': {}, 'infra_attempted': False}
        write(self.receipt, receipt)
        # Keep the receipt after any failure. Down verifies it and the manifest;
        # no failure path guesses ownership from a project name.
        for node in nodes:
            execution = self.register(node['id'])
            receipt['executions'][node['id']] = execution
            write(self.receipt, receipt)
            gate = self.directory / 'gates' / execution / node['id']
            gate.mkdir(parents=True)
            if os.geteuid() == 0:
                os.chown(gate, 65532, 65532)
        document = render(self.config, self.resolved, self.directory, self.args.port,
                          receipt['executions'], not self.args.no_capture, not self.args.no_history)
        write(self.compose_file, document)
        run([*self.compose, 'config', '--quiet'])
        if self.args.build:
            run([*self.compose, 'build'])
        # Docker binds the published endpoint atomically; a bind failure retains
        # the receipt and never stops a competing listener or another instance.
        try:
            run([*self.compose, 'create'])
        finally:
            # Containers remain stopped until their immutable IDs are durable.
            for item in self.inventory():
                labels = item['Config']['Labels']
                if labels.get('org.graphx.graph') != self.config['graph']['id'] or labels.get('org.graphx.instance') != self.config['deployment']['instance_id']:
                    raise ValueError('unexpected container created in instance project')
                receipt['containers'][item['Id']] = {k: v for k, v in labels.items() if k.startswith('org.graphx.')}
            for network in self.networks():
                receipt['networks'][network['Id']] = network['Name']
            write(self.receipt, receipt)
        run([*self.compose, 'start'])
        if self.managed:
            receipt['infra_attempted'] = True
            write(self.receipt, receipt)
            run([self.cli, 'infra', 'create', self.config_path])
        for node, execution in receipt['executions'].items():
            write(self.directory / 'gates' / execution / node / 'ready', '', 0o644)
        print(f'Instance {self.config["deployment"]["instance_id"]}: http://127.0.0.1:{self.args.port}')

    def restart(self, node):
        if self.managed:
            raise ValueError('OVS container replacement requires down/up through the owned infrastructure lifecycle')
        receipt = load(self.receipt)
        current = self.verify_receipt(receipt)
        if node == 'telemetry':
            collectors = [identity for identity, labels in receipt['containers'].items()
                          if labels.get('org.graphx.role') == 'telemetry' and identity in current]
            if len(collectors) != 1:
                raise ValueError('expected exactly one recorded collector')
            run(['docker', 'restart', *collectors])
            return
        if node not in receipt['executions']:
            raise ValueError('restart requires a known --node')
        manifest = self.manifest()
        registration = next(n for n in manifest['nodes'] if n['id'] == node)
        execution = receipt['executions'][node]
        if registration.get('execution_id') != execution:
            raise ValueError('registration changed; refusing restart')
        identities = [identity for identity, labels in receipt['containers'].items()
                      if labels.get('org.graphx.node') == node and identity in current]
        if len(identities) > 1:
            raise ValueError('multiple containers for one node; refusing restart')
        if identities:
            run(['docker', 'stop', '--time', '10', *identities])
            run(['docker', 'rm', *identities])
        for identity, labels in list(receipt['containers'].items()):
            if labels.get('org.graphx.node') == node:
                del receipt['containers'][identity]
        self.register(node, True, execution)
        del receipt['executions'][node]
        write(self.receipt, receipt)
        execution = self.register(node)
        receipt['executions'][node] = execution
        write(self.receipt, receipt)
        gate = self.directory / 'gates' / execution / node
        gate.mkdir(parents=True)
        if os.geteuid() == 0:
            os.chown(gate, 65532, 65532)
        document = load(self.compose_file)
        service = document['services'][node]
        service['environment']['GRAPHX_EXECUTION_ID'] = execution
        service['labels']['org.graphx.execution'] = execution
        next(m for m in service['volumes'] if m['target'] == '/gate')['source'] = str(gate)
        write(self.compose_file, document)
        created = []
        try:
            run([*self.compose, 'up', '--no-start', '--no-deps', '--force-recreate', node])
        finally:
            for item in self.inventory():
                if item['Id'] in receipt['containers']:
                    continue
                labels = item['Config']['Labels']
                if any(labels.get(key) != value for key, value in service['labels'].items()):
                    raise ValueError('unexpected replacement container; refusing adoption')
                created.append(item['Id'])
                receipt['containers'][item['Id']] = service['labels']
            write(self.receipt, receipt)
        if len(created) != 1:
            raise ValueError('expected exactly one replacement container')
        write(gate / 'ready', '', 0o644)
        run(['docker', 'start', *created])

    def down(self):
        receipt = load(self.receipt)
        current = self.verify_receipt(receipt)
        networks = {n['Id']: n for n in self.networks()}
        if set(networks) - set(receipt['networks']):
            raise ValueError('unrecorded/replaced management network; refusing mutation')
        for identity, network in networks.items():
            if network['Name'] != receipt['networks'][identity] or set(network.get('Containers', {})) - set(current):
                raise ValueError('management network identity or endpoints changed; refusing mutation')
        manifest = self.manifest()
        for node in manifest['nodes']:
            if node.get('execution_id') and receipt['executions'].get(node['id']) not in (None, node['execution_id']):
                raise ValueError('registration changed; refusing mutation')
        if self.managed and receipt.get('infra_attempted', True):
            run([self.cli, 'infra', 'destroy', self.config_path])
        # Use immutable IDs, never a broad compose down or project-name cleanup.
        if current:
            run(['docker', 'stop', '--time', '10', *current])
            run(['docker', 'rm', *current])
        manifest = load(self.directory / 'identity/identities.json')
        for node in manifest['nodes']:
            if node.get('execution_id'):
                expected = receipt['executions'].get(node['id'])
                if expected and expected != node['execution_id']:
                    raise ValueError('registration changed; refusing retirement')
                self.register(node['id'], True, node['execution_id'])
        for identity in networks:
            run(['docker', 'network', 'rm', identity])
        self.receipt.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['plan', 'up', 'status', 'down', 'token', 'restart'])
    parser.add_argument('config')
    parser.add_argument('--graphx', default=os.environ.get('GRAPHX_BIN', ROOT / 'build/dev/graphx'))
    parser.add_argument('--state-root', default=os.environ.get('GRAPHX_INSTANCE_ROOT',
        '/var/lib/graphx/runtime/instances' if platform.system() == 'Linux' else ROOT / '.graphx/instances'))
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--node', help='node to reactivate, or telemetry to restart the collector')
    parser.add_argument('--set', action='append', default=[], metavar='PATH=VALUE')
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--no-capture', action='store_true')
    parser.add_argument('--no-history', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('--port must be from 1 through 65535')
    if args.set:
        os.environ['GRAPHX_OVERRIDES'] = ';'.join(filter(None, [os.environ.get('GRAPHX_OVERRIDES', ''), *args.set]))
    runtime = Runtime(args)
    if args.action == 'plan':
        print(json.dumps(render(runtime.config, runtime.resolved, runtime.directory, args.port,
                                capture=not args.no_capture, history=not args.no_history), indent=2))
        return
    runtime.preflight()
    protected_directory(runtime.directory.parent)
    protected_directory(runtime.directory)
    with lock(runtime.directory / '.lock'):
        if args.action == 'up':
            runtime.up()
        elif args.action == 'restart':
            runtime.restart(args.node)
        elif args.action == 'down':
            runtime.down()
        elif args.action == 'status':
            receipt = load(runtime.receipt)
            current = runtime.verify_receipt(receipt)
            print(json.dumps({identity: item['State'] for identity, item in current.items()}, indent=2))
        else:
            print((runtime.directory / 'identity/operator.token').read_text())


if __name__ == '__main__':
    os.umask(0o077)
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError, StopIteration) as error:
        print(f'graphx instance: {error}', file=sys.stderr)
        sys.exit(1)
