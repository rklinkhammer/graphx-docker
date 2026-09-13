#!/usr/bin/env python3
"""Explicit, unprivileged acceptance of graphx run through generated Compose."""
import argparse
import json
from pathlib import Path
import re
import os
import signal
import socket
import urllib.request
import shutil
import subprocess
import time
import uuid

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('images',type=Path)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--case',default='examples/sample-pipeline/graphx.yml')
args=parser.parse_args()
source=Path(__file__).resolve().parents[1]
root=args.output.resolve();root.mkdir(parents=True,exist_ok=False)
cli=source/'build/dev/graphx'

def call(*argv,ok=True,timeout=120):
 r=subprocess.run(list(map(str,argv)),capture_output=True,text=True,timeout=timeout,cwd=source)
 if ok: assert r.returncode==0,(argv,r.stdout,r.stderr)
 else: assert r.returncode!=0,(argv,r.stdout,r.stderr)
 return r

def inventory():
 return {kind:call(*argv).stdout.splitlines() for kind,argv in {
  'containers':['docker','ps','-aq','--no-trunc'],
  'volumes':['docker','volume','ls','--format','{{.Name}}'],
  'networks':['docker','network','ls','--no-trunc','--format','{{.ID}}']}.items()}
before=inventory()
graph='p6-'+uuid.uuid4().hex[:12]
authored=root/'workspace'/args.case;authored.parent.mkdir(parents=True)
text=(source/args.case).read_text();text=re.sub(r'(?m)^  id: [a-z0-9_-]+$', '  id: '+graph,text,count=1)
with socket.socket() as listener:
 listener.bind(('127.0.0.1',0));console_port=listener.getsockname()[1]
assert '  console:' not in text, 'fixture must explicitly handle an existing console block'
if '\nplatform:\n' in text:
 text=text.replace('\nplatform:\n',f'\nplatform:\n  console: {{port: {console_port}}}\n',1)
else:text+=f'\nplatform:\n  console: {{port: {console_port}}}\n'
authored.write_text(text)
shutil.copytree(args.images/'catalog',root/'workspace/config/catalog')
compiled=root/'compiled';state=root/'state'
call(cli,'compile',authored,'--target',('orbstack' if os.uname().sysname=='Darwin' else 'native-linux'),'--catalog-root',root/'workspace/config/catalog','--source-root',root/'workspace',
 '--credential-root',root/'credentials','--output',compiled)
external=root/'external'; external.mkdir(mode=0o700)
manifest=json.loads((compiled/'credentials.json').read_text())
fixture=root/'fixture'
# Locally generated, disposable TLS fixture for declared external providers.
refs={ref:entry for ref,entry in manifest['entries'].items() if entry['provider']=='external'}
if any('ca.pem' in e['members'] or 'cert.pem' in e['members'] for e in refs.values()):
 script="""import {stageCredentials} from './apps/telemetry/credentials.mjs';
 stageCredentials({version:1,deny_inline_values:true,entries:{fixture:{provider:'lab-generated',identity:'platform',members:['ca.pem','cert.pem','key.pem'],tls_roles:['clientAuth']}},allowed_consumers:{fixture:['platform']}},process.argv[1]);"""
 call('node','--input-type=module','-e',script,fixture)
for ref,entry in refs.items():
 directory=external/ref;directory.mkdir(mode=0o700)
 for member in entry['members']:
  data=(ref+'x'*64).encode() if member in ('token','password') else (fixture/'fixture'/member).read_bytes()
  (directory/member).write_bytes(data);(directory/member).chmod(0o400)
options=['--external',external,'--output',compiled,'--state-root',state,'--images',args.images.resolve()]
try:
 invalid=list(options);invalid[invalid.index('--state-root')+1]=compiled/'runtime'
 assert 'E_EXECUTION_PATH' in call(cli,'run','up',*invalid,ok=False).stderr
 assert not (compiled/'runtime').exists()
 result=call(cli,'run','up',*options)
 (root/'up.log').write_text(result.stdout+result.stderr)
 assert 'ready graph=' in result.stdout
 time.sleep(3)
 resolved=json.loads((compiled/'resolved.json').read_text())
 platform_id='graphx-'+graph+'-platform'
 token=call('docker','exec',platform_id,'cat','/run/secrets/observer/token').stdout.strip()
 def request(path):
  port=resolved['platform']['console']['port']
  req=urllib.request.Request(f'http://127.0.0.1:{port}'+path,headers={'Authorization':'Bearer '+token})
  with urllib.request.urlopen(req,timeout=5) as r:return r.read()
 captures=json.loads(request('/api/captures'))
 (root/'captures.json').write_text(json.dumps(captures,indent=2)+'\n')
 expected_capture=[n for n in resolved['nodes'] if n['capture']['enabled'] and n['capture']['provider']=='application']
 if expected_capture:
  rows=captures.get('captures',captures.get('files',[]))
  assert len(rows)==len(expected_capture),captures
  for row in rows:
   data=request('/captures/'+row['name'])
   assert data[:4]==b'\x0a\x0d\x0d\x0a' and len(data)>100
   (root/row['name']).write_bytes(data)
 if args.case=='examples/sample-pipeline/graphx.yml':
  ownership=state/graph/'ownership.yml';original=ownership.read_text()
  # A changed immutable image identity must prevent any destructive operation.
  changed=re.sub(r'(secondary_id: [\"\']?)sha256:[a-f0-9]{64}',r'\1sha256:'+('0'*64),original,count=1)
  assert changed!=original
  ownership.write_text(changed)
  refused=call(cli,'run','down',*options,ok=False)
  assert 'E_DOCKER_IDENTITY' in refused.stderr
  assert json.loads(call('docker','container','inspect',platform_id).stdout)[0]['State']['Running']
  ownership.write_text(original)
 call(cli,'run','status',*options)
 call(cli,'run','down',*options)
 logs='\n'.join(p.read_text() for p in (state/graph/'logs').glob('*.log'))
 assert 'value=' in logs or 'processed sequence=' in logs or 'received' in logs.lower(),logs[-2000:]
 for node in resolved['nodes']:
  if node['execution']['kind']!='container':continue
  node_log=(state/graph/'logs'/('graphx-'+graph+'-'+node['node_id']+'.log')).read_text()
  assert 'ready node='+node['node_id'] in node_log,node_log
  if node['type'].startswith(('sample.','udp.','discovery.')):
   assert 'node='+node['node_id']+' seq=' in node_log,node_log
  elif node['type']=='sdr.processor':assert 'processed sequence=' in node_log,node_log
  elif node['type']=='sdr.result-sink':assert 'result ' in node_log,node_log
 if args.case=='examples/sample-pipeline/graphx.yml':
  with socket.socket() as busy:
   busy.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
   busy.bind(('127.0.0.1',resolved['platform']['console']['port']));busy.listen(1)
   assert 'E_LISTENER_BUSY' in call(cli,'run','up',*options,ok=False).stderr
  process=subprocess.Popen(list(map(str,[cli,'run','up',*options])),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  deadline=time.monotonic()+60
  while time.monotonic()<deadline and process.poll() is None:
   if 'kind: container' in ownership.read_text():
    process.send_signal(signal.SIGINT);break
   time.sleep(.02)
  stdout,stderr=process.communicate(timeout=90)
  (root/'interruption.log').write_text(stdout+stderr)
  assert process.returncode!=0,(stdout,stderr)
  call(cli,'run','down',*options)
  assert not (state/graph/'barriers/release').exists()
  call(cli,'run','up',*options)
  call(cli,'run','down',*options)
finally:
 call(cli,'run','down',*options)
 # Explicit final disposal of this test's retained history/capture volumes.
 state_text=(state/graph/'ownership.yml').read_text()
 owner=re.search(r'owner_token: [\"\']?([a-f0-9]{32})',state_text).group(1)
 for volume in call('docker','volume','ls','--filter','label=org.graphx.graph='+graph,'--format','{{.Name}}').stdout.splitlines():
  metadata=json.loads(call('docker','volume','inspect',volume).stdout)[0]
  assert metadata['Labels']['org.graphx.owner']==owner
  call('docker','volume','rm',volume)
 after=inventory()
 (root/'inventory.json').write_text(json.dumps({'before':before,'after':after},indent=2)+'\n')
 assert {k:sorted(v) for k,v in before.items()}=={k:sorted(v) for k,v in after.items()}
print('Compiled Compose startup, release, traffic, owned shutdown and preserved external inventory passed')
