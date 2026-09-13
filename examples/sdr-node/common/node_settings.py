"""Resolved node bindings validated by the release's authoritative C++ reader."""
from __future__ import annotations

import argparse
import json
import os
import stat
import subprocess
import time


def arguments(expected_type: str):
    parser = argparse.ArgumentParser()
    parser.add_argument('--node', required=True)
    parser.add_argument('--config', required=True)
    parser.add_argument('--release-file', required=True)
    parser.add_argument('--release-token', required=True)
    args = parser.parse_args()
    if not 32 <= len(args.release_token.encode()) <= 128:
        parser.error('release token must contain 32–128 bytes')
    return args, load(args.node, args.config, expected_type)


def load(node_id, config, expected_type):
    result = subprocess.run([os.environ.get('GRAPHX_CLI', 'graphx'), 'node-settings',
                             '--node', node_id, '--config', config],
                            check=True, capture_output=True, text=True, timeout=10)
    node = json.loads(result.stdout)
    if node['type'] != expected_type:
        raise ValueError('E_NODE_TYPE: wrong executable for resolved node')
    return node


def binding(node, port):
    peers = node['bindings'][port]
    if len(peers) != 1:
        raise ValueError('E_PORT_CARDINALITY: exactly one peer required')
    return peers[0]


def release(args, node, stop):
    print(f"ready node={node['node_id']}", flush=True)
    deadline = time.monotonic() + node['startup']['max_wait_ms'] / 1000
    while not stop.is_set():
        try:
            descriptor = os.open(args.release_file, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            descriptor = None
        if descriptor is not None:
            with os.fdopen(descriptor, 'rb') as stream:
                metadata = os.fstat(stream.fileno())
                if not stat.S_ISREG(metadata.st_mode) or metadata.st_size > 128:
                    raise ValueError('E_RELEASE_IDENTITY: bounded regular release file required')
                if stream.read(129) != args.release_token.encode():
                    raise ValueError('E_RELEASE_IDENTITY: wrong invocation token')
            return True
        if time.monotonic() >= deadline:
            raise TimeoutError('E_READINESS_TIMEOUT: release deadline expired')
        stop.wait(0.02)
    return False
