#!/usr/bin/env python3
"""Guest packet application bindings and release contract without a guest boot."""
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

build, root = (Path(p).resolve() for p in sys.argv[1:])
node = json.loads((root/'tests/fixtures/compiled/qemu-tap/nodes/qemu-node.json').read_text())
node['node_id'] = 'renamed-echo'
node['startup']['max_wait_ms'] = 3000
ports = {}
for protocol, kind in [('tcp', socket.SOCK_STREAM), ('udp', socket.SOCK_DGRAM)]:
    with socket.socket(socket.AF_INET, kind) as available:
        available.bind(('127.0.0.1', 0))
        ports[protocol] = available.getsockname()[1]
for name, values in node['bindings'].items():
    binding = values[0]
    binding['source_address'] = binding['destination_address'] = '127.0.0.1'
    binding['settings']['bind'] = '127.0.0.1'
    binding['settings']['host' if name.startswith('tcp') else 'destination'] = '127.0.0.1'
    binding['settings']['port'] = ports[name[:3]]
    if name.startswith('udp'):
        binding['settings']['max_datagram_bytes'] = 64
with tempfile.TemporaryDirectory(prefix='graphx-packet-guest-') as raw:
    directory = Path(raw)
    config, log, release = (directory/name for name in ('node.json', 'log', 'release'))
    config.write_text(json.dumps(node))
    command = [build/'graphx-packet-guest', '--node', node['node_id'], '--config', config,
               '--release-file', release, '--release-token', 'c'*32]
    with log.open('w') as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic()+5
        while 'ready node=renamed-echo' not in log.read_text():
            assert process.poll() is None and time.monotonic() < deadline, log.read_text()
            time.sleep(.02)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.settimeout(.15)
            client.sendto(b'held', ('127.0.0.1', ports['udp']))
            try:
                client.recv(4096)
                raise AssertionError('application processed data before release')
            except socket.timeout:
                pass
            (directory/'release.pending').write_text('c'*32)
            (directory/'release.pending').rename(release)
            client.settimeout(2)
            assert client.recv(4096) == b'held'
            client.sendto(b'x'*65, ('127.0.0.1', ports['udp']))
            client.sendto(b'x'*1401, ('127.0.0.1', ports['udp']))
            client.sendto(b'bounded', ('127.0.0.1', ports['udp']))
            assert client.recv(4096) == b'bounded', 'oversized datagram was echoed'
        with socket.create_connection(('127.0.0.1', ports['tcp']), timeout=2) as client:
            client.sendall(b'tcp-renamed-bindings')
            assert client.recv(4096) == b'tcp-renamed-bindings'
    finally:
        process.terminate()
        process.wait(timeout=5)
    assert process.returncode == 0, log.read_text()
    node['type'] = 'sdr.radio'
    config.write_text(json.dumps(node))
    assert subprocess.run(command, capture_output=True, timeout=5).returncode != 0
print('Packet application renamed bindings, held/release, TCP/UDP echo, datagram bounds and stop passed; no guest boot inferred')
