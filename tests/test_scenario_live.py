#!/usr/bin/env python3
"""Explicit finite scenario acceptance; no VM provisioning or context changes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error
import uuid
import yaml

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--case',choices=['S11','S12','S14','V04'],required=True)
parser.add_argument('--target',choices=['lima','native-linux','orbstack'],required=True)
parser.add_argument('--allow-privileged',action='store_true')
parser.add_argument('--release',type=Path)
parser.add_argument('--images',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args()
privileged=a.case!='V04'
if privileged and (not a.allow_privileged or sys.platform!='linux' or os.geteuid()!=0):
    parser.error('requires explicitly authorized local Linux root with --allow-privileged')
source=Path(__file__).resolve().parents[1]
cli=a.release/'bin/graphx' if a.release else source/'build/dev/graphx'
root=a.output.resolve()
if privileged and not root.is_relative_to(Path('/var/lib/graphx')):parser.error('privileged evidence must stay in guest storage')
root.mkdir(parents=True,exist_ok=False)
def call(*args,ok=True,timeout=180):
    p=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=timeout)
    if ok:assert p.returncode==0,(args,p.stdout,p.stderr)
    return p
def inventory():
    commands={'containers':['docker','ps','-aq','--no-trunc'],'networks':['docker','network','ls','--no-trunc','--format','{{.ID}}'],'volumes':['docker','volume','ls','--format','{{.Name}}']}
    if privileged:commands.update(bridges=['ovs-vsctl','list-br'],namespaces=['ip','netns','list'],links=['ip','-o','link','show'],processes=['ps','-eo','pid,comm'])
    return {key:call(*args).stdout.splitlines() for key,args in commands.items()}
call('docker','info');call('docker','compose','version')
if sys.platform=='darwin':assert call('docker','context','show').stdout.strip()=='orbstack'
before=inventory();(root/'before.json').write_text(json.dumps(before,indent=2)+'\n')
case={'S11':'network-observability','S12':'static-route-policy','S14':'sdr-node/external','V04':'variants/credential-rotation'}[a.case]
original=source/'examples'/case/'graphx.yml';source_hash=hashlib.sha256(original.read_bytes()).hexdigest()
graph=yaml.safe_load(original.read_text());graph['graph']['id']='p9-'+uuid.uuid4().hex[:12]
with socket.socket() as listener:listener.bind(('127.0.0.1',0));port=listener.getsockname()[1]
graph.setdefault('platform',{})['console']={'port':port}
for node in graph['nodes'].values():
    if node['type'].startswith('sample.'):
        node.setdefault('parameters',{})['max_messages']=10000
for action in graph['scenario']['actions']:
    if action['action']=='fault':action['duration_seconds']=2
    if action['action']=='credential-rotate':action['grace_seconds']=3
workspace=root/'workspace';workspace.mkdir();graph['catalog']=os.path.relpath(a.images.resolve()/'catalog/lock.json',workspace)
config=workspace/'graphx.yml';config.write_text(json.dumps(graph))
compiled=root/'compiled';state=root/'state';credentials=root/'credentials';external=root/'external';external.mkdir(mode=0o700)
tokens={}
if a.case=='V04':
    for ref in ('operator','operator-next'):
        directory=external/ref;directory.mkdir(mode=0o700);tokens[ref]=secrets.token_hex(32)
        path=directory/'token';path.write_text(tokens[ref]);path.chmod(0o400)
compile_args=[cli,'compile',config,'--target',a.target,'--catalog-root',a.images.resolve()/'catalog','--source-root',workspace,'--credential-root',credentials,'--output',compiled]
if a.case=='S14':compile_args+=['--laboratory','laboratory-radio']
call(*compile_args)
resolved=json.loads((compiled/'resolved.json').read_text())
options=['--output',compiled,'--state-root',state,'--images',a.images,'--external',external]
if a.release:options+=['--release',a.release]
if privileged:options+=['--allow-privileged']
ledger=state/graph['graph']['id']/'ownership.yml'
checks=[];complete=False
result={'result':'incomplete','case':a.case,'target':a.target,'host_architecture':os.uname().machine,'checks':checks}
def scenario(operation,action,ok=True):
    p=call(cli,'scenario',operation,*options,'--action',action,ok=ok)
    with (root/'actions.log').open('a') as log:log.write(f'{operation} {action}\n'+p.stdout+p.stderr)
    return p
def container(node):return next(p['stable_id'] for p in yaml.safe_load(ledger.read_text())['processes'] if p['kind']=='container' and p['name']=='graphx-'+graph['graph']['id']+'-'+node)
try:
    p=call(cli,'run','up',*options,ok=False);(root/'up.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stderr
    (root/'ready-ownership.yml').write_text(ledger.read_text())
    assert not yaml.safe_load(ledger.read_text()).get('scenario_actions')
    checks.append('baseline executes no scenario actions')
    if a.case=='S11':
        interface=next(x['interface'] for x in resolved['network']['attachments'] if x['kind']=='qemu_tap')
        assert 'netem' not in call('tc','qdisc','show','dev',interface).stdout
        scenario('run','source-delay');assert 'netem' in call('tc','qdisc','show','dev',interface).stdout
        deadline=time.monotonic()+5
        while 'netem' in call('tc','qdisc','show','dev',interface).stdout:assert time.monotonic()<deadline;time.sleep(.1)
        scenario('clear','source-delay');checks.append('owned netem applied, expired automatically and cleared')
    elif a.case=='S12':
        scenario('run','verify-flows');scenario('run','apply-deferred')
        scenario('run','verify-flows');scenario('run','clear-deferred');scenario('run','verify-flows')
        original_ledger=ledger.read_text();value=yaml.safe_load(original_ledger)
        next(x for x in value['scenario_actions'] if x['id']=='apply-deferred')['status']='pending'
        ledger.write_text(yaml.safe_dump(value))
        try:assert scenario('run','apply-deferred',ok=False).returncode!=0
        finally:ledger.write_text(original_ledger)
        checks.append('route absent/apply/clear packet checks; pending intent refuses replay')
    elif a.case=='S14':
        assert all(n['execution']['kind']!='external' for n in resolved['nodes'])
        assert not any(e['provider']=='external' for e in json.loads((compiled/'credentials.json').read_text())['entries'].values())
        deadline=time.monotonic()+20
        while 'result ' not in call('docker','logs',container('sink')).stdout:assert time.monotonic()<deadline;time.sleep(.2)
        script="""import sys,json
sys.path.insert(0,'/opt/graphx-sdr/common')
import processor
from node_settings import load,binding
from credential_files import configure_tls
n=load('processor','/run/graphx/node.json','sdr.processor');configure_tls(n);processor.control_binding=binding(n,'control');print(json.dumps(processor.control('status')))
"""
        status=json.loads(call('docker','exec',container('processor'),'python3','-c',script).stdout)
        assert status['accepted'] is True
        (root/'sdr-status.json').write_text(json.dumps(status,indent=2)+'\n')
        checks.append('explicit isolated simulator, test-only trust, samples/results and authenticated control')
    else:
        def status(token):
            request=urllib.request.Request(f'http://127.0.0.1:{port}/api/control/commands',headers={'Authorization':'Bearer '+token})
            try:
                with urllib.request.urlopen(request,timeout=3) as response:return response.status
            except urllib.error.HTTPError as error:return error.code
        assert status(tokens['operator'])==200
        scenario('run','rollover')
        assert status(tokens['operator'])==200 and status(tokens['operator-next'])==200
        time.sleep(3.2)
        assert status(tokens['operator'])==401 and status(tokens['operator-next'])==200
        scenario('run','runtime-rollover');assert scenario('run','runtime-rollover',ok=False).returncode!=0
        checks.append('operator overlap and expiry; per-node runtime generation rotation; duplicate refusal')
    assert hashlib.sha256(original.read_bytes()).hexdigest()==source_hash
    complete=True
finally:
    down=call(cli,'run','down',*options,ok=False);(root/'down.log').write_text(down.stdout+down.stderr)
    after=inventory();(root/'after.json').write_text(json.dumps(after,indent=2)+'\n')
    result['cleanup_returncode']=down.returncode
    (root/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    assert down.returncode==0,down.stderr
    for key in ('containers','networks','bridges','namespaces'):
        if key in before:assert sorted(before[key])==sorted(after[key]),key
    if privileged:
        names=lambda rows:{r.split(':',2)[1].split('@')[0].strip() for r in rows}
        assert names(before['links'])==names(after['links'])
        processes=lambda rows:{r.strip() for r in rows if any(v in r for v in ('graphx','dumpcap','qemu-system'))}
        assert processes(before['processes'])==processes(after['processes'])
    assert set(before['volumes'])<=set(after['volumes'])
    result['result']='pass' if complete else 'incomplete';checks.append('owned cleanup and existing-resource preservation; authored physical trust unchanged')
    (root/'results.json').write_text(json.dumps(result,indent=2)+'\n')
print('Scenario acceptance passed:',root)
