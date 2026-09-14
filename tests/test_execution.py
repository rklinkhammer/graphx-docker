#!/usr/bin/env python3
"""Native execution lifecycle using exact-file local test release fixtures."""
import contextlib
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import urllib.error

build, source = (Path(p).resolve() for p in sys.argv[1:3])
example = sys.argv[3] if len(sys.argv)>3 else 'examples/shared-memory/graphx.yml'
cli = build/'graphx'

def run(*args, ok=True, timeout=45):
    result=subprocess.run([str(cli),*map(str,args)],capture_output=True,text=True,timeout=timeout)
    if ok: assert result.returncode==0,(args,result.stdout,result.stderr)
    else: assert result.returncode!=0,(args,result.stdout,result.stderr)
    return result

with contextlib.nullcontext(tempfile.mkdtemp(prefix='graphx-p6-')) as temporary:
    root=Path(temporary).resolve()
    release=root/'release'
    if os.environ.get('GRAPHX_TEST_RELEASE'):
        shutil.copytree(Path(os.environ['GRAPHX_TEST_RELEASE']).resolve(),release)
        bundle=release/'libexec/graphx-platform'
        cli=release/'bin/graphx'
    else:
        release=root/'release'; (release/'bin').mkdir(parents=True)
        bundle=release/'libexec/graphx-platform'
        app=bundle/'apps/telemetry'; app.mkdir(parents=True)
        for module in (source/'apps/telemetry').glob('*.mjs'):
            if not module.name.endswith('.test.mjs'): shutil.copy2(module,app/module.name)
        shutil.copytree(source/'apps/telemetry/node_modules',app/'node_modules',ignore=shutil.ignore_patterns('.bin'))
        shutil.copytree(source/'web/dist',bundle/'web/dist')
        (bundle/'config/schema').mkdir(parents=True)
        shutil.copy2(source/'config/schema/normalized-graph.schema.json',bundle/'config/schema/normalized-graph.schema.json')
        node_binary=Path(shutil.which('node')).resolve()
        shutil.copy2(node_binary,bundle/'node')
        # Homebrew's test Node is dynamically linked; published companions use the pinned official binary.
        if sys.platform=='darwin':
            for dependency in (node_binary.parent.parent/'lib').glob('libnode*.dylib'):
                shutil.copy2(dependency,bundle/dependency.name)
        assert subprocess.check_output([bundle/'node','--version'],text=True).startswith('v24.')
        launcher=release/'bin/graphx-platform';launcher.write_text('#!/bin/sh\nexit 64\n');launcher.chmod(0o755)
        for name in ('graphx','graphx-generator','graphx-transform','graphx-sink','graphx-udp-publisher','graphx-udp-subscriber'):
            shutil.copy2(build/name,release/'bin'/name)
        files={str(p.relative_to(release)):{'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'mode':p.stat().st_mode & 0o777}
               for p in release.rglob('*') if p.is_file()}
        (release/'release.json').write_text(json.dumps({'version':1,'graphx_version':'1.1.0','platform':('darwin' if sys.platform=='darwin' else 'linux')+'-'+('aarch64' if os.uname().machine in ('arm64','aarch64') else 'x86_64'),'files':files}))
    graph=json.loads(run('config','normalize',source/example,'--target',('native-macos' if sys.platform=='darwin' else 'native-linux')).stdout)
    authored=root/'input'; authored.mkdir()
    original=(source/example).read_text()
    original=original.replace('../../config/catalog/lock.json',str(source/'config/catalog/lock.json'))
    # Authored catalog paths must be relative to the graph directory.
    original=original.replace(str(source/'config/catalog/lock.json'),os.path.relpath(source/'config/catalog/lock.json',authored))
    rotation = example == 'examples/shared-memory/graphx.yml'
    if rotation:
        original=original.replace('interval_ms: 50','interval_ms: 50\n      max_messages: 10000')
        for type_name in ('sink','transform'):
            original=original.replace('    type: sample.'+type_name,'    parameters: {max_messages: 10000}\n    type: sample.'+type_name)
        node=graph['nodes'][0]
        rotate_ref=node['telemetry']['credential']
        original += '\ncredentials: ' + json.dumps({'scenario-next':{'identity':node['node_id'],'provider':'runtime-generated','members':['hmac']}})
        original += '\nscenario: ' + json.dumps({'actions':[{'id':'rotate-runtime','action':'credential-rotate','credential':rotate_ref,'next':'scenario-next','grace_seconds':1}]}) + '\n'
    # Lifecycle acceptance stops live applications explicitly instead of racing
    # the examples' short demonstration message limits.
    original=re.sub(r'max_messages: [0-9]+', 'max_messages: 10000', original)
    for type_name in ('publisher','subscriber'):
        original=original.replace('    type: udp.'+type_name, '    parameters: {max_messages: 10000}\n    type: udp.'+type_name)
    (authored/'graphx.yml').write_text(original)
    compiled=root/'compiled'; credentials=root/'credentials'; state=root/'state'
    run('compile',authored/'graphx.yml','--target',('native-macos' if sys.platform=='darwin' else 'native-linux'),'--catalog-root',source/'config/catalog',
        '--source-root',authored,'--credential-root',credentials,'--output',compiled)
    options=['--output',compiled,'--state-root',state,'--release',release,'--credentials',credentials]
    try:
        for key in ('--state-root','--credentials'):
            invalid=list(options);invalid[invalid.index(key)+1]=compiled/'runtime'
            assert 'E_EXECUTION_PATH' in run('run','up',*invalid,ok=False).stderr
            assert not (compiled/'runtime').exists()
        started=run('run','up',*options)
        assert 'ready graph=' in started.stdout
        time.sleep(1)
        status=run('run','status',*options)
        assert 'platform running' in status.stdout
        logs=state/graph['graph_id']/'logs'
        assert any(marker in '\n'.join(p.read_text() for p in logs.glob('*.log')) for marker in ('value=', 'received'))
        # The owned relay publishes observation-protected output through the platform.
        port = graph['platform']['console']['port']
        node_id = graph['nodes'][0]['node_id']
        log_url = f'http://127.0.0.1:{port}/api/nodes/{node_id}/logs'
        try:
            urllib.request.urlopen(log_url, timeout=2)
            raise AssertionError('unauthenticated logs accepted')
        except urllib.error.HTTPError as error:
            assert error.code == 401
        token = (credentials/'observer/token').read_text().strip()
        request = urllib.request.Request(log_url, headers={'Authorization': 'Bearer '+token})
        deadline = time.monotonic() + 10
        while True:
            try:
                with urllib.request.urlopen(request, timeout=2) as response:
                    console = json.load(response)
                if console['status'] in ('running', 'stopped') and console['hex']:
                    break
            except urllib.error.HTTPError as error:
                assert error.code == 503
            assert time.monotonic() < deadline, 'node log relay did not produce output'
            time.sleep(.2)
        assert console['graph'] == graph['graph_id'] and console['node'] == node_id
        assert len(bytes.fromhex(console['hex'])) <= 65536
        assert console['generation'].startswith('console-') and not console['stale']
        if example == 'examples/capture/graphx.yml':
            assert len(list((state/graph['graph_id']/'captures').rglob('*.pcapng'))) == 3
        run('run','up',*options,ok=False)
        if rotation:
            action_options=[*options,'--action','rotate-runtime']
            current=credentials/rotate_ref
            old=(current/'hmac').read_bytes()
            assert 'not-run' in run('scenario','status',*action_options).stdout
            run('scenario','run',*action_options)
            metadata=json.loads((current/'generation.json').read_text())
            assert metadata['generation']==2 and metadata['previous']['members']['hmac']==hashlib.sha256(old).hexdigest()
            assert (current/'hmac').read_bytes()!=old
            run('scenario','run',*action_options,ok=False)
            check="""import {pathToFileURL} from 'node:url';const [module,manifest,root,ref]=process.argv.slice(1);const {CredentialReader,readJson}=await import(pathToFileURL(module));const reader=new CredentialReader(root,readJson(manifest),'platform');if(!reader.read(ref).previous.hmac)throw Error('overlap absent');await new Promise(r=>setTimeout(r,1200));if(reader.read(ref).previous.hmac)throw Error('overlap did not expire');"""
            subprocess.run([bundle/'node','--input-type=module','-e',check,bundle/'apps/telemetry/credentials.mjs',compiled/'credentials.json',credentials,rotate_ref],check=True,timeout=10)
            assert 'platform running' in run('run','status',*options).stdout
        # Tampering one stored executable identity must preserve every running process.
        state_file=state/graph['graph_id']/'ownership.yml'
        original_state=state_file.read_text()
        state_file.write_text(original_state.replace(str(bundle/'node'),'/usr/bin/false'))
        refused=run('run','down',*options,ok=False)
        assert 'E_PROCESS_IDENTITY' in refused.stderr
        state_file.write_text(original_state)
        assert 'platform running' in run('run','status',*options).stdout
        run('run','down',*options)
        assert 'inactive' in run('run','status',*options).stdout
        if os.environ.get('GRAPHX_EXECUTION_EVIDENCE'):
            evidence=Path(os.environ['GRAPHX_EXECUTION_EVIDENCE']).resolve()
            evidence.mkdir(parents=True,exist_ok=False)
            shutil.copytree(logs,evidence/'logs')
            captures=state/graph['graph_id']/'captures'
            if captures.exists():shutil.copytree(captures,evidence/'captures')
            (evidence/'ownership-before-stop.yml').write_text(original_state)
            shutil.copy2(state_file,evidence/'ownership-after-stop.yml')
        assert not (state/graph['graph_id']/'barriers/release').exists()
        # A occupied console must fail before a new child is recorded.
        with socket.socket() as occupied:
            occupied.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
            occupied.bind(('127.0.0.1',8080)); occupied.listen(1)
            failed=run('run','up',*options,ok=False)
            assert 'E_LISTENER_BUSY' in failed.stderr
        # Restart uses the same owner/history, with a fresh barrier publication.
        run('run','up',*options)
        run('run','down',*options)
        # Interrupt while the platform is being registered/started, before application release.
        process=subprocess.Popen([str(cli),'run','up',*map(str,options)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        deadline=time.monotonic()+10
        while time.monotonic()<deadline and process.poll() is None:
            if 'status: creating' in state_file.read_text() and 'kind: native' in state_file.read_text():
                process.send_signal(signal.SIGINT); break
            time.sleep(.005)
        stdout,stderr=process.communicate(timeout=15)
        assert process.returncode!=0 and 'E_INTERRUPTED' in stderr,(stdout,stderr)
        # If interruption caught the platform's exec transition, a later exact-identity stop completes recovery.
        run('run','down',*options)
        assert not (state/graph['graph_id']/'barriers/release').exists()
        executable=release/'bin/graphx-generator'
        before=executable.read_bytes()
        executable.write_bytes(before+b'changed')
        assert 'E_RELEASE_IDENTITY' in run('run','up',*options,ok=False).stderr
        executable.write_bytes(before)
        extra=bundle/'injected.mjs';extra.write_text('export default 1')
        assert 'E_RELEASE_IDENTITY' in run('run','up',*options,ok=False).stderr
        extra.unlink()
        tampered=compiled/'platform.json'; original=tampered.read_bytes();tampered.write_bytes(original+b' ')
        assert 'E_COMPILE_IDENTITY' in run('run','up',*options,ok=False).stderr
        tampered.write_bytes(original)
    finally:
        run('run','down',*options,ok=False) if not (state/graph['graph_id']/'ownership.yml').exists() else run('run','down',*options)
    for directory in root.rglob('*'):
        if directory.is_dir(): directory.chmod(0o700)
    shutil.rmtree(root)
print('Native platform-first startup, release barrier, samples, duplicate refusal and owned stop passed')
