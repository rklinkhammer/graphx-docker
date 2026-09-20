#!/usr/bin/env python3
"""Real P1 radios -> production P2 processor -> production detector, native/mTLS."""
import copy
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import select
import threading
import socket
import struct
import sys
import subprocess
import tempfile
import time
from test_vita_radio import BUILD, ROOT, certificates, port, run

class TlsRelay:
    """Bounded test-owned opaque TCP relay; never implements the VITA protocol."""
    def __init__(self, front, back):
        self.stop = threading.Event()
        self.cut = threading.Event()
        self.accepted = 0
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', front))
        self.listener.listen(1)
        self.listener.settimeout(.1)
        self.back = back
        self.thread = threading.Thread(target=self.serve)
        self.thread.start()

    def serve(self):
        while not self.stop.is_set():
            client = server = None
            try:
                client, _ = self.listener.accept()
                server = socket.create_connection(('127.0.0.1', self.back), timeout=.2)
                client.settimeout(.2)
                self.accepted += 1
                while not self.stop.is_set():
                    if self.cut.is_set():
                        self.cut.clear()
                        break
                    readable, _, _ = select.select([client, server], [], [], .02)
                    disconnected = False
                    for source in readable:
                        data = source.recv(4096)
                        if not data:
                            disconnected = True
                            break
                        (server if source is client else client).sendall(data)
                    if disconnected:
                        break
            except OSError:
                pass
            finally:
                if client:
                    client.close()
                if server:
                    server.close()

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)
        self.listener.close()
        assert not self.thread.is_alive()

with tempfile.TemporaryDirectory(prefix='graphx-vita-processing-') as directory:
    root=Path(directory).resolve();credentials=root/'credentials';credentials.mkdir();certificates(credentials)
    catalog=root/'catalog';shutil.copytree(ROOT/'config/catalog',catalog)
    for path in (ROOT/'config/vita/types').glob('*.json'):shutil.copy2(path,catalog/'types'/path.name)
    wire=json.loads((catalog/'wire-schemas.json').read_text());wire.update(json.loads((ROOT/'config/vita/wire-schemas.json').read_text()));(catalog/'wire-schemas.json').write_text(json.dumps(wire))
    lock=json.loads((catalog/'lock.json').read_text());paths={x['path'] for x in lock['files']}|{f'types/{p.name}' for p in (ROOT/'config/vita/types').glob('*.json')};lock['files']=[{'path':p,'sha256':hashlib.sha256((catalog/p).read_bytes()).hexdigest()} for p in sorted(paths)];(catalog/'lock.json').write_text(json.dumps(lock))
    graph={'version':3,'catalog':'catalog/lock.json','graph':{'id':'vita-processing'},'credentials':{n:{'identity':n,'members':['ca.pem','cert.pem','key.pem'],'provider':'lab-generated'} for n in ['radio','controller']},'nodes':{},'connections':{}}
    graph['nodes']['processor']={'type':'vita.processor','execution':{'kind':'native'},'credentials':{f'control{i}':'controller' for i in range(1,5)}}
    graph['nodes']['detector']={'type':'vita.detector','execution':{'kind':'native'}}
    mode=sys.argv[2] if len(sys.argv)>2 else ''
    missing=mode=='--missing'
    wrong_source=len(sys.argv)>2 and sys.argv[2]=='--wrong-source'
    active=3 if missing or wrong_source or mode in ('--busy-radio','--auth-failure') else 4
    boundary=len(sys.argv)>2 and sys.argv[2]=='--boundary'
    rate=1000003 if boundary else 1000000
    size=2048 if boundary else 1024
    graph['nodes']['processor']['parameters']={'window':int(boundary),'overlap_percent':50 if boundary else 0}
    for i in range(1,5):graph['nodes']['processor']['parameters'].update({f'rate{i}':rate,f'bin_width_numerator_hz{i}':rate,f'bin_width_denominator{i}':size,f'gain_db{i}':-1})
    ports=set()
    def fresh():
        while True:
            p=port()
            if p not in ports:ports.add(p);return p
    def edge(a,b,schema_tcp=False):
        p=fresh()
        value={'from':a,'to':b,'transport':'tcp' if schema_tcp else 'udp','settings':{'bind':'127.0.0.1','port':p,'framing':'none'}}
        if schema_tcp:value.update(security={'profile':'mtls','server_name':'radio'});value['settings']['host']='127.0.0.1'
        else:value['settings'].update(destination='127.0.0.1',max_datagram_bytes=8836 if b=='detector.spectra' else 4128,mode='unicast')
        return value
    for i in range(1,5):
        graph['nodes'][f'radio{i}']={'type':'vita.radio','execution':{'kind':'native'},'credentials':{'control':'radio'},'parameters':{'radio_index':i,'signal_frequency_hz':100000000+[-50000,100000,-150000,200000][i-1],'burst_samples':2050}}
        graph['connections'][f'control{i}']=edge(f'processor.control{i}',f'radio{i}.control',True)
        graph['connections'][f'data{i}']=edge(f'radio{i}.samples',f'processor.data{i}')
    graph['connections']['spectra']=edge('processor.spectra','detector.spectra')
    if mode=='--auth-failure':graph['connections']['control4']['security']['server_name']='wrong.invalid'
    path=root/'graphx.yml';path.write_text(json.dumps(graph))
    normalized=json.loads(run(BUILD/'graphx','config','normalize',path,'--catalog-root',catalog))
    # Cross-field validation is authoritative and rejects unsupported combinations.
    for parameters in ({'overlap_percent':30},{'bin_width_numerator_hz1':999},{'bandwidth1':2000000},{'bin_width_numerator_hz1':15625,'bin_width_denominator1':32,'path_mtu':4200}):
        bad=copy.deepcopy(graph);bad['nodes']['processor']['parameters']=parameters;path.write_text(json.dumps(bad));result=subprocess.run([str(BUILD/'graphx'),'config','normalize',str(path),'--catalog-root',str(catalog)],capture_output=True);assert result.returncode!=0,result.stdout
    # Observe the actual production spectrum bytes on a bounded local UDP relay.
    relay=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);relay.bind(('127.0.0.1',graph['connections']['spectra']['settings']['port']));relay.settimeout(.02)
    detector_port=fresh()
    for node in normalized['nodes']:
        if node['node_id']=='detector':node['bindings']['spectra'][0]['settings']['port']=detector_port
    for name,change in [('spectra',{'max_datagram_bytes':1400}),('data1',{'max_datagram_bytes':4096}),('spectra',{'mode':'multicast'})]:
        bad=copy.deepcopy(graph);bad['connections'][name]['settings'].update(change);path.write_text(json.dumps(bad));assert subprocess.run([str(BUILD/'graphx'),'config','normalize',str(path),'--catalog-root',str(catalog)],capture_output=True).returncode!=0
    tls_relay=None
    for node in normalized['nodes']:
        if wrong_source and node['node_id']=='processor':node['bindings']['data4'][0]['source_address']='127.0.0.2'
        if boundary and node['node_id']=='radio1':
            backend=fresh();node['bindings']['control'][0]['settings']['port']=backend
            tls_relay=TlsRelay(graph['connections']['control1']['settings']['port'],backend)
    release=root/'release';token='c'*64;release.write_text(token)
    processes=[];logs=[]
    occupied=None
    if mode=='--busy-radio':
        occupied=socket.socket();occupied.bind(('127.0.0.1',graph['connections']['control4']['settings']['port']));occupied.listen(1)
    try:
        for node in sorted(normalized['nodes'],key=lambda n:n['node_id']=='processor'):
            if (missing and node['node_id']=='radio4') or (mode=='--all-missing' and node['node_id'].startswith('radio')) or (mode=='--no-processor' and node['node_id']=='processor'):continue
            node['telemetry']['credential']=None
            config=root/(node['node_id']+'.json');config.write_text(json.dumps(node));log=(root/(node['node_id']+'.log')).open('w');logs.append(log)
            executable='graphx-vita-'+('radio' if node['node_id'].startswith('radio') else node['node_id'])
            app=subprocess.Popen([str(BUILD/executable),'--node',node['node_id'],'--config',str(config),'--release-file',str(release),'--release-token',token],stdout=log,stderr=log,env=dict(os.environ,GRAPHX_CREDENTIALS=str(credentials)));processes.append(app)
            if mode=='--busy-radio' and node['node_id']=='radio4':
                assert app.wait(timeout=3)==75
                processes.remove(app)
        if mode in ('--all-missing','--no-processor'):
            time.sleep(7)
            assert all(p.poll() is None for p in processes)
            if mode=='--all-missing':
                text=(root/'processor.log').read_text()
                for sid in range(1,5): assert f'stream={sid} results=unavailable' in text,text
                assert 'executed=1' not in text and 'spectra=0' in text,text
            else:
                for sid in range(1,5):
                    text=(root/f'radio{sid}.log').read_text()
                    assert 'packets=0' in text and not re.search(r'packets=[1-9]',text),text
            text=(root/'detector.log').read_text()
            for sid in range(1,5): assert f'detection stream={sid} results=unavailable' in text,text
            assert 'rf_hz=' not in text
            print(mode, 'survivors remain live; no acquisition/results fabricated')
            raise SystemExit(0)
        deadline=time.monotonic()+12;found={};counts={i:0 for i in range(1,active+1)};boundaries=0;epochs={};cut_at=None;last_seen={};first=time.monotonic()
        while time.monotonic()<deadline:
            for app in processes:assert app.poll() is None,{p.name:p.read_text() for p in root.glob('*.log')}
            for _ in range(256):
                try:wire,_=relay.recvfrom(9000)
                except socket.timeout:break
                assert wire[:8]==b'GXP2\x00\x01\x00\x80'
                length,sid,seq,n,hop=struct.unpack_from('!5I',wire,8)
                assert length==len(wire)==132+17*n//4 and n==size and hop==size//(2 if boundary else 1)
                assert struct.unpack_from('!Q',wire,40)[0]==rate
                epochsec,epochps,ordinal=struct.unpack_from('!3Q',wire,96);epochs[sid]=(epochsec,epochps)
                sec,ps,endsec,endps=struct.unpack_from('!4Q',wire,48)
                assert sec*10**12+ps==epochsec*10**12+epochps+ordinal*10**12//rate
                assert endsec*10**12+endps==epochsec*10**12+epochps+(ordinal+n)*10**12//rate
                valid,gaps=struct.unpack_from('!2I',wire,80);assert valid+gaps==n and gaps==sum(x.bit_count() for x in wire[128:128+n//8])
                last_seen[sid]=time.monotonic();counts[sid]+=1;boundaries+=sum(x.bit_count() for x in wire[128+n//8:132+n//4])
                relay.sendto(wire,('127.0.0.1',detector_port))
            text=(root/'detector.log').read_text()
            for sid,hz in re.findall(r'detection stream=(\d+).*?rf_hz=([0-9.e+-]+)',text):found[int(sid)]=float(hz)
            if len(found)==active and min(counts.values())>=20:
                if tls_relay and cut_at is None:
                    cut_at=time.monotonic();tls_relay.cut.set();counts={i:0 for i in counts}
                elif not tls_relay or (time.monotonic()-cut_at>1 and tls_relay.accepted>=2 and all(time.monotonic()-last_seen[i]<.3 for i in counts)):break
            time.sleep(.1)
        assert len(found)==active,{p.name:p.read_text() for p in root.glob('*.log')}
        for sid,hz in found.items():assert abs(hz-(100000000+[-50000,100000,-150000,200000][sid-1]))<=rate/size/2,(sid,hz)
        assert len(set(epochs.values()))==1,epochs
        controller=(root/'processor.log').read_text()
        assert all(f'controller stream={sid} executed=1' in controller for sid in counts),controller
        assert boundaries>0 and min(counts.values())>=20,counts
        print('observed spectra',counts,'seconds',time.monotonic()-first,'boundaries',boundaries,'N',size,'rate',rate)
        if tls_relay:assert tls_relay.accepted>=2
        if mode=='--auth-failure':
            text=(root/'processor.log').read_text()
            assert 'stream=4 results=unavailable control=authentication-failed attempts=1' in text,text
            text=(root/'radio4.log').read_text()
            assert 'packets=0' in text and not re.search(r'packets=[1-9]',text),text
            print('certificate name rejection: no commands/data, one attempt, healthy three streams continue')
        if mode=='--failures':
            radio4=next(p for p in processes if '--node' in p.args and p.args[p.args.index('--node')+1]=='radio4')
            radio4.kill();radio4.wait(timeout=3)
            deadline=time.monotonic()+3.5;healthy={1:0,2:0,3:0}
            while time.monotonic()<deadline:
                try:
                    packet,_=relay.recvfrom(9000);sid=struct.unpack_from('!I',packet,12)[0]
                    if sid in healthy:healthy[sid]+=1
                    relay.sendto(packet,('127.0.0.1',detector_port))
                except socket.timeout:pass
            assert min(healthy.values())>20,healthy
            assert 'stream=4 results=stale' in (root/'processor.log').read_text()
            assert 'detection stream=4 results=stale' in (root/'detector.log').read_text()
            processor=next(p for p in processes if 'graphx-vita-processor' in p.args[0])
            before={i:int(re.findall(r'packets=(\d+)',(root/f'radio{i}.log').read_text())[-1]) for i in healthy}
            processor.kill();processor.wait(timeout=3)
            time.sleep(3.5)
            for i in healthy:
                after=int(re.findall(r'packets=(\d+)',(root/f'radio{i}.log').read_text())[-1]);assert after>before[i],(i,before,after)
                assert f'detection stream={i} results=stale' in (root/'detector.log').read_text()
            assert all(p.poll() is None for p in processes if p not in (radio4,processor))
            assert radio4.poll() is not None and processor.poll() is not None
            print('runtime radio loss: healthy spectra',healthy,'controller loss: surviving radios advance; detector explicitly stale; no respawn')
            raise SystemExit(0)
        # Detector loss cannot block processor or radio control/acquisition.
        detector=next(p for p in processes if 'graphx-vita-detector' in p.args[0]);detector.terminate();assert detector.wait(timeout=3)==0
        time.sleep(1.2)
        assert all(p.poll() is None for p in processes if p!=detector)
        if wrong_source:
            rejected=[int(v) for v in re.findall(r' invalid=(\d+)',(root/'processor.log').read_text())];assert rejected and max(rejected)>0
        if boundary:
            # A stalled stdout reader must cause counted display drops, not an
            # unbounded queue or a blocked receive/lifecycle loop.
            probe=subprocess.Popen(detector.args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=dict(os.environ,GRAPHX_CREDENTIALS=str(credentials)));processes.append(probe)
            time.sleep(.15)
            for seq in range(5000):
                packet=bytearray(wire);struct.pack_into('!I',packet,16,seq);relay.sendto(packet,('127.0.0.1',detector_port))
                if seq%32==0:time.sleep(.001)
            assert probe.poll() is None
            os.set_blocking(probe.stdout.fileno(),False);display=b'';deadline=time.monotonic()+3
            zero=bytearray(wire);zero[132+size//4:]=bytes(4*size)
            while time.monotonic()<deadline:
                try:display+=os.read(probe.stdout.fileno(),65536)
                except BlockingIOError:pass
                relay.sendto(zero,('127.0.0.1',detector_port));relay.sendto(b'invalid',('127.0.0.1',detector_port));time.sleep(.02)
                drops=[int(n) for n in re.findall(rb'display_drops=(\d+)',display)]
                if drops and max(drops)>0 and b'rf_hz=none' in display and b'detection invalid' in display:break
            assert drops and max(drops)>0 and b'rf_hz=none' in display and b'detection invalid' in display,display[-2000:]
            probe.terminate();assert probe.wait(timeout=3)==0;probe.stdout.close();probe.stderr.close()
            print('stalled console recovery: counted display drops',max(drops),'zero and invalid results observed')
        print('four P1 radios, production controller/FFT/detector, signed tone offsets, config rejection and detector loss passed',found)
    finally:
        for app in processes:
            if app.poll() is None:app.terminate()
        for app in processes:
            try:app.wait(timeout=3)
            except subprocess.TimeoutExpired:app.kill();app.wait();raise
        for log in logs:log.close()
        relay.close()
        if tls_relay:tls_relay.close()
        if occupied:occupied.close()
