#!/usr/bin/env python3
"""Trust fingerprints and actual build command forwarding, without Docker execution."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

root = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(root / 'scripts/release'))
sys.path.insert(0, str(root / 'scripts'))
from build_trust import build_trust
import image_release
import guest_release
import build_examples


class BuildObserved(Exception):
    pass


with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary).resolve()
    ca = directory / 'company ca.crt'
    installer = directory / 'install certs.sh'
    ca.write_text('test CA material')
    installer.write_text('#!/bin/bash\nexit 87\n')  # Must never execute on the host.
    environment = {'GRAPHX_CA_CERT': str(ca), 'GRAPHX_CERT_INSTALL_SCRIPT': str(installer)}
    for selection in ({}, {'GRAPHX_CA_CERT': str(ca)},
                      {'GRAPHX_CERT_INSTALL_SCRIPT': str(installer)}, environment):
        fingerprint, arguments = build_trust(selection)
        shell = subprocess.check_output(
            ['bash', '-c', 'source "$1"; printf "%s" "$GRAPHX_BUILD_TRUST_FINGERPRINT"',
             'trust-test', str(root / 'scripts/configure-build-trust.sh')],
            env={'PATH': os.environ['PATH'], **selection}, text=True)
        assert fingerprint == shell
        assert arguments.count('--secret') == len(selection)
    first, expected = build_trust(environment)
    installer.write_text('#!/bin/bash\nexit 88\n')
    assert build_trust(environment)[0] != first
    installer.write_text('#!/bin/bash\nexit 87\n')
    for invalid in ('relative.pem', str(directory / 'missing'), str(directory)):
        try:
            build_trust({'GRAPHX_CA_CERT': invalid})
        except ValueError:
            pass
        else:
            raise AssertionError('invalid trust path accepted')
    comma = directory / 'ca,env=OTHER'
    comma.write_text('test')
    try:
        build_trust({'GRAPHX_CA_CERT': str(comma)})
    except ValueError:
        pass
    else:
        raise AssertionError('BuildKit secret field injection accepted')

    # Trust changes invalidate the aggregate artifact cache, even outside the source tree.
    with patch.object(build_examples, 'source_files', return_value=[]), \
         patch.object(build_examples, 'run', return_value='a' * 40), \
         patch.dict(os.environ, environment, clear=True):
        before = build_examples.fingerprint(root, 'linux/arm64')
        ca.write_text('rotated CA material')
        assert before != build_examples.fingerprint(root, 'linux/arm64')
        ca.write_text('test CA material')
        assert before == build_examples.fingerprint(root, 'linux/arm64')

    def output(command, **options):
        if command[:3] == ['docker', 'context', 'show']:
            return 'orbstack\n'
        assert command[0] == 'git', command
        if 'rev-parse' in command:
            return 'a' * 40
        if 'show' in command:
            return '1700000000'
        return b''

    calls = []
    def run(command, **options):
        calls.append(command)
        assert command[0] == 'docker', command
        if command[1] in ('build', 'buildx'):
            for index in range(0, len(expected), 2):
                assert any(command[i:i+2] == expected[index:index+2]
                           for i in range(len(command)-1)), command
            if command[1] == 'buildx':
                assert 'GRAPHX_BUILD_VITA_RADIO=ON' in command
            raise BuildObserved()
        return SimpleNamespace(returncode=0)

    with patch.dict(os.environ, environment, clear=True), \
         patch.object(subprocess, 'run', side_effect=run), \
         patch.object(subprocess, 'check_output', side_effect=output):
        args = SimpleNamespace(source=root, output=directory/'images', with_vita=True,
                               qualification_hooks=False, allow_dirty=True,
                               platform='linux/arm64', no_cache=True)
        try:
            image_release.build(args)
        except BuildObserved:
            pass
        else:
            raise AssertionError('image builder never invoked Docker build')
        args = SimpleNamespace(source=root, output=directory/'guests',
                               catalog=root/'config/catalog', allow_dirty=True)
        with patch.object(guest_release, 'sources'), patch.object(guest_release, 'fetch_yaml'), \
             patch.object(guest_release, 'tree_digest', return_value='a' * 64):
            try:
                guest_release.build(args)
            except BuildObserved:
                pass
            else:
                raise AssertionError('guest builder never invoked Docker build')
print('Trust forwarding, shell fingerprint parity, cache invalidation and negative paths passed')
