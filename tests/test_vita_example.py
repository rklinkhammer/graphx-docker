#!/usr/bin/env python3
"""Maintained seven-application example, credentials and OVS binding boundaries."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, source = map(lambda p: Path(p).resolve(), sys.argv[1:])
example = source / 'examples/four-radio-vita/graphx.yml'

def run(*args):
    return subprocess.run([str(cli), *map(str,args)], capture_output=True, text=True, timeout=30)

result = run('config','normalize',example,'--target','lima')
assert result.returncode == 0, result.stderr
v=json.loads(result.stdout)
assert len(v['nodes']) == 7 and v['lifecycle'] == {'startup':'available','readiness_ms':5000}
assert v['platform']['console']=={'bind':'127.0.0.1','port':8080}
assert not v['platform']['control']['enabled']
assert v['platform']['control']['allowed_origins']==['http://127.0.0.1:8080','http://127.0.0.1:18080']
linux=json.loads(run('config','normalize',example,'--target','native-linux').stdout)
assert linux['platform']['control']['allowed_origins']==['http://127.0.0.1:8080']
action=json.loads(run('config','authored',example).stdout)['scenario']['actions'][0]
assert action['id']=='iq-loss-jitter' and action['action']=='fault'
assert action['attachment']=='processor-data' and action['duration_seconds']==20
assert action['loss_percent']==2 and action['jitter_ms']==6
nodes={n['node_id']:n for n in v['nodes']}
assert nodes['recorder']['bindings']=={} and not nodes['recorder']['capture']['enabled']
ports=[]
for i in range(1,5):
    radio=nodes[f'radio{i}']; assert radio['parameters']['radio_index']==i
    control=radio['bindings']['control'][0]; data=radio['bindings']['samples'][0]
    assert control['security']=={'profile':'mtls','server_name':'radio'}
    assert control['source_address']=='10.79.0.14' and control['destination_address']==f'10.79.0.{9+i}'
    assert control['settings']['bind']==control['destination_address']
    assert data['source_address']==f'10.79.0.{9+i}' and data['destination_address']=='10.79.0.14'
    assert data['settings']['max_datagram_bytes']==4128 and data['encoding']=='raw'
    ports += [control['settings']['port'], data['settings']['port']]
assert len(set(ports))==8
assert all(a['mtu']==9000 for a in v['network']['attachments'])
assert len(v['network']['edge_paths'])==9
assert nodes['detector']['bindings']['spectra'][0]['source_address']=='10.79.0.14'
with tempfile.TemporaryDirectory(prefix='graphx-vita-example-') as directory:
    root=Path(directory).resolve(); output=root/'compiled'
    result=run('compile',example,'--target','lima','--output',output,'--source-root',source,'--credential-root',root/'credentials')
    assert result.returncode==0,result.stderr
    compose=json.loads((output/'compose.yaml').read_text())
    assert compose['services']['platform']['ports']==['127.0.0.1:8080:8080']
    for name,service in compose['services'].items():
        assert service['restart']=='no' and not service.get('privileged')
        if name=='platform': continue
        assert list(service['networks'])==['mg-'+name]
        assert not service.get('ports') and not service.get('network_mode')
    assert compose['services']['recorder']['cap_add']==['NET_RAW']
    authored=json.loads(run('config','authored',example).stdout)
    authored['catalog']=os.path.relpath(source/'config/catalog/lock.json',root)
    def rejected(value):
        path=root/'bad.yml';path.write_text(json.dumps(value))
        assert run('config','normalize',path,'--target','lima').returncode!=0
    bad=copy.deepcopy(authored);bad['connections']['data1'].pop('attachments');rejected(bad)
    bad=copy.deepcopy(authored);bad['network']['attachments'][0]['mtu']=1500;rejected(bad)
    bad=copy.deepcopy(authored);bad['nodes']['radio1']['credentials']['control']='missing';rejected(bad)
assert run('config','normalize',example,'--target','orbstack').returncode!=0
print('Four-radio example: OVS-only bindings, distinct ports, credential references and compiler boundaries passed')
