#!/usr/bin/env python3
"""Unprivileged loopback diagnostic listener, release barrier and malformed packets."""
import copy
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

build, root = map(lambda p: Path(p).resolve(), sys.argv[1:])
resolved = json.loads(subprocess.check_output([build / 'graphx', 'config', 'normalize',
                                              root / 'examples/static-route-policy/graphx.yml'], text=True))
node = copy.deepcopy(next(n for n in resolved['nodes'] if n['node_id'] == 'right'))
with tempfile.TemporaryDirectory(prefix='graphx-diagnostic-') as temporary:
    directory = Path(temporary).resolve()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as port_socket:
        port_socket.bind(('127.0.0.1', 0))
        port = port_socket.getsockname()[1]
    binding = node['bindings']['routed'][0]
    binding['settings']['bind'] = '127.0.0.1'
    binding['settings']['port'] = port
    binding['destination_address'] = '127.0.0.1'
    node['startup']['max_wait_ms'] = 3000
    config = directory / 'node.json'
    config.write_text(json.dumps(node))
    release = directory / 'release'
    log = directory / 'node.log'
    args = [build / 'graphx-diagnostic', '--node', 'right', '--config', config,
            '--release-file', release, '--release-token', 'a' * 32]
    with log.open('w') as output:
        process = subprocess.Popen(args, stdout=output, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 5
        while 'ready node=right' not in log.read_text():
            assert process.poll() is None, log.read_text()
            assert time.monotonic() < deadline, log.read_text()
            time.sleep(.02)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(b'GXR1\0\x05valid', ('127.0.0.1', port))
            time.sleep(.1)
            assert 'received' not in log.read_text(), 'received before release'
            release.write_text('a' * 32)
            for payload in [b'', b'GXR1\0\xff', b'GXR1\0\x01xextra', b'GXR1\0\x01\n', b'x' * 1024]:
                sender.sendto(payload, ('127.0.0.1', port))
            sender.sendto(b'GXR1\0\x04last', ('127.0.0.1', port))
        deadline = time.monotonic() + 3
        while 'token=last' not in log.read_text():
            assert process.poll() is None, log.read_text()
            assert time.monotonic() < deadline, log.read_text()
            time.sleep(.02)
        assert log.read_text().count('received node=') == 2, log.read_text()
    finally:
        process.terminate()
        process.wait(timeout=5)
    wrong = subprocess.run([*args[:2], 'left', *args[3:]], capture_output=True, text=True, timeout=5)
    assert wrong.returncode != 0 and 'E_NODE_ID' in wrong.stderr
print('diagnostic listener binding, release, malformed/oversized packet rejection and stop passed')
