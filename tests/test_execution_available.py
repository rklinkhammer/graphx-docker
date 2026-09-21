#!/usr/bin/env python3
"""Exercise the common runner with exact-file local releases and injected app faults.
No Docker, namespaces, OVS, or privileged operations.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time

build = Path(sys.argv[1]).resolve()
source = Path(__file__).resolve().parents[1]
target = 'native-macos' if sys.platform == 'darwin' else 'native-linux'

with tempfile.TemporaryDirectory(prefix='graphx-p4-lifecycle-') as directory:
    root = Path(directory).resolve()
    release = root/'release'; (release/'bin').mkdir(parents=True)
    bundle = release/'libexec/graphx-platform'; app = bundle/'apps/telemetry'; app.mkdir(parents=True)
    for module in (source/'apps/telemetry').glob('*.mjs'):
        if not module.name.endswith('.test.mjs'): shutil.copy2(module, app/module.name)
    shutil.copytree(source/'apps/telemetry/node_modules', app/'node_modules', ignore=shutil.ignore_patterns('.bin'))
    shutil.copytree(source/'web/dist', bundle/'web/dist')
    (bundle/'config/schema').mkdir(parents=True)
    shutil.copy2(source/'config/schema/normalized-graph.schema.json', bundle/'config/schema/normalized-graph.schema.json')
    node = Path(shutil.which('node')).resolve(); shutil.copy2(node, bundle/'node')
    if sys.platform == 'darwin':
        for dependency in (node.parent.parent/'lib').glob('libnode*.dylib'): shutil.copy2(dependency, bundle/dependency.name)
    assert subprocess.check_output([bundle/'node', '--version'], text=True).startswith('v26.')
    shutil.copy2(build/'graphx', release/'bin/graphx')
    launcher = release/'bin/graphx-platform'; launcher.write_text('#!/bin/sh\nexit 64\n'); launcher.chmod(0o755)
    for name in ('generator', 'transform', 'sink'):
        shutil.copy2(build/'graphx-lifecycle-application', release/f'bin/graphx-{name}')
    files = {str(p.relative_to(release)): {'sha256': hashlib.sha256(p.read_bytes()).hexdigest(), 'mode': p.stat().st_mode & 0o777} for p in release.rglob('*') if p.is_file()}
    (release/'release.json').write_text(json.dumps({'version':1, 'graphx_version':'1.1.0', 'platform': ('darwin' if sys.platform=='darwin' else 'linux')+'-'+('aarch64' if os.uname().machine in ('arm64','aarch64') else 'x86_64'), 'files':files}))
    def run(*args, ok=True):
        result = subprocess.run([str(build/'graphx'), *map(str,args)], capture_output=True, text=True, timeout=45)
        assert (result.returncode == 0) == ok, (args,result.stdout,result.stderr)
        return result
    inputs=root/'inputs'; inputs.mkdir()
    original = (source/'examples/shared-memory/graphx.yml').read_text().replace('../../config/catalog/lock.json', os.path.relpath(source/'config/catalog/lock.json', inputs))
    path = inputs/'graphx.yml'; compiled = root/'compiled'; state = root/'state'; credentials = root/'credentials'
    opts = ['--output',compiled,'--state-root',state,'--release',release,'--credentials',credentials]
    def compile_policy(policy):
        if compiled.exists(): shutil.rmtree(compiled)
        path.write_text(original + '\nlifecycle: '+json.dumps(policy)+'\n')
        run('compile',path,'--target',target,'--catalog-root',source/'config/catalog','--source-root',inputs,'--credential-root',credentials,'--output',compiled)
        return json.loads((compiled/'resolved.json').read_text())
    graph = compile_policy({'startup':'available','readiness_ms':500})
    gid = graph['graph_id']; nodes = [n['node_id'] for n in graph['nodes']]
    ledger = state/gid/'ownership.yml'; barrier = state/gid/'barriers/release'; logs = state/gid/'logs'
    evidence = Path(os.environ['GRAPHX_EXECUTION_EVIDENCE']).resolve() if os.environ.get('GRAPHX_EXECUTION_EVIDENCE') else None
    if evidence: evidence.mkdir(parents=True,exist_ok=False)
    def record(label):
        if evidence: (evidence/(label+'.yml')).write_bytes(ledger.read_bytes())
    def clear():
        for p in root.glob('*.fault'): p.unlink()
    try:
        for name in nodes:
            for fault in ('unavailable','signal','timeout'):
                clear(); (root/(name+'.fault')).write_text(fault)
                before=time.monotonic(); result=run('run','up',*opts)
                assert time.monotonic()-before < 20
                assert 'degraded graph=' in result.stdout and barrier.exists(),result.stdout
                status=run('run','status',*opts).stdout
                assert f'application={name} admission=' in status and 'status=degraded' in status,status
                record(name+'-'+fault+'-before-stop')
                text=ledger.read_text(); assert text.count('kind: native') >= len(nodes)+1,text
                assert 'released node=' not in (logs/(name+'.log')).read_text()
                for other in set(nodes)-{name}: assert f'application={other} admission=ready status=ready' in status
                # Exact inventory unchanged; failed application is not respawned.
                time.sleep(.1); assert ledger.read_text()==text
                run('run','down',*opts); assert not barrier.exists()
                assert 'kind: native' not in ledger.read_text()
                record(name+'-'+fault+'-after-stop')
        clear()
        for name in nodes: (root/(name+'.fault')).write_text('unavailable')
        assert 'unavailable graph=' in run('run','up',*opts).stdout
        run('run','down',*opts)
        for fault in ('invalid','unknown'):
            clear(); (root/(nodes[0]+'.fault')).write_text(fault)
            assert 'E_APPLICATION_STARTUP' in run('run','up',*opts,ok=False).stderr
            assert not barrier.exists() and 'kind: native' not in ledger.read_text()
        # Runtime loss retains survivors and exact ledger, no implicit relaunch.
        for name in nodes:
            clear(); run('run','up',*opts); before=ledger.read_text()
            match=re.search(r'name: '+name+r'\n\s+stable_id: ["\']?(\d+)',before); assert match,before
            os.kill(int(match[1]),signal.SIGKILL); time.sleep(.15)
            status=run('run','status',*opts).stdout
            assert f'application={name} admission=ready status=unavailable' in status,status
            assert 'status=degraded' in status and before==ledger.read_text()
            run('run','down',*opts)
        # Identity corruption cannot turn into degraded success or delete survivors.
        clear(); run('run','up',*opts); before=ledger.read_text()
        ledger.write_text(before.replace(str(bundle/'node'),'/usr/bin/false'))
        assert 'E_PROCESS_IDENTITY' in run('run','status',*opts,ok=False).stderr
        assert 'E_PROCESS_IDENTITY' in run('run','down',*opts,ok=False).stderr
        ledger.write_text(before); run('run','down',*opts)
        clear(); run('run','up',*opts)
        token=barrier.read_text(); barrier.chmod(0o600); barrier.write_text('0'*32)
        assert 'E_EXECUTION_IDENTITY' in run('run','down',*opts,ok=False).stderr
        barrier.write_text(token);barrier.chmod(0o444);run('run','down',*opts)
        # Interrupt an available-policy startup while an application waits readiness.
        clear();(root/(nodes[-1]+'.fault')).write_text('timeout')
        process=subprocess.Popen([str(build/'graphx'),'run','up',*map(str,opts)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        deadline=time.monotonic()+10
        while time.monotonic()<deadline and process.poll() is None:
            if ledger.exists() and ledger.read_text().count('kind: native')>=len(nodes)+1:
                process.send_signal(signal.SIGINT);break
            time.sleep(.005)
        stdout,stderr=process.communicate(timeout=15)
        assert process.returncode!=0 and 'E_INTERRUPTED' in stderr,(stdout,stderr)
        run('run','down',*opts)
        assert not barrier.exists() and 'kind: native' not in ledger.read_text()
        # Same unavailable application still rolls back in default transactional mode.
        shutil.rmtree(state)
        compile_policy({'startup':'transactional'})
        (root/(nodes[0]+'.fault')).write_text('unavailable')
        assert 'E_READINESS_EXIT' in run('run','up',*opts,ok=False).stderr
        assert not barrier.exists() and 'kind: native' not in ledger.read_text()
        for policy in ({'startup':'anything'}, {'startup':'available','readiness_ms':0}, {'startup':'available','readiness_ms':30001}):
            path.write_text(original+'\nlifecycle: '+json.dumps(policy)+'\n')
            run('config','normalize',path,'--target',target,ok=False)
        print('P4 native: each node before readiness/after release, timeouts, all unavailable, unsafe startup rejection, identity refusal, cleanup and transactional defaults passed')
    finally:
        if ledger.exists(): run('run','down',*opts)
