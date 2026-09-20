#!/usr/bin/env python3
"""Run the P1 harness against verified OCI radio bytes without network privileges."""
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

source = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(source / 'scripts/release'))
from image_release import verify

images = Path(sys.argv[1]).resolve()
manifest = verify(images)
assert 'vita' in manifest['images'] and not manifest['qualification_hooks']
inspection = manifest['images']['vita']['inspection']

with tempfile.TemporaryDirectory(prefix='graphx-packaged-radio-') as temporary:
    root = Path(temporary).resolve()
    root.chmod(0o755)
    wanted = {'usr/local/bin/graphx-vita-radio': 'graphx-vita-radio',
              'usr/local/lib/libSoapySDR.so.0.8.1': 'libSoapySDR.so.0.8'}
    found = set()
    with tarfile.open(images / 'vita.oci.tar') as archive:
        index = json.load(archive.extractfile('index.json'))
        image = json.load(archive.extractfile('blobs/sha256/' + index['manifests'][0]['digest'][7:]))
        for layer in image['layers']:
            with tarfile.open(fileobj=io.BytesIO(archive.extractfile('blobs/sha256/' + layer['digest'][7:]).read())) as files:
                for member in files:
                    name = member.name.removeprefix('./')
                    if name not in wanted: continue
                    assert member.isfile()
                    data = files.extractfile(member).read()
                    assert hashlib.sha256(data).hexdigest() == inspection['files'][name]['sha256']
                    target = root / wanted[name]
                    target.write_bytes(data); target.chmod(0o555); found.add(name)
    assert found == set(wanted)
    # The verified shared SDR role supplies only Python/OpenSSL for the harness.
    # The radio and SoapySDR library are exact bytes extracted from the VITA role.
    subprocess.run(['docker', 'image', 'load', '-i', str(images / 'sdr.oci.tar')], check=True, timeout=120)
    sdr = manifest['images']['sdr']['inspection']
    identity = None
    for candidate in (sdr['digest'], sdr['config_digest']):
        result = subprocess.run(['docker', 'image', 'inspect', candidate], capture_output=True, text=True, timeout=30)
        if result.returncode == 0:
            assert json.loads(result.stdout)[0]['Id'] == candidate
            identity = candidate; break
    assert identity
    # The P1 harness expects both executables in one directory.
    (root / 'graphx').symlink_to('/usr/local/bin/graphx')
    # A normal image must ignore the private admission-fault environment hook.
    command = ['docker', 'create', '--network', 'none', '--read-only', '--cap-drop', 'ALL',
               '--security-opt', 'no-new-privileges:true', '--user', '65532:65532',
               '--pids-limit', '128', '--memory', '512m', '--tmpfs', '/tmp:rw,nosuid,size=256m',
               '--env', 'GRAPHX_TEST_CATALOG=/catalog', '--env', 'LD_LIBRARY_PATH=/packaged',
               '--env', 'PYTHONDONTWRITEBYTECODE=1',
               '--env', 'GRAPHX_TEST_APPLICATION_FAULT=invalid']
    for local, remote in ((root, '/packaged'), (images / 'catalog', '/catalog'),
                          (source / 'tests', '/source/tests'), (source / 'config', '/source/config')):
        command += ['--mount', f'type=bind,src={local},dst={remote},readonly']
    command += ['--entrypoint', 'python3', identity, '/source/tests/test_vita_radio.py', '/packaged']
    container = subprocess.check_output(command, text=True, timeout=30).strip()
    assert len(container) == 64 and all(c in '0123456789abcdef' for c in container)
    try:
        subprocess.run(['docker', 'start', '--attach', container], check=True, timeout=180)
        state = json.loads(subprocess.check_output(['docker', 'inspect', container], text=True))[0]
        assert state['Id'] == container and state['State']['ExitCode'] == 0
    finally:
        subprocess.run(['docker', 'rm', '--force', container], check=True, timeout=30)
print('Packaged VITA radio bytes passed the four-radio P1 harness in an unprivileged Linux container')
