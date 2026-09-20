#!/usr/bin/env python3
"""P3 authoritative jumbo/mirror contracts; never mutates a network."""
import contextlib
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

build=Path(sys.argv[1]).resolve()
source=Path(__file__).resolve().parents[1]
retained = Path(sys.argv[3]).resolve() if len(sys.argv) == 4 and sys.argv[2] == '--retain' else None
if retained:
    retained.mkdir(parents=True, exist_ok=False)
fixture = contextlib.nullcontext(str(retained)) if retained else tempfile.TemporaryDirectory(prefix='graphx-p3-contract-')
with fixture as directory:
    root=Path(directory).resolve(); inputs=root/'inputs';inputs.mkdir();catalog=inputs/'catalog';shutil.copytree(source/'config/catalog',catalog)
    for path in (source/'config/catalog/types').glob('*.json'):
        value=json.loads(path.read_text())
        (catalog/'types'/path.name).write_text(json.dumps(value))
    schemas=json.loads((catalog/'wire-schemas.json').read_text());schemas.update(json.loads((source/'config/catalog/wire-schemas.json').read_text()));(catalog/'wire-schemas.json').write_text(json.dumps(schemas))
    lock=json.loads((catalog/'lock.json').read_text());paths={f['path'] for f in lock['files']}|{f'types/{p.name}' for p in (source/'config/catalog/types').glob('*.json')};lock['files']=[{'path':p,'sha256':hashlib.sha256((catalog/p).read_bytes()).hexdigest()} for p in sorted(paths)];(catalog/'lock.json').write_text(json.dumps(lock))
    graph={'version':3,'catalog':'catalog/lock.json','graph':{'id':'p3-contract'},'nodes':{'recorder':{'type':'vita.recorder','execution':{'kind':'container'}}},'connections':{},'credentials':{n:{'identity':n,'members':['ca.pem','cert.pem','key.pem'],'provider':'lab-generated'} for n in ['radio','controller']},'network':{'networks':[{'id':'data','profile':'ethernet','realization':'ovs','subnets':['10.79.0.0/24']}],'switches':[{'id':'switch','kind':'openvswitch','datapath':'system','ports':[{'id':'capture'}],'mirror':{'id':'mirror','output_port':'capture','select_all':True}}],'attachments':[{'id':'mirror','kind':'mirror','owner':'recorder','switch':'switch','port':'capture','delivery':'container','mtu':9000}],'edge_paths':{},'captures':[{'id':'diagnostic','attachment':'mirror','snaplen':9022,'max_file_bytes':65536,'max_files':2,'rotation_seconds':10,'retention_seconds':60}]}}
    graph['platform']={'capture':{'enabled':True,'provider':'application'}}
    graph['nodes']['processor']={'type':'vita.processor','execution':{'kind':'container'},'credentials':{f'control{i}':'controller' for i in range(1,5)}}
    graph['nodes']['detector']={'type':'vita.detector','execution':{'kind':'container'}}
    for i in range(1,5):graph['nodes'][f'radio{i}']={'type':'vita.radio','execution':{'kind':'container'},'credentials':{'control':'radio'},'parameters':{'radio_index':i}}
    for i,name in enumerate(graph['nodes'],10):
        if name!='recorder':graph['network']['attachments'].append({'id':name+'-data','kind':'container_veth','owner':name,'network':'data','address':f'10.79.0.{i}/24','switch':'switch','mtu':9000})
    def edge(name,a,b,tcp=False):
        start=a.split('.')[0];end=b.split('.')[0]
        graph['connections'][name]={'from':a,'to':b,'transport':'tcp' if tcp else 'udp','attachments':{'from':start+'-data','to':end+'-data'},'settings':{'port':18000+len(graph['connections']),'framing':'none'}}
        if tcp:graph['connections'][name]['security']={'profile':'mtls','server_name':'radio'}
        else:graph['connections'][name]['settings']['max_datagram_bytes']=8836 if name=='spectra' else 4128
        graph['network']['edge_paths'][name]=[start,'data','switch',end]
    for i in range(1,5):edge(f'control{i}',f'processor.control{i}',f'radio{i}.control',True);edge(f'data{i}',f'radio{i}.samples',f'processor.data{i}')
    edge('spectra','processor.spectra','detector.spectra')
    path=inputs/'graphx.yml'
    def normalize(value):
        path.write_text(json.dumps(value))
        return subprocess.run([str(build/'graphx'),'config','normalize',str(path),'--catalog-root',str(catalog)],capture_output=True,text=True)
    result=normalize(graph);assert result.returncode==0,result.stderr
    normalized=json.loads(result.stdout)
    mirror=next(a for a in normalized['network']['attachments'] if a['id']=='mirror')
    recorder=next(n for n in normalized['nodes'] if n['node_id']=='recorder')
    assert recorder['recorder']=={'interface':mirror['peer'],'mtu':9000}
    assert not recorder['capture']['enabled']
    assert recorder['bindings']=={} and mirror['delivery']=='container'
    compiled=root/'compiled'
    result=subprocess.run([str(build/'graphx'),'compile',str(path),'--output',str(compiled),'--source-root',str(inputs),'--credential-root',str(root/'credentials'),'--catalog-root',str(catalog),'--target','native-linux'],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    compose=json.loads((compiled/'compose.yaml').read_text())
    service=compose['services']['recorder']
    assert all(':/captures' not in v for v in service['volumes'])
    assert service['cap_drop']==['ALL'] and service['cap_add']==['NET_RAW']
    assert service['read_only'] and service['restart']=='no' and service['security_opt']==['no-new-privileges:true']
    assert 'network_mode' not in service and len(service['networks'])==1
    assert all('docker.sock' not in v and 'openvswitch' not in v for v in service['volumes'])
    assert all('cap_add' not in v for k,v in compose['services'].items() if k!='recorder')
    for mtu in (0,575,9001,-1,'9000'):
        bad=copy.deepcopy(graph);bad['network']['attachments'][0]['mtu']=mtu;assert normalize(bad).returncode!=0
    for index in (0,1,2):
        bad=copy.deepcopy(graph);bad['network']['attachments'][index]['mtu']=1500;assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['captures'][0]['snaplen']=9021;assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['attachments'][0]['owner']='processor';assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['attachments'][0]['address']='10.79.0.1/24';assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['switches'][0]['mirror']['select_all']=False;assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['switches'][0]['mirror']['output_port']='missing';assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['captures']=[];assert normalize(bad).returncode==0
    bad=copy.deepcopy(graph);bad['network']['attachments'][1]['port']='capture';assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['attachments'][0]['delivery']='invalid';assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['attachments'][1]['delivery']='container';assert normalize(bad).returncode!=0
    bad=copy.deepcopy(graph);bad['network']['attachments'][1]['mtu']=8864;assert normalize(bad).returncode!=0 # declared processor path is 9000
    other=copy.deepcopy(graph);other['graph']['id']='p3-other';other_result=normalize(other);assert other_result.returncode==0,other_result.stderr
    other_mirror=next(a for a in json.loads(other_result.stdout)['network']['attachments'] if a['id']=='mirror')
    assert other_mirror['interface']!=mirror['interface'] and other_mirror['peer']!=mirror['peer']
    assert normalize(graph).returncode==0
    available=copy.deepcopy(graph);available['lifecycle']={'startup':'available','readiness_ms':5000}
    value=normalize(available);assert value.returncode==0,value.stderr
    assert json.loads(value.stdout)['lifecycle']==available['lifecycle']
    output=root/'available'
    result=subprocess.run([str(build/'graphx'),'compile',str(path),'--output',str(output),'--source-root',str(inputs),'--credential-root',str(root/'credentials'),'--catalog-root',str(catalog),'--target','native-linux'],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    assert json.loads((output/'execution-plan.json').read_text())['lifecycle']==available['lifecycle']
    assert all(s['restart']=='no' for s in json.loads((output/'compose.yaml').read_text())['services'].values())
    assert normalize(graph).returncode==0
    if retained: print(f'Reviewable authored fixture and compiled contracts: {retained}; no runtime resources created')
    print('P3 jumbo bounds, passive owner, source isolation, optional capture and compiler permission contracts passed')
