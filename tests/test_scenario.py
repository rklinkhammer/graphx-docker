#!/usr/bin/env python3
"""Scenario reference, operation, baseline and compilation identity boundaries."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import yaml
cli,source=(Path(p).resolve() for p in sys.argv[1:3])
with tempfile.TemporaryDirectory() as temporary:
    root=Path(temporary).resolve()
    def call(*args,ok=True):
        result=subprocess.run([str(cli),*map(str,args)],capture_output=True,text=True,timeout=20)
        assert (result.returncode==0)==ok,(args,result.stdout,result.stderr)
        return result
    original=yaml.safe_load((source/'examples/static-route-policy/graphx.yml').read_text())
    original['catalog']=str(source/'config/catalog/lock.json')
    # Catalog paths are relative declarations, even outside the temporary root.
    import os
    original['catalog']=os.path.relpath(source/'config/catalog/lock.json',root)
    (root/'input').mkdir()
    original['catalog']=os.path.relpath(source/'config/catalog/lock.json',root/'input')
    graph=root/'input/graphx.yml'
    def write(value):graph.write_text(json.dumps(value))
    write(original)
    call('validate',graph,'--target','lima')
    for edit in ({'router':'missing'}, {'destination':'10.99.0.0/24'}, {'next':'not-a-route-field'}):
        value=json.loads(json.dumps(original));value['scenario']['actions'][0].update(edit);write(value)
        assert 'E_SCENARIO' in call('validate',graph,'--target','lima',ok=False).stderr
    value=json.loads(json.dumps(original));value['scenario']['actions'].append(value['scenario']['actions'][0]);write(value)
    assert 'duplicate action' in call('validate',graph,'--target','lima',ok=False).stderr
    value=json.loads(json.dumps(original));del value['scenario']['actions'][0]['destination'];write(value)
    assert 'missing operation field' in call('validate',graph,'--target','lima',ok=False).stderr
    write(original);compiled=root/'compiled';state=root/'state'
    call('compile',graph,'--target','lima','--source-root',source,'--credential-root',root/'credentials','--output',compiled)
    options=['--output',compiled,'--state-root',state]
    plan=json.loads(call('scenario','plan',*options).stdout)
    assert plan['implicit_start'] is False and not state.exists()
    call('scenario','run',*options,'--action','undeclared',ok=False)
    call('scenario','run',*options,'--action','apply-deferred',ok=False)
    assert not state.exists()
    path=compiled/'scenario-plan.json';path.write_text(path.read_text()+' ')
    assert 'E_COMPILE_IDENTITY' in call('scenario','plan',*options,ok=False).stderr
    lab=root/'laboratory'
    call('compile',source/'examples/sdr-node/external/graphx.yml','--target','lima','--laboratory','laboratory-radio','--source-root',source,'--credential-root',root/'lab-credentials','--output',lab)
    resolved=json.loads((lab/'resolved.json').read_text())
    radio=next(n for n in resolved['nodes'] if n['node_id']=='radio')
    assert radio['type']=='sdr.simulator' and radio['execution']['kind']=='container'
    assert all(a['kind']!='external' for a in resolved['network']['attachments'])
    attachment=next(a for a in resolved['network']['attachments'] if a['id']=='sdr-node-data')
    assert attachment['switch']=='br-sdr'
    manifest=json.loads((lab/'credentials.json').read_text())
    assert not any(e['provider']=='external' for e in manifest['entries'].values())
    assert json.loads((lab/'laboratory-selection.json').read_text())['physical_device'] is False
    call('compile',source/'examples/sdr-node/external/graphx.yml','--target','lima','--laboratory','missing','--source-root',source,'--credential-root',root/'lab-credentials','--output',root/'invalid',ok=False)
    assert not (root/'invalid').exists()
    value=yaml.safe_load((source/'examples/sdr-node/external/graphx.yml').read_text())
    value['catalog']=original['catalog']
    value['network']['switches'].append({'id':'another-switch','kind':'openvswitch','datapath':'system','ports':[]})
    next(a for a in value['network']['attachments'] if a['id']=='sink-data')['switch']='another-switch'
    write(value)
    result=call('compile',graph,'--target','lima','--laboratory','laboratory-radio','--source-root',source,'--credential-root',root/'lab-credentials','--output',root/'ambiguous',ok=False)
    assert 'unambiguous owned switch' in result.stderr,result.stderr
    assert not (root/'ambiguous').exists()
print('Scenario references, explicit selection, baseline and tamper checks passed')
