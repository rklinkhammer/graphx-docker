#!/usr/bin/env python3
"""Explicit unprivileged engine acceptance of the compiled default platform.

Requires a verified image_release directory. Never uses graph run/infra or privileged flags.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile
import subprocess
import sys
import time
import uuid
import urllib.request
import urllib.error


def run(*args, **kwargs):
    return subprocess.check_output(list(args), text=True, timeout=kwargs.pop('timeout', 60), **kwargs).strip()


def request(port, path, token=None):
    headers = {'Authorization':'Bearer ' + token} if token else {}
    try:
        with urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}' + path, headers=headers), timeout=2) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve(); args.release = args.release.resolve()
    root = Path(__file__).resolve().parents[1]
    if sys.platform == 'darwin' and run('docker','context','show') != 'orbstack':
        raise RuntimeError('macOS acceptance requires OrbStack')
    engine = run('docker','info','--format','{{.OperatingSystem}} {{.Architecture}}')
    run('docker','compose','version')
    before = set(run('docker','ps','-aq','--no-trunc').splitlines())
    args.output.mkdir(parents=True, exist_ok=False)
    manifest = json.loads((args.release / 'images.json').read_text())
    image = manifest['images']['telemetry']['inspection']['config_digest']
    present = subprocess.run(['docker','image','inspect',image], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    run('docker','image','load','--input',str(args.release / 'telemetry.oci.tar'),timeout=180)
    evidence = {'engine':engine, 'before':sorted(before), 'cases':[]}
    try:
        for case, source in [('S01','examples/sample-pipeline/graphx.yml'),
                ('S11','examples/network-observability/graphx.yml'),
                *[(f'V{i+1:02}',f'examples/variants/{name}/graphx.yml') for i,name in enumerate(
                    ['history','observability','control','credential-rotation','secure-otlp','otlp-mtls'])],
                ('SDR','examples/sdr-node/simulated/graphx.yml')]:
            owner = uuid.uuid4().hex
            output = args.output / case
            with tempfile.TemporaryDirectory(prefix='graphx-p5-compile-') as temporary:
                temporary = Path(temporary).resolve()
                authored = temporary/'workspace'/source
                authored.parent.mkdir(parents=True)
                shutil.copyfile(root/source, authored)
                catalog = temporary/'workspace/config/catalog'
                shutil.copytree(args.release/'catalog', catalog)
                run(str(root/'build/dev/graphx'),'compile',str(authored),'--output',str(temporary/'compiled'),
                    '--source-root',str(root),'--credential-root',str(temporary/'credentials'), '--target','lima',
                    '--catalog-root',str(catalog))
                shutil.copytree(temporary/'compiled', output)
            containers, volumes, extension_images = [], [], []
            network = None
            def create(command, mounts, extra=(), selected_image=image, alias=None):
                cid = run('docker','create','--label','org.graphx.owner='+owner,'--read-only','--user','65532:65532',
                    '--cap-drop','ALL','--security-opt','no-new-privileges:true','--pids-limit','128','--memory','512m',
                    '--tmpfs','/tmp:rw,nosuid,nodev,size=16m', '--network',network or 'none',
                    *(['--network-alias',alias] if alias else []), *mounts, *extra, selected_image, *command)
                assert len(cid) == 64; containers.append(cid); return cid
            def execute(command, mounts):
                cid = create(command,mounts)
                result = run('docker','start','--attach',cid,timeout=60)
                assert run('docker','inspect','--format','{{.State.ExitCode}}',cid) == '0', result
                return result
            try:
                network = run('docker','network','create','--label','org.graphx.owner='+owner,'graphx-p5-'+owner)
                for kind in ['credentials','state']:
                    volume = 'graphx-p5-' + owner + '-' + kind
                    run('docker','volume','create','--label','org.graphx.owner='+owner,volume); volumes.append(volume)
                graph_mount = ['--mount',f'type=bind,src={output.resolve()},dst=/run/graphx,readonly']
                staging = graph_mount + ['--mount',f'type=volume,src={volumes[0]},dst=/var/lib/graphx']
                # Explicit local test fixtures for external references. No real device trust is read or changed.
                prepare = '''import {stageCredentials,readJson} from '/app/credentials.mjs';
import {mkdirSync,writeFileSync,readFileSync} from 'node:fs';
const manifest=readJson('/run/graphx/credentials.json'), root='/var/lib/graphx';
const refs=Object.entries(manifest.entries).filter(([,e])=>e.provider==='external');
mkdirSync(root+'/external',{mode:0o700});
if(refs.some(([,e])=>e.members.includes('cert.pem')||e.members.includes('ca.pem'))) {
 stageCredentials({version:1,deny_inline_values:true,entries:{fixture:{provider:'lab-generated',identity:'platform',members:['ca.pem','cert.pem','key.pem'],tls_roles:['clientAuth']}},allowed_consumers:{fixture:['platform']}},root+'/fixture');
}
for(const [ref,entry] of refs) {mkdirSync(root+'/external/'+ref,{mode:0o700});
 for(const member of entry.members) writeFileSync(root+'/external/'+ref+'/'+member,
 ['token','password'].includes(member)?ref+'x'.repeat(64):readFileSync(root+'/fixture/fixture/'+member),{mode:0o400});}
stageCredentials(manifest,root+'/credentials',{externalRoot:root+'/external'});
console.log(readFileSync(root+'/credentials/observer/token','utf8'));'''
                token = execute(['node','--input-type=module','-e',prepare], staging).splitlines()[-1]
                credential_manifest = json.loads((output/'credentials.json').read_text())
                mounts = graph_mount + ['--mount',f'type=volume,src={volumes[1]},dst=/var/lib/graphx']
                for ref, consumers in credential_manifest['allowed_consumers'].items():
                    if 'platform' in consumers:
                        mounts += ['--mount',f'type=volume,src={volumes[0]},dst=/run/secrets/{ref},volume-subpath=credentials/{ref},readonly']
                cid = create(['graphx-platform','--config','/run/graphx/platform.json'], mounts,
                    ['-e','GX_CREDENTIALS=/run/secrets','-e','GX_STATE=/var/lib/graphx','-e','GX_OWNER='+owner,
                     '-p','127.0.0.1::8080'], alias='platform')
                run('docker','start',cid)
                port = int(run('docker','port',cid,'8080/tcp').rsplit(':',1)[1])
                deadline = time.monotonic()+20
                while time.monotonic() < deadline:
                    try:
                        if request(port,'/api/ready')[0] == 200: break
                    except (OSError, urllib.error.URLError): pass
                    time.sleep(.1)
                else: raise RuntimeError('platform readiness failed: '+run('docker','logs',cid))
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    status, body = request(port, '/api/history/status', token)
                    if status == 200 and json.loads(body).get('status') == 'ready': break
                    time.sleep(.1)
                else: raise RuntimeError('history readiness failed: ' + body)
                assert request(port,'/api/topology')[0] == 401
                status, body = request(port,'/api/topology',token)
                assert status == 200
                snapshot = json.loads(body)
                if case == 'S11': assert not snapshot['control']['nodeBoundIdentity']
                assert token not in body
                if case == 'SDR':
                    sdr_image = manifest['images']['sdr']['inspection']['config_digest']
                    if subprocess.run(['docker','image','inspect',sdr_image],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:
                        run('docker','image','load','--input',str(args.release/'sdr.oci.tar'),timeout=180)
                        extension_images.append(sdr_image)
                    radio_mounts = graph_mount.copy()
                    for ref, consumers in credential_manifest['allowed_consumers'].items():
                        if 'radio' in consumers:
                            radio_mounts += ['--mount',f'type=volume,src={volumes[0]},dst=/run/secrets/{ref},volume-subpath=credentials/{ref},readonly']
                    probe = "import sys; sys.path.insert(0,'/opt/graphx-sdr/common'); from node_settings import load; from protocol import configure_telemetry; from pathlib import Path; node=load('radio','/run/graphx/nodes/radio.json','sdr.simulator'); configure_telemetry(node); assert not Path('/run/secrets/lab-processor/key.pem').exists(); import json, subprocess; stale=json.loads(Path('/run/graphx/nodes/radio.json').read_text()); stale['type_revision']-=1; Path('/tmp/stale.json').write_text(json.dumps(stale)); rejected=subprocess.run(['graphx','node-settings','--node','radio','--config','/tmp/stale.json'],capture_output=True,text=True); assert rejected.returncode and 'E_NODE_TYPE' in rejected.stderr; print('SDR authoritative release settings, stale revision rejection and scoped staged credentials passed')"
                    radio = create(['python3','-c',probe],radio_mounts,['-e','GRAPHX_CREDENTIALS=/run/secrets'],sdr_image)
                    result = run('docker','start','--attach',radio)
                    assert run('docker','inspect','--format','{{.State.ExitCode}}',radio) == '0',result
                    (output/'sdr-settings.log').write_text(result+'\n')
                if case == 'V02':
                    compose = json.loads((output/'compose.yaml').read_text())
                    for extension in ['prometheus', 'grafana']:
                        service = compose['services'][extension]
                        pin = service['image']
                        if subprocess.run(['docker','image','inspect',pin],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:
                            run('docker','pull',pin,timeout=180); extension_images.append(pin)
                        extension_mounts = []
                        for mount in service['volumes']:
                            source_, destination, _ = mount.split(':')
                            if source_.startswith('${GX_CREDENTIALS}/'):
                                ref = source_.split('/')[1]
                                assert extension in credential_manifest['allowed_consumers'][ref]
                                spec = f'type=volume,src={volumes[0]},dst={destination},volume-subpath=credentials/{ref},readonly'
                            else:
                                spec = f'type=bind,src={(output/source_).resolve()},dst={destination},readonly'
                            extension_mounts += ['--mount',spec]
                        extra = ['--pids-limit',str(service['pids_limit'])]
                        for key,value in service.get('environment',{}).items(): extra += ['-e',key+'='+value]
                        for value in service.get('tmpfs',[]): extra += ['--tmpfs',value]
                        service_port = service['ports'][0].split(':')[-1]
                        extra += ['-p','127.0.0.1::'+service_port]
                        extension_id = create(service.get('command',[]), extension_mounts, extra, pin, extension)
                        run('docker','start',extension_id)
                        published = int(run('docker','port',extension_id,service_port+'/tcp').rsplit(':',1)[1])
                        deadline = time.monotonic()+45
                        route = '/api/v1/targets' if extension == 'prometheus' else '/api/health'
                        while time.monotonic() < deadline:
                            try:
                                status, payload = request(published,route)
                                result = json.loads(payload)
                                if status == 200 and ((extension == 'grafana' and result.get('database') == 'ok') or
                                    (extension == 'prometheus' and any(target['health']=='up' for target in result.get('data',{}).get('activeTargets',[])))):
                                    break
                            except (OSError,ValueError,urllib.error.URLError): pass
                            time.sleep(.5)
                        else: raise RuntimeError(extension+' readiness failed: '+run('docker','logs',extension_id))
                        (output/(extension+'.log')).write_text(run('docker','logs',extension_id))
                run('docker','stop','--time','5',cid)
                logs = run('docker','logs',cid)
                (output/'platform.log').write_text(logs)
                assert 'durable history sqlite backend ready' in logs, logs
                assert run('docker','inspect','--format','{{.State.ExitCode}}',cid) == '0'
                evidence['cases'].append({'case':case,'status':'passed','container':cid,'uid':65532,'privileged':False})
            finally:
                for cid in reversed(containers):
                    assert run('docker','inspect','--format','{{index .Config.Labels "org.graphx.owner"}}',cid) == owner
                    run('docker','rm','--force',cid)
                for volume in volumes:
                    assert run('docker','volume','inspect','--format','{{index .Labels "org.graphx.owner"}}',volume) == owner
                    run('docker','volume','rm',volume)
                if network:
                    assert run('docker','network','inspect','--format','{{index .Labels "org.graphx.owner"}}',network) == owner
                    run('docker','network','rm',network)
                for pin in extension_images: run('docker','image','rm',pin)
    finally:
        if not present: run('docker','image','rm',image)
        evidence['after'] = sorted(run('docker','ps','-aq','--no-trunc').splitlines())
        (args.output/'verification.json').write_text(json.dumps(evidence,indent=2)+'\n')
        assert set(evidence['after']) == before, 'container inventory changed'
    print(f'{engine}: nine compiled platform cases and scoped SDR settings passed; existing containers preserved')


if __name__ == '__main__': main()
