#!/usr/bin/env python3
"""Verify and execute an actual native platform companion; no network downloads."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request

root, build, bundle_directory = (Path(argument).resolve() for argument in sys.argv[1:])
sys.path.insert(0,str(root/'scripts/release'))
from platform_bundle import verify
from release_common import ReleaseError
archive, = bundle_directory.glob('*.tar.gz')
manifest, = bundle_directory.glob('*.manifest.json')
identity = verify(archive,manifest)
with tempfile.TemporaryDirectory(prefix='graphx-native-platform-') as temporary:
    temporary = Path(temporary).resolve()
    with tarfile.open(archive) as stream: stream.extractall(temporary,filter='data')
    release = temporary/f"graphx-{identity['graphx_version']}-{identity['platform']}"
    shutil.copy2(build/'graphx',release/'bin/graphx')
    launcher = release/'bin/graphx-platform'
    assert 'stage' in subprocess.check_output([launcher,'--help'],text=True)
    output = temporary/'compiled'
    subprocess.run([build/'graphx','compile',root/'examples/shared-memory/graphx.yml',
        '--output',output,'--source-root',root,'--credential-root',temporary/'credentials'],check=True)
    platform_file = output/'platform.json'; normalized_file = output/'resolved.json'
    platform = json.loads(platform_file.read_text()); normalized = json.loads(normalized_file.read_text())
    platform['console']['port'] = 18986; platform['control']['allowed_origins'] = ['http://127.0.0.1:18986']; platform['telemetry']['port'] = 19986
    normalized['platform'] = platform
    platform_file.write_text(json.dumps(platform)); normalized_file.write_text(json.dumps(normalized))
    subprocess.run([launcher,'stage','--manifest',output/'credentials.json','--destination',temporary/'credentials'],check=True)
    env = {**os.environ,'GX_CREDENTIALS':str(temporary/'credentials'),'GX_STATE':str(temporary/'state'), 'GX_OWNER':'platform-native-release-owner-1234567890'}
    log = temporary/'platform.log'
    with log.open('w') as stream:
        child = subprocess.Popen([launcher,'--config',platform_file],env=env,stdout=stream,stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic()+10
        while time.monotonic()<deadline:
            assert child.poll() is None,log.read_text()
            try:
                with urllib.request.urlopen('http://127.0.0.1:18986/api/ready',timeout=1) as response:
                    if response.status==200: break
            except OSError: pass
            time.sleep(.1)
        else: raise AssertionError(log.read_text())
        with urllib.request.urlopen('http://127.0.0.1:18986/',timeout=2) as response:
            assert '<html' in response.read().decode().lower()
    finally:
        child.terminate(); child.wait(timeout=6)
        print(log.read_text())
    assert child.returncode==0
    bad = temporary/'bad.manifest.json'
    changed = json.loads(manifest.read_text()); changed['sha256']='0'*64; bad.write_text(json.dumps(changed))
    try: verify(archive,bad)
    except ReleaseError: pass
    else: raise AssertionError('wrong archive checksum accepted')
    try: verify(archive,manifest,{'commit':'0'*40})
    except ReleaseError: pass
    else: raise AssertionError('wrong release identity accepted')
    node_name=next(name for name in changed['files'] if name.endswith('/node'))
    changed=json.loads(manifest.read_text()); changed['files'][node_name]['mode']=0o644;bad.write_text(json.dumps(changed))
    try: verify(archive,bad)
    except ReleaseError: pass
    else: raise AssertionError('wrong executable mode accepted')
    subprocess.run(['chmod','-R','u+w',str(temporary)],check=True)
print('Pinned native Node/web bundle starts; credential staging, checksum/identity/mode rejection passed')
