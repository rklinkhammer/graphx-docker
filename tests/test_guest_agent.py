#!/usr/bin/env python3
"""Unprivileged framing and cross-node provisioning boundary tests."""
import hashlib
import importlib.util
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

root = Path(sys.argv.pop(1)).resolve()
spec = importlib.util.spec_from_file_location('guest_agent', root/'guests/common/agent.py')
agent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(agent)


class GuestAgent(unittest.TestCase):
    def test_frames(self):
        for payload in (b'', b'{}', b'{"node":"a","node":"b"}'):
            left, right = socket.socketpair()
            with left, right:
                left.sendall(struct.pack('!I', len(payload)) + payload)
                if payload == b'{}':
                    self.assertEqual(agent.receive(right.fileno(), time.monotonic()+1), {})
                else:
                    with self.assertRaises(ValueError):
                        agent.receive(right.fileno(), time.monotonic()+1)
        for size in (agent.MAX_FRAME+1, 0xffffffff):
            left, right = socket.socketpair()
            with left, right:
                left.sendall(struct.pack('!I', size))
                with self.assertRaises(ValueError):
                    agent.receive(right.fileno(), time.monotonic()+1)
        left, right = socket.socketpair()
        with left, right:
            left.sendall(struct.pack('!I', 12)+b'{}')
            left.shutdown(socket.SHUT_WR)
            with self.assertRaises(ValueError):
                agent.receive(right.fileno(), time.monotonic()+1)
        left, right = socket.socketpair()
        with left, right:
            with self.assertRaises(TimeoutError):
                agent.receive(right.fileno(), time.monotonic()+.02)
            with self.assertRaises(ValueError):
                agent.send(left.fileno(), 'x'*agent.MAX_FRAME, time.monotonic()+1)

    def test_identity_before_files_or_secrets(self):
        config = dict(version=1, node='renamed-guest', config='{}',
                      config_sha256=hashlib.sha256(b'{}').hexdigest(), token='a'*32, network={})
        secret = {k: config[k] for k in ('node','config_sha256','token')}
        secret['credentials'] = {}
        for field in ('node','config_sha256','token'):
            with tempfile.TemporaryDirectory() as raw:
                with self.assertRaises(ValueError):
                    agent.provision(config, {**secret, field:'other'}, Path(raw), 'guest.echo')
                self.assertEqual(list(Path(raw).iterdir()), [])
        with tempfile.TemporaryDirectory() as raw:
            with self.assertRaises(ValueError):
                agent.provision({**config, 'config':'tampered'}, secret, Path(raw), 'guest.echo')
            self.assertEqual(list(Path(raw).iterdir()), [])

    def test_private_credential_inventory(self):
        config = dict(version=1, node='radio-renamed', config='{}',
                      config_sha256=hashlib.sha256(b'{}').hexdigest(), token='b'*32, network={})
        identity = {k: config[k] for k in ('node','config_sha256','token')}
        node = dict(type='sdr.radio', execution={'kind':'qemu'}, credentials={'tls':'radio-tls'})
        members = {name: b'test-only'.hex() for name in ('ca.pem','cert.pem','key.pem')}
        members['generation.json'] = json.dumps({'version':1, 'generation':1, 'members':{
            name:hashlib.sha256(b'test-only').hexdigest() for name in members}}).encode().hex()
        with patch.object(agent.subprocess, 'check_output', return_value=json.dumps(node)), patch.object(agent.os, 'chown'):
            for credentials in ({}, {'wrong':members}, {'radio-tls':{'../escape':'00'}},
                                {'radio-tls':{**members, 'key.pem':'a'*131074}}):
                with tempfile.TemporaryDirectory() as raw:
                    with self.assertRaises(ValueError):
                        agent.provision(config, {**identity,'credentials':credentials}, Path(raw), 'sdr.radio')
            with tempfile.TemporaryDirectory() as raw:
                path = Path(raw)
                actual, actual_identity = agent.provision(config, {**identity,'credentials':{'radio-tls':members}}, path, 'sdr.radio')
                self.assertEqual((actual, actual_identity), (node, identity))
                self.assertEqual((path/'credentials').stat().st_mode & 0o777, 0o700)
                for name in members:
                    self.assertEqual((path/'credentials/radio-tls'/name).stat().st_mode & 0o777, 0o400)
            with tempfile.TemporaryDirectory() as raw:
                with self.assertRaises(ValueError):
                    agent.provision(config, {**identity,'credentials':{'radio-tls':members}}, Path(raw), 'guest.echo')


unittest.main()
