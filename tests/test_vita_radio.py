#!/usr/bin/env python3
"""Standalone acceptance: authoritative normalization, real TLS radio, independent VITA bytes."""
import copy
import hashlib
import json
import math
import signal
import threading
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / 'build/vita'

def run(*args):
    try:
        return subprocess.check_output([str(x) for x in args], stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        print(error.output.decode(), file=sys.stderr)
        raise

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def certificates(root):
    run('openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
        '-subj', '/CN=radio-test', '-addext', 'basicConstraints=critical,CA:TRUE',
        '-addext', 'keyUsage=critical,keyCertSign,cRLSign', '-keyout', root/'ca.key', '-out', root/'ca.pem')
    for name in ('radio', 'controller', 'stranger'):
        p = root/name
        p.mkdir()
        run('openssl', 'req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN='+name,
            '-keyout', p/'key.pem', '-out', p/'request.pem')
        (p/'extensions').write_text('subjectAltName=DNS:'+name+'\nextendedKeyUsage='+
                                   ('serverAuth' if name == 'radio' else 'clientAuth')+'\n')
        run('openssl', 'x509', '-req', '-days', '1', '-in', p/'request.pem',
            '-CA', root/'ca.pem', '-CAkey', root/'ca.key', '-CAcreateserial',
            '-extfile', p/'extensions', '-out', p/'cert.pem')
        (p/'ca.pem').write_bytes((root/'ca.pem').read_bytes())
        (p/'key.pem').chmod(0o600)
        (p/'generation.json').write_text(json.dumps({'version':1,'generation':1,
            'members':{n:hashlib.sha256((p/n).read_bytes()).hexdigest() for n in ('ca.pem','cert.pem','key.pem')}}))

def fixture(root, index, udp_port, tcp_port, burst=2050):
    # A private catalog adds native test peers, not a production launcher.
    import shutil
    catalog = root/'catalog'
    shutil.copytree(ROOT/'config/catalog', catalog)
    shutil.copy2(ROOT/'config/vita/types/vita.radio.json', catalog/'types/vita.radio.json')
    wire = json.loads((catalog/'wire-schemas.json').read_text())
    wire.update(json.loads((ROOT/'config/vita/wire-schemas.json').read_text()))
    (catalog/'wire-schemas.json').write_text(json.dumps(wire))
    radio = json.loads((catalog/'types/vita.radio.json').read_text())
    peer = copy.deepcopy(radio)
    peer.update(id='vita.test-peer', credentials=['control'])
    peer['ports']['samples'].update(direction='input', listener=True)
    peer['ports']['control'].update(direction='output', listener=False)
    (catalog/'types/vita.test-peer.json').write_text(json.dumps(peer))
    lock = json.loads((catalog/'lock.json').read_text())
    paths = {x['path'] for x in lock['files']} | {'types/vita.radio.json','types/vita.test-peer.json'}
    lock['files'] = [{'path':p,'sha256':hashlib.sha256((catalog/p).read_bytes()).hexdigest()} for p in sorted(paths)]
    (catalog/'lock.json').write_text(json.dumps(lock))
    graph = {'version':3,'catalog':'catalog/lock.json','graph':{'id':'vita-test'},
        'credentials': {n:{'identity':n,'members':['ca.pem','cert.pem','key.pem'],'provider':'lab-generated'}
                        for n in ('radio','controller')},
        'nodes': {'radio':{'type':'vita.radio','execution':{'kind':'native'},
                 'credentials':{'control':'radio'},'parameters':{'radio_index':index,'burst_samples':burst,
                    'signal_frequency_hz':100000000+{1:50000,2:100000,3:-150000,4:200000}[index]}},
                  'controller':{'type':'vita.test-peer','execution':{'kind':'native'},
                    'credentials':{'control':'controller'}}},
        'connections':{
          'control':{'from':'controller.control','to':'radio.control','transport':'tcp',
            'security':{'profile':'mtls','server_name':'radio'},
            'settings':{'bind':'127.0.0.1','host':'127.0.0.1','port':tcp_port,'framing':'none'}},
          'iq':{'from':'radio.samples','to':'controller.samples','transport':'udp',
            'settings':{'bind':'127.0.0.1','destination':'127.0.0.1','port':udp_port,
                        'max_datagram_bytes':4128,'mode':'unicast','framing':'none'}}}}
    path=root/'graphx.yml'; path.write_text(json.dumps(graph))
    normalized=json.loads(run(BUILD/'graphx','config','normalize',path,'--catalog-root',catalog))
    node=next(n for n in normalized['nodes'] if n['node_id']=='radio')
    node['telemetry']['credential']=None
    result=root/'radio.json'; result.write_text(json.dumps(node))
    return result

def command(index, message, kind, *, rate=1000000, frequency=100000000, bandwidth=800000, gain=0, at=0, selectors=0x28a00002, status_reply=False, admission=False):
    sec=int(at); ps=round((at-sec)*1e12)
    cam=0xa0000000  # enabled word-format controller and controllee identifiers
    cif=0; fields=b''
    if kind in ('status','capabilities'):
        cam |= 1<<18
        cif=selectors
        if kind=='capabilities':
            cif=0x28a00080;fields=struct.pack('!I',0x0c000000)
        if cif&2: fields=struct.pack('!I',64)
    else:
        cam |= (2<<23)|(1<<19)|(3<<16)
        if status_reply: cam |= 1<<18
        if admission: cam |= 1<<20
        if kind=='configure':
            cif=0x28a00000
            fields=struct.pack('!qqIq',int(bandwidth*2**20),int(frequency*2**20),
                               int(gain*128)&65535,int(rate*2**20))
        else:
            cif=2
            fields=struct.pack('!II',64,3 if kind=='start' else 2)
            if kind=='start': cam |= 1<<12
    body=struct.pack('!IIQIIIII',index,sec,ps,cam,message,index,1,cif)+fields
    return struct.pack('!I',0x60600000|((len(body)+4)//4))+body

def receive(s):
    def exact(n):
        b=b''
        while len(b)<n:
            r=s.recv(n-len(b))
            if not r: raise EOFError('TLS closed')
            b+=r
        return b
    header=exact(4); size=(struct.unpack('!I',header)[0]&65535)*4
    assert 36<=size<=1024
    return header+exact(size-4)

def replay_equal(a,b):
    # Retries retain semantic result/time, with a new packet-family sequence count.
    return (int.from_bytes(a[:4],'big')&~0xf0000)==(int.from_bytes(b[:4],'big')&~0xf0000) and a[4:]==b[4:]

def data_packet(b):
    header, sid = struct.unpack_from('!II',b)
    assert (header&65535)*4==len(b)
    assert header>>28==1 and header&(1<<27) and header&(1<<26)
    assert b[8:16]==bytes.fromhex('00ffffff00000000')
    sec,ps=struct.unpack_from('!IQ',b,16)
    assert ps<10**12
    count=(len(b)-32)//4
    assert 1<=count<=1024
    trailer=struct.unpack_from('!I',b,len(b)-4)[0]
    samples=struct.unpack_from('!'+('hh'*count),b,28)
    return sid,sec*10**12+ps,(trailer>>10)&3,samples,(header>>16)&15

def connect(root, tcp, who='controller'):
    context=ssl.create_default_context(cafile=str(root/'ca.pem'))
    context.load_cert_chain(root/who/'cert.pem',root/who/'key.pem')
    peer=context.wrap_socket(socket.create_connection(('127.0.0.1',tcp),timeout=2),server_hostname='radio')
    peer.settimeout(3)
    return peer

def capability_query(peer,index,message):
    request=command(index,message,'capabilities')
    peer.sendall(request);reply=receive(peer)
    assert len(reply)==100 and reply[0]&8==0,reply.hex()
    assert struct.unpack_from('!I',reply,20)[0]&(1<<18)
    assert struct.unpack_from('!III',reply,24)==(message,index,1)
    assert struct.unpack_from('!II',reply,36)==(0x28a00080,0x0c000000)
    assert struct.unpack_from('!qqqqIIqq',reply,44)==(
        2000000*2**20,1*2**20,6000000000*2**20,1000000*2**20,
        60*128,(-60*128)&65535,2000000*2**20,1000*2**20),reply.hex()

def acceptance(root,index,barrier,shared):
    burst = {1:2050, 2:262144, 3:32, 4:2050}[index]
    rate=32000 if index==3 else 1000003
    bandwidth=16000 if index==3 else 800000
    tone = {1:50000, 2:100000, 3:-150000, 4:200000}[index]
    root.mkdir(); credentials=root/'credentials';credentials.mkdir();certificates(credentials)
    udp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);udp.bind(('127.0.0.1',0));udp.settimeout(.15)
    tcp=port(); node=fixture(root,index,udp.getsockname()[1],tcp,burst)
    release=root/'release';token='a'*64;release.write_text(token)
    bad_node=json.loads(node.read_text());bad_node['parameters']['radio_index']=5
    invalid=root/'invalid.json';invalid.write_text(json.dumps(bad_node))
    invalid_run=subprocess.run([str(BUILD/'graphx-vita-radio'),'--node','radio','--config',str(invalid),
        '--release-file',str(release),'--release-token',token],capture_output=True,timeout=3)
    assert invalid_run.returncode!=0 and b'E_SCHEMA' in invalid_run.stderr,invalid_run.stderr
    log=(root/'radio.log').open('w+')
    env=dict(os.environ,GRAPHX_CREDENTIALS=str(credentials))
    app=subprocess.Popen([str(BUILD/'graphx-vita-radio'),'--node','radio','--config',str(node),
        '--release-file',str(release),'--release-token',token],stdout=log,stderr=log,env=env)
    try:
        deadline=time.monotonic()+5
        while 'ready node=radio' not in (root/'radio.log').read_text():
            assert app.poll() is None,(root/'radio.log').read_text()
            assert time.monotonic()<deadline
            time.sleep(.02)
        s=connect(credentials,tcp)
        capability_query(s,index,1)
        cfg=command(index,2,'configure',rate=rate,bandwidth=bandwidth,status_reply=True)
        s.sendall(cfg[:7]);time.sleep(.01);s.sendall(cfg[7:]);ack=receive(s)
        assert struct.unpack_from('!I',ack,20)[0]&(1<<19),ack.hex()
        applied=receive(s)
        assert struct.unpack_from('!I',applied,20)[0]&(1<<18)
        assert struct.unpack_from('!I',applied,36)[0]==0x28a00000
        assert struct.unpack_from('!qqIq',applied,40)==(bandwidth*2**20,100000000*2**20,0,rate*2**20)
        try: udp.recv(65535);raise AssertionError('configuration emitted IQ')
        except socket.timeout: pass
        barrier.wait(timeout=10)
        start=shared['start']
        req=command(index,3,'start',at=start,admission=True);s.sendall(req)
        admitted=receive(s)
        assert struct.unpack_from('!I',admitted,20)[0]&(1<<20)
        assert time.time()<start
        # Delay each owned process across the activation deadline; no app test hooks.
        def delay_activation():
            time.sleep(max(0,start-time.time()-.025))
            os.kill(app.pid,signal.SIGSTOP)
            try: time.sleep(max(0,start-time.time()+index*.005))
            finally: os.kill(app.pid,signal.SIGCONT)
        delay=threading.Thread(target=delay_activation)
        delay.start()
        ack=receive(s);delay.join(timeout=2)
        assert not delay.is_alive()
        assert struct.unpack_from('!I',ack,20)[0]&(1<<19)
        assert not struct.unpack_from('!I',ack,20)[0]&(1<<16),ack.hex()
        actual_sec,actual_ps=struct.unpack_from('!IQ',ack,8)
        actual=actual_sec*10**12+actual_ps
        scheduled_sec,scheduled_ps=struct.unpack_from('!IQ',req,8)
        scheduled=scheduled_sec*10**12+scheduled_ps
        assert index*4_000_000_000 <= actual-scheduled <= 100_000_000_000,(index,actual-scheduled)
        s.sendall(req);assert replay_equal(receive(s),admitted)
        assert replay_equal(receive(s),ack)
        udp.settimeout(1)
        frames=[];contexts=[];received_at=None;first_context=None
        frame_count = 512 if burst == 262144 else 20
        while len(frames)<frame_count:
            b=udp.recv(65535)
            if b[0]>>4==4:
                header,sid,sec,ps,cif=struct.unpack_from('!IIIQI',b)
                assert len(b)==52 and header&0x08000000==0 and sid==index
                assert cif&0x7fffffff==0x28a00000,b.hex()
                assert struct.unpack_from('!qqIq',b,24)==(bandwidth*2**20,100000000*2**20,0,rate*2**20)
                assert (header>>16)&15==len(contexts)%16
                contexts.append(sec*10**12+ps)
            if b[0]>>4==1:
                if not frames: first_context=contexts[-1] if contexts else None
                received_at=received_at or time.time();frames.append(data_packet(b))
        assert start-.005<=received_at<=start+.1,(start,received_at)
        if burst==2050:
            assert [len(f[3])//2 for f in frames[:3]]==[1024,1024,2]
            assert [f[2] for f in frames[:3]]==[1,2,3]
        elif burst==32:
            assert all(f[2]==0 and len(f[3])==64 for f in frames)
        else:
            assert frames[0][2]==1 and frames[255][2]==3 and frames[256][2]==1
        scheduled_sec,scheduled_ps=struct.unpack_from('!IQ',req,8)
        scheduled=scheduled_sec*10**12+scheduled_ps
        assert frames[0][1]==scheduled, ('scheduled sample epoch mismatch',index,
            'scheduled_ps',scheduled,'first_sample_ps',frames[0][1],
            'difference_ps',frames[0][1]-scheduled)
        assert first_context==scheduled,(contexts,scheduled)
        assert scheduled+burst*10**12//rate in contexts
        offset=0
        for n,f in enumerate(frames):
            assert f[0]==index and f[4]==n%16
            assert f[1]==scheduled+offset*10**12//rate,(index,n,f[1],scheduled+offset*10**12//rate)
            for k,(i,q) in enumerate(zip(f[3][::2],f[3][1::2])):
                phase=2*math.pi*(tone/rate)*(offset+k)
                amplitude=8191.75 if abs(tone)<=bandwidth/2 and abs(tone)<rate/2 else 0
                assert abs(i-round(amplitude*math.cos(phase)))<=1
                assert abs(q-round(amplitude*math.sin(phase)))<=1
            offset+=len(f[3])//2
        # A controller reconnect must not reconfigure or restart acquisition.
        s.close();time.sleep(.02)
        s=connect(credentials,tcp)
        s.sendall(req);assert replay_equal(receive(s),admitted)
        assert replay_equal(receive(s),ack)
        capability_query(s,index,4)
        # Losing the data receiver must not block control or bounded shutdown.
        s.sendall(command(index,5,'stop'));receive(s)
        # A coalesced status pair preserves framing and operation correlation.
        s.sendall(command(index,6,'status',selectors=1<<21)+command(index,7,'status'))
        selected=receive(s)
        assert struct.unpack_from('!I',selected,24)[0]==6
        assert (struct.unpack_from('!I',selected,20)[0]>>23)&3==0
        assert len(selected)==48 and struct.unpack_from('!I',selected,36)[0]==1<<21
        assert struct.unpack_from('!q',selected,40)[0]==rate*2**20
        full_status=receive(s)
        assert struct.unpack_from('!I',full_status,24)[0]==7
        assert struct.unpack_from('!II',full_status,36)==(0x28a00002,64)
        assert struct.unpack_from('!qqIqI',full_status,44)==(bandwidth*2**20,100000000*2**20,0,rate*2**20,2)
        s.sendall(command(index,8,'configure',rate=0));bad=receive(s)
        assert struct.unpack_from('!I',bad,20)[0]&(1<<16)
        # Independent EIF masks/reason bits; invalid settings must not mutate state.
        cases=[({'frequency':0},1<<27,1<<28),
               ({'bandwidth':2000000},1<<29,1<<28),
               ({'gain':61},1<<23,1<<28),
               ({'rate':1000000.5},1<<21,1<<27)]
        for message,(settings,field,reason) in enumerate(cases,9):
            s.sendall(command(index,message,'configure',**settings));rejected=receive(s)
            cam=struct.unpack_from('!I',rejected,20)[0]
            assert cam&(1<<19) and cam&(1<<16) and not cam&(1<<10)
            fields={}
            position=36
            masks=[]
            for enabled in (cam&(1<<17),cam&(1<<16)):
                if not enabled: continue
                mask=struct.unpack_from('!I',rejected,position)[0];position+=4
                assert mask&~0x28a00000==0,rejected.hex()
                masks.append(mask)
            for mask in masks:
                for selected_field in [1<<29,1<<27,1<<23,1<<21]:
                    if mask&selected_field:
                        value=struct.unpack_from('!I',rejected,position)[0];position+=4
                        fields[selected_field]=fields.get(selected_field,0)|value
            assert position==len(rejected),rejected.hex()
            assert fields[field]&reason,rejected.hex()
        s.sendall(command(index,13,'status',selectors=0x28a00000));unchanged=receive(s)
        assert struct.unpack_from('!qqIq',unchanged,40)==(bandwidth*2**20,100000000*2**20,0,rate*2**20)
        s.sendall(command(index,14,'start',at=time.time()-1));late=receive(s)
        assert (struct.unpack_from('!I',late,20)[0]>>12)&7==7
        assert struct.unpack_from('!II',late,36)==(2,64)
        assert struct.unpack_from('!I',late,44)[0]&(1<<25),late.hex()
        capability_query(s,index,15)
        # Drain old UDP and restart with a new epoch without reconfiguration.
        udp.setblocking(False)
        try:
            while True: udp.recv(65535)
        except BlockingIOError: pass
        udp.settimeout(1)
        restart=time.time()+.1
        second_start=command(index,16,'start',at=restart)
        s.sendall(second_start);second_ack=receive(s)
        assert struct.unpack_from('!I',second_ack,20)[0]&(1<<10),second_ack.hex()
        second_epoch=struct.unpack_from('!I',second_start,8)[0]*10**12+struct.unpack_from('!Q',second_start,12)[0]
        prior_context=None
        while True:
            wire=udp.recv(65535)
            if wire[0]>>4==4:
                sec,ps=struct.unpack_from('!IQ',wire,8);prior_context=sec*10**12+ps
            if wire[0]>>4==1:
                restarted=data_packet(wire)
                assert restarted[1]==second_epoch and prior_context==second_epoch
                assert restarted[3][:2]==((8192,0) if index!=3 else (0,0))
                break
        s.sendall(command(index,17,'stop'));receive(s)
        if index==1:
            # Miss the execution window after successful admission. Never activate.
            missed=time.time()+.1
            s.sendall(command(index,18,'start',at=missed,admission=True))
            assert struct.unpack_from('!I',receive(s),20)[0]&(1<<20)
            os.kill(app.pid,signal.SIGSTOP)
            try: time.sleep(max(0,missed-time.time()+.15))
            finally: os.kill(app.pid,signal.SIGCONT)
            failed_start=receive(s)
            assert struct.unpack_from('!I',failed_start,20)[0]&(1<<16)
            assert struct.unpack_from('!I',failed_start,44)[0]&(1<<25)
            s.sendall(command(index,19,'status'));stopped=receive(s)
            assert struct.unpack_from('!I',stopped,len(stopped)-4)[0]==2
            # Exercise retention exhaustion while proving old IDs cannot mutate.
            saturated=False
            for message in range(20,280):
                s.sendall(command(index,message,'status',selectors=1<<21))
                try: reply=receive(s)
                except (EOFError,ConnectionError):
                    assert message>=240,message
                    saturated=True
                    break
                assert struct.unpack_from('!I',reply,24)[0]==message
            assert saturated, '256-entry retention limit not exercised'
            s.close()
            # The library retains results for at least 30 seconds; capacity refusal
            # is bounded admission, not permission to evict live retry protection.
            time.sleep(31)
            assert app.poll() is None
            s=connect(credentials,tcp)
            s.sendall(command(index,2,'configure',frequency=101000000))
            # A fresh read-only operation proves rejection did not alter settings.
            s.sendall(command(index,280,'status',selectors=1<<27))
            current=receive(s)
            # Admission errors can omit timestamps. Locate CAM/message ID from
            # header TSI/TSF instead of assuming the successful AckS layout.
            header=struct.unpack_from('!I',current)[0]
            cam_offset=8+(4 if (header>>22)&3 else 0)+(8 if (header>>20)&3 else 0)
            cam,reply_id=struct.unpack_from('!II',current,cam_offset)
            if reply_id==2:
                assert cam&(1<<16),current.hex()
                current=receive(s)
            assert struct.unpack_from('!I',current,24)[0]==280,current.hex()
            assert struct.unpack_from('!q',current,40)[0]==100000000*2**20
        udp.close()
        s.close();time.sleep(.05)
        badpeer=connect(credentials,tcp,'stranger')
        try:
            badpeer.sendall(command(index,15,'status'));receive(badpeer)
            raise AssertionError('unauthorized identity accepted')
        except (ssl.SSLError,EOFError,ConnectionError): pass
        finally: badpeer.close()
        time.sleep(.02)
        malformed=connect(credentials,tcp)
        malformed.sendall(struct.pack('!I',0x6060ffff))
        try:
            receive(malformed); raise AssertionError('oversized frame accepted')
        except (EOFError,ssl.SSLError,ConnectionError): pass
        finally: malformed.close()
        # Query selectors carry no setting payload and cannot be empty.
        payload_query=command(index,16,'status',selectors=1<<21)+b'\0'*8
        header=struct.unpack_from('!I',payload_query)[0]
        payload_query=struct.pack('!I',(header&0xffff0000)|(len(payload_query)//4))+payload_query[4:]
        for invalid_query in (command(index,16,'status',selectors=0),payload_query):
            time.sleep(.02)
            peer=connect(credentials,tcp)
            try:
                peer.sendall(invalid_query);receive(peer)
                raise AssertionError('malformed query accepted')
            except (EOFError,ssl.SSLError,ConnectionError): pass
            finally: peer.close()
        # Stalled handshake and partial VRT frame expire without blocking the next peer.
        for tls in (False,True):
            time.sleep(.03)
            stalled=connect(credentials,tcp) if tls else socket.create_connection(('127.0.0.1',tcp),timeout=3)
            stalled.settimeout(3)
            if tls: stalled.sendall(command(index,1000,'status')[:5])
            began=time.monotonic()
            try: assert stalled.recv(1)==b''
            except (ssl.SSLError,ConnectionError): pass
            finally: stalled.close()
            assert time.monotonic()-began<2.8
        time.sleep(.03)
        healthy=connect(credentials,tcp)
        capability_query(healthy,index,1001)
        healthy.close()
        print(f'radio {index}: {len(frames)} data packets, {offset} samples, '
              f'first emission delay {(received_at-start)*1000:.3f} ms; '
              f'actual activation delay {(actual-scheduled)/1e9:.3f} ms; '
              'common epoch/context, rational bursts, restart, capabilities and mTLS failures passed')
    finally:
        os.kill(app.pid,signal.SIGCONT) if app.poll() is None else None
        app.terminate()
        try: app.wait(timeout=3)
        except subprocess.TimeoutExpired: app.kill();app.wait();raise
        log.close();udp.close()
        if app.returncode: print((root/'radio.log').read_text())

if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='graphx-vita-') as directory:
        from concurrent.futures import ThreadPoolExecutor
        shared={}
        barrier=threading.Barrier(4,action=lambda:shared.update(start=time.time()+.4))
        with ThreadPoolExecutor(max_workers=4) as pool:
            futures=[pool.submit(acceptance,Path(directory).resolve()/str(index),index,barrier,shared) for index in range(1,5)]
            for future in futures: future.result()
