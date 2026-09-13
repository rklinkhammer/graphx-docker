#!/usr/bin/env python3
"""Fixed boot agent: three bounded virtio channels, private tmpfs, no management NIC."""
import hashlib
import json
import os
from pathlib import Path
import re
import select
import signal
import struct
import subprocess
import sys
import time

MAX_FRAME = 1024 * 1024


def transfer(fd, size, deadline, data=None):
    result = bytearray()
    offset = 0
    while offset < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('guest channel deadline expired')
        readable, writable, _ = select.select([] if data is not None else [fd],
                                               [fd] if data is not None else [], [], min(.1, remaining))
        if not readable and not writable:
            continue
        try:
            part = os.write(fd, data[offset:]) if data is not None else os.read(fd, size - offset)
        except BlockingIOError:
            continue
        if not part:
            raise ValueError('truncated guest channel')
        offset += part if data is not None else len(part)
        if data is None:
            result.extend(part)
    return bytes(result)


def receive(fd, deadline):
    size, = struct.unpack('!I', transfer(fd, 4, deadline))
    if not 0 < size <= MAX_FRAME:
        raise ValueError('invalid guest frame length')
    return json.loads(transfer(fd, size, deadline), object_pairs_hook=unique)


def unique(pairs):
    value = {}
    for key, child in pairs:
        if key in value:
            raise ValueError('duplicate guest field')
        value[key] = child
    return value


def send(fd, value, deadline):
    data = json.dumps(value, separators=(',', ':')).encode()
    if len(data) > MAX_FRAME:
        raise ValueError('oversized guest frame')
    frame = struct.pack('!I', len(data)) + data
    transfer(fd, len(frame), deadline, frame)


def channel(name, deadline):
    while time.monotonic() < deadline:
        for item in Path('/sys/class/virtio-ports').glob('*/name'):
            if item.read_text().strip() == 'org.graphx.' + name:
                return os.open('/dev/' + item.parent.name, os.O_RDWR | os.O_NONBLOCK | os.O_NOFOLLOW)
        time.sleep(.05)
    raise TimeoutError('virtio channel missing')


def provision(config, secret, root, expected_type):
    if set(config) != {'version', 'node', 'config_sha256', 'token', 'config', 'network'} or config['version'] != 1:
        raise ValueError('invalid configuration message')
    identity = {key: config[key] for key in ('node', 'config_sha256', 'token')}
    if not re.fullmatch('[a-z][a-z0-9_-]{0,63}', identity['node']) or not re.fullmatch('[a-f0-9]{32}', identity['token']):
        raise ValueError('invalid invocation identity')
    raw = config['config'].encode()
    if hashlib.sha256(raw).hexdigest() != config['config_sha256']:
        raise ValueError('configuration digest mismatch')
    if set(secret) != {*identity, 'credentials'} or any(secret[key] != value for key, value in identity.items()):
        raise ValueError('credential channel identity mismatch')
    path = root / 'node.json'
    path.write_bytes(raw)
    path.chmod(0o444)
    # Authoritative C++ node reader owns schema, catalog type and binding validation.
    node = json.loads(subprocess.check_output(['graphx', 'node-settings', '--node', identity['node'],
                                             '--config', str(path)], timeout=10))
    if node['execution']['kind'] != 'qemu' or node['type'] != expected_type or expected_type not in ('guest.echo', 'sdr.radio'):
        raise ValueError('wrong guest application')
    refs = set(node['credentials'].values())
    if set(secret['credentials']) != refs:
        raise ValueError('unexpected guest credential reference')
    credentials = root / 'credentials'
    credentials.mkdir(mode=0o700)
    os.chown(credentials, 65532, 65532)
    for ref, members in secret['credentials'].items():
        if not re.fullmatch('[a-z][a-z0-9_-]{0,63}', ref) or set(members) != {'ca.pem', 'cert.pem', 'key.pem', 'generation.json'}:
            raise ValueError('invalid guest credential inventory')
        if not isinstance(members['generation.json'], str) or len(members['generation.json']) > 16384:
            raise ValueError('oversized guest generation')
        metadata = json.loads(bytes.fromhex(members['generation.json']), object_pairs_hook=unique)
        if metadata.get('version') != 1 or type(metadata.get('generation')) is not int or metadata['generation'] < 1:
            raise ValueError('invalid guest credential generation')
        for name in ('ca.pem', 'cert.pem', 'key.pem'):
            if not isinstance(members[name], str) or len(members[name]) > 131072:
                raise ValueError('oversized guest credential')
            if metadata.get('members', {}).get(name) != hashlib.sha256(bytes.fromhex(members[name])).hexdigest():
                raise ValueError('incomplete guest credential generation')
        directory = credentials / ref
        directory.mkdir(mode=0o700)
        os.chown(directory, 65532, 65532)
        for name, encoded in members.items():
            if not isinstance(encoded, str) or len(encoded) > 131072:
                raise ValueError('oversized guest credential')
            data = bytes.fromhex(encoded)
            target = directory / name
            with target.open('xb') as stream:
                stream.write(data)
            target.chmod(0o400)
            os.chown(target, 65532, 65532)
    return node, identity


def main():
    import platform
    print('guest boot architecture=' + platform.machine(), flush=True)
    deadline = time.monotonic() + 120
    descriptors = {name: channel(name, deadline) for name in ('config', 'credentials', 'ready')}
    root = Path('/run/graphx')
    root.mkdir(mode=0o755, exist_ok=True)
    subprocess.run(['mount', '-t', 'tmpfs', '-o', 'size=8m,nosuid,nodev,noexec,mode=0755', 'graphx', str(root)], check=True)
    config = receive(descriptors['config'], deadline)
    secret = receive(descriptors['credentials'], deadline)
    node, identity = provision(config, secret, root, Path('/usr/lib/graphx/application').read_text().strip())
    secret.clear()
    # The guest has one data NIC. Match its compiled MAC, never invent a management path.
    network = config['network']
    links = [p.parent.name for p in Path('/sys/class/net').glob('*/address')
             if p.read_text().strip().lower() == network['mac'].lower()]
    if len(links) != 1:
        raise ValueError('compiled guest NIC not found')
    device = links[0]
    import ipaddress
    ipaddress.ip_interface(network['address'])
    subprocess.run(['ip', 'link', 'set', 'lo', 'up'], check=True)
    subprocess.run(['ip', 'address', 'add', network['address'], 'dev', device], check=True)
    subprocess.run(['ip', 'link', 'set', device, 'up'], check=True)
    application = ['/usr/bin/graphx-packet-guest'] if node['type'] == 'guest.echo' else [sys.executable, '/usr/lib/graphx/sdr/radio.py']
    application += ['--node', identity['node'], '--config', str(root/'node.json'),
                    '--release-file', str(root/'release'), '--release-token', identity['token']]
    def drop_identity():
        os.setgroups([])
        os.setgid(65532)
        os.setuid(65532)
    child = subprocess.Popen(application, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             env={'PATH':'/usr/bin:/bin:/usr/sbin:/sbin', 'GRAPHX_CREDENTIALS':str(root/'credentials')},
                             preexec_fn=drop_identity)
    try:
        pending = b''
        while time.monotonic() < deadline:
            if child.poll() is not None:
                raise ValueError('guest application exited before readiness')
            if not select.select([child.stdout], [], [], .05)[0]:
                continue
            pending += os.read(child.stdout.fileno(), 4096)
            if len(pending) > 16384:
                raise ValueError('guest readiness log exceeded bound')
            if ('ready node=' + identity['node']).encode() in pending.splitlines():
                break
        else:
            raise TimeoutError('guest listeners did not become ready')
        send(descriptors['ready'], {**identity, 'state':'listeners-bound'}, deadline)
        if receive(descriptors['ready'], deadline) != {**identity, 'state':'release'}:
            raise ValueError('guest release identity mismatch')
        with (root/'release.pending').open('x') as stream:
            stream.write(identity['token'])
            stream.flush()
            os.fsync(stream.fileno())
        (root/'release.pending').chmod(0o444)
        (root/'release.pending').rename(root/'release')
        send(descriptors['ready'], {**identity, 'state':'released'}, deadline)
        # Drain bounded application diagnostics; never copy channel credentials to serial.
        records = 0
        while True:
            line = child.stdout.readline(2049)
            if not line:
                break
            if len(line) > 2048:
                raise ValueError('guest diagnostic record exceeded bound')
            if records < 4096:
                print(line.decode(errors='replace').rstrip(), flush=True)
            records += 1
        if child.wait() != 0:
            raise ValueError('guest application failed')
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        # Exception values may contain private provisioning bytes; emit only the category.
        print('guest startup failed: ' + type(error).__name__, flush=True)
        raise SystemExit(1)
