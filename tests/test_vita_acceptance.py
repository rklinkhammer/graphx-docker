#!/usr/bin/env python3
"""P6 maintained-example acceptance. Preparation is offline; runs require authorization."""
import argparse
import copy
import json
import os
from pathlib import Path
import re
import signal
import uuid
import socket
import subprocess
import sys
import threading
import time

from vita_acceptance_wire import Measurements, PS
from test_vita_live import Fixture, inventory, preserved
from vita_live_support import capture_has_packet_after

SOURCE=Path(__file__).resolve().parents[1]
NODES=['radio1','radio2','radio3','radio4','processor','detector','recorder']


def write(path,value):
    temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(value,indent=2)+'\n');temporary.replace(path)


def call(*args,ok=True,timeout=90):
    result=subprocess.run(list(map(str,args)),capture_output=True,text=True,timeout=timeout)
    if ok and result.returncode: raise AssertionError((args,result.returncode,result.stdout[-3000:],result.stderr[-3000:]))
    return result


class Case:
    def __init__(self,args,name):
        self.args=args;self.name=name;self.root=args.output/name
        self.root.mkdir(exist_ok=True);self.instance='vita-p6-'+args.output.name+'-'+name
        self.base=[args.cli,'example'];self.started=False
        self.command('prepare')
        folder=args.workspace/args.target/self.instance
        record=json.loads((folder/'current.json').read_text())
        self.generation=folder/record['generation']
        self.resolved=json.loads((self.generation/'compiled/resolved.json').read_text())
        self.graph=self.resolved['graph_id'];self.ledger=self.generation/'state'/self.graph/'ownership.yml'

    def command(self,action,*extra,ok=True):
        result=call(*self.base,action,'four-radio-vita','--source',SOURCE,'--target',self.args.target,
                    '--workspace',self.args.workspace,'--instance',self.instance,
                    '--images',self.args.images,'--allow-privileged',*extra,ok=ok,timeout=240)
        # Tokens are used only for the explicit browser checkpoint, never written into evidence.
        text=re.sub(r'("(?:observation_token|control_token)"\s*:\s*)"[^"]*"',r'\1"<redacted>"',result.stdout+result.stderr)
        (self.root/(action+'-'+str(time.monotonic_ns())+'.log')).write_text(text)
        return result

    def state_value(self): return Fixture.state_value(self)
    def container(self,name,running=None): return Fixture.container(self,name,running)
    def logs(self,name): return Fixture.logs(self,name)
    def counters(self,name): return Fixture.counters(self,name)

    def snapshot(self):
        values={}
        for name in NODES:
            c=self.container(name);pid=c['State']['Pid'];logs=self.logs(name)
            rss=0
            if pid:
                match=re.search(r'^VmRSS:\s+(\d+)',Path(f'/proc/{pid}/status').read_text(),re.M)
                rss=int(match[1])*1024 if match else 0
            values[name]={'id':c['Id'],'running':c['State']['Running'],'restart_count':c['RestartCount'],
                          'rss':rss,'counter':self.counters(name)}
            (self.root/(name+'.log')).write_text(logs)
        return values

    def sample(self,seconds,monitor=None):
        samples=[];deadline=time.monotonic()+seconds
        while True:
            if monitor: monitor.snapshot()
            samples.append({'time':time.monotonic(),'nodes':self.snapshot()})
            if time.monotonic()>=deadline: break
            time.sleep(min(5,max(0,deadline-time.monotonic())))
        return samples

    def checkpoint(self,label):
        if not self.args.browser: return
        write(self.root/('browser-'+label+'.json'),{'instance':self.instance,'graph':self.graph,
              'console':('http://127.0.0.1:18080' if self.args.target=='lima' else
                         'http://127.0.0.1:'+str(self.resolved['platform']['console']['port'])),
              'deadline_seconds':180})
        deadline=time.monotonic()+180
        while not (self.root/('browser-'+label+'.done')).exists():
            if time.monotonic()>deadline: raise TimeoutError('browser checkpoint '+label)
            time.sleep(.5)

    def capture(self,after_epoch=0):
        capture=next(r for r in self.state_value()['resources'] if r['kind']=='network_capture')
        directory=Path(capture['session_directory']);deadline=time.monotonic()+10
        while True:
            files=list(directory.glob('*.pcapng'));fresh=False
            for path in files:
                fresh |= capture_has_packet_after(path,after_epoch)
            if fresh:break
            if time.monotonic()>deadline:raise AssertionError('fresh diagnostic packets unavailable')
            time.sleep(.2)
        sizes={str(path):path.stat().st_size for path in directory.glob('*.pcapng')}
        assert 0<len(sizes)<=2 and all(size<=4194304+9022+4096 for size in sizes.values())
        write(self.root/'capture.json',{'fresh_after_epoch':after_epoch,'files':sizes})

    def stop(self):
        if not self.started:return
        before=time.monotonic();self.command('down');elapsed=time.monotonic()-before
        assert elapsed<60
        state=self.state_value();assert not state.get('resources')
        assert all(p['kind']=='volume' and p['name']=='graphx-'+self.graph+'-history' for p in state.get('processes',[]))
        assert not (self.ledger.parent/'barriers/release').exists()
        write(self.root/'cleanup.json',{'seconds':elapsed,'active_resources':0,'retained':state.get('processes',[])})
        self.started=False


class Observer:
    def __init__(self,case,endpoint_id='diagnostic:diagnostic'):
        self.endpoint_id=endpoint_id;self.case=case;self.measure=Measurements();self.stop=threading.Event();self.lock=threading.Lock()
        self.error=None;self.drops=0;self.thread=threading.Thread(target=self.receive,daemon=True);self.thread.start()
    def receive(self):
        namespace=None
        try:
            deadline=time.monotonic()+180;sock=None
            while not self.stop.is_set() and time.monotonic()<deadline:
                if self.case.ledger.exists():
                    state=self.case.state_value()
                    endpoints=state.get('expected_endpoints',[])
                    endpoint=next((e for e in endpoints if e['id']==self.endpoint_id),None)
                    if endpoint:
                        resource=next((r for r in state.get('resources',[]) if r.get('attachment_id')==endpoint['id'] and r['kind'] in ('mirror_veth','container_veth')),None)
                        if resource:
                            if endpoint.get('container_id') and namespace is None:
                                container=self.case.container(endpoint['owner'],True)
                                assert container['Id']==endpoint['container_id']==resource['container_id']
                                namespace=os.open('/proc/self/ns/net',os.O_RDONLY)
                                target=os.open(f"/proc/{container['State']['Pid']}/ns/net",os.O_RDONLY)
                                try:
                                    assert os.fstat(target).st_ino==resource['namespace_inode']
                                    os.setns(target,0)
                                finally:os.close(target)
                            live=json.loads(call('ip','-j','link','show',endpoint['target_interface'],ok=False).stdout or '[]')
                            if live and live[0]['ifindex']==resource['peer_ifindex']:
                                assert live[0]['ifalias']==f"graphx:{state['owner_token']}:{endpoint['id']}:peer"
                                sock=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(3))
                                # Linux SO_RCVBUFFORCE applies only to this owned observer
                                # socket; default rmem_max otherwise clamps the requested
                                # queue below one scheduling pause at acceptance load.
                                sock.setsockopt(socket.SOL_SOCKET,33,4*1024*1024)
                                assert sock.getsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF)>=4*1024*1024
                                sock.bind((endpoint['target_interface'],0));sock.settimeout(.1);break
                time.sleep(.005)
            if not sock: raise RuntimeError('owned observer interface unavailable')
            try:
                while not self.stop.is_set():
                    try: frame=sock.recv(9023)
                    except socket.timeout:continue
                    with self.lock:self.measure.observe(frame)
                import struct
                _,self.drops=struct.unpack('II',sock.getsockopt(263,6,8))
            finally:sock.close()
        except Exception as error:self.error=repr(error)
        finally:
            if namespace is not None:
                os.setns(namespace,0);os.close(namespace)
    def snapshot(self):
        if self.error:raise AssertionError(self.error)
        with self.lock:return copy.deepcopy(self.measure.report())
    def phase(self,name):
        with self.lock:self.measure.phase=name
    def close(self):
        self.stop.set();self.thread.join(2);assert not self.thread.is_alive()
        with self.lock:result=copy.deepcopy(self.measure.report())
        result.update(observer_kernel_drops=self.drops,error=self.error)
        return result


def progress(samples,names):
    for name in names:
        first=samples[0]['nodes'][name];last=samples[-1]['nodes'][name]
        assert first['id']==last['id'] and last['running'] and last['counter']>first['counter'],name
        assert last['restart_count']==0,name


def baseline(case):
    observer=Observer(case)
    try:
        case.started=True;case.command('up','--no-open','--json')
        samples=case.sample(case.args.seconds,observer);wire=observer.snapshot()
        write(case.root/'baseline.json',{'samples':samples,'wire':wire})
        progress(samples,NODES)
        for node in case.resolved['nodes']:
            rss=[s['nodes'][node['node_id']]['rss'] for s in samples]
            # The executable contract bounds every application at 128 MiB.
            assert max(rss)<134217728,(node['node_id'],max(rss))
            middle=max(1,len(rss)//2);assert max(rss[middle:])-max(rss[:middle])<=16*1024*1024
        assert not wire['fragments'] and wire['max_iq']==4128 and wire['max_spectrum']==8836
        assert len(wire['control_peers'])==4
        assert len(wire['epochs'])==4 and len({x[0] for x in wire['epochs'].values()})==1
        for sid,s in wire['streams'].items():
            epoch,rate=wire['epochs'][sid];first=wire['first_iq'][sid]
            assert first['time']==epoch and first['context']==epoch
            assert 0<=first['arrival']-epoch/PS<=.1,(sid,first['arrival']-epoch/PS)
            elapsed=(s['last']-s['first'])/PS
            measured=(s['samples']-s['last_count'])/elapsed
            assert abs(measured-rate)/rate<=.02,(sid,measured,rate)
            s['measured_rate']=measured
            assert wire['spectra'][sid]['frequency_checks']>0 and s['signal_checks']>0
        wire['first_arrival_skew_ms']=1000*(max(x['arrival'] for x in wire['first_iq'].values())-min(x['arrival'] for x in wire['first_iq'].values()))
        write(case.root/'baseline.json',{'samples':samples,'wire':wire})
        case.capture(time.time()-10)
        case.checkpoint('baseline')
        observer.phase('loss-jitter')
        receiver=Observer(case,'processor-data')
        try:
            case.command('scenario','--action','iq-loss-jitter','--operation','run')
            impaired=case.sample(15,receiver);progress(impaired,NODES)
            case.command('scenario','--action','iq-loss-jitter','--operation','clear')
        finally:
            received=receiver.close();write(case.root/'impaired-receiver.json',received)
        assert sum(s['reordered'] for s in received['streams'].values())>0
        assert received['error'] is None and received['observer_kernel_drops']==0,(received['error'],received['observer_kernel_drops'])
        fault=observer.snapshot();assert fault['phase_counts']['loss-jitter']['padded']>0
        assert all(f'stream={i} results=available control=connected' in case.logs('processor') for i in range(1,5))
        write(case.root/'impairment.json',{'samples':impaired,'wire':fault})
    finally:
        observed=observer.close();write(case.root/'observer-final.json',observed)
        assert observed['error'] is None and observed['observer_kernel_drops']==0,(observed['error'],observed['observer_kernel_drops'])


def failure(case,node):
    case.started=True;case.command('up','--no-open','--json');before=case.snapshot()
    victim=case.container(node,True);death_epoch=time.time();call('docker','kill','--signal','KILL',victim['Id'])
    time.sleep(3);samples=case.sample(10)
    after=case.container(node,False);assert after['Id']==victim['Id'] and after['RestartCount']==0
    status=case.command('status',ok=False);assert f'application={node} admission=ready status=unavailable' in status.stdout
    healthy=[n for n in NODES if n!=node and n!='detector' and not (node=='processor' and n=='processor')]
    progress(samples,healthy)
    if node.startswith('radio'):
        index=int(node[-1]);assert f'stream={index} results=stale' in case.logs('processor')
        assert all(f'stream={i} results=available' in case.logs('processor') for i in range(1,5) if i!=index)
        case.checkpoint('degraded')
    if node=='processor':assert 'results=stale' in case.logs('detector')
    if node=='recorder':case.capture(death_epoch)
    write(case.root/'failure.json',{'node':node,'before':before,'samples':samples,'after':{'id':after['Id'],'running':False,'restart_count':after['RestartCount']}})
    case.stop()
    case.started=True;case.command('up','--no-open','--json');recovered=case.sample(5);progress(recovered,NODES)
    assert all(recovered[0]['nodes'][n]['id']!=before[n]['id'] for n in NODES)
    write(case.root/'recovery.json',recovered)


def main():
    p=argparse.ArgumentParser();p.add_argument('--cli',type=Path,required=True);p.add_argument('--images',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--workspace',type=Path,required=True)
    p.add_argument('--case',choices=['baseline','radio1','detector','processor','recorder'],required=True)
    p.add_argument('--target',choices=['lima','native-linux'],default='lima')
    p.add_argument('--seconds',type=int,default=180);p.add_argument('--run',action='store_true');p.add_argument('--allow-privileged',action='store_true');p.add_argument('--browser',action='store_true')
    args=p.parse_args()
    if args.run and (not args.allow_privileged or sys.platform!='linux' or os.geteuid()!=0):
        p.error('run requires Linux root and explicit --allow-privileged authorization')
    if not 30<=args.seconds<=600:p.error('seconds must be 30..600')
    args.output=args.output.resolve();args.workspace=args.workspace.resolve();args.cli=args.cli.resolve();args.images=args.images.resolve()
    sys.path.insert(0,str(SOURCE/'scripts/release'))
    from image_release import verify
    manifest=verify(args.images);assert manifest['qualification_hooks'] is False and 'vita' in manifest['images']
    if args.run and (args.output/args.case/'result.json').exists():
        p.error('case already attempted; choose a fresh output identity')
    args.output.mkdir(exist_ok=True,parents=True)
    import hashlib
    write(args.output/'inputs.json',{'target':args.target,'image_catalog_sha256':manifest['catalog_sha256'],
        'qualification_hooks':manifest['qualification_hooks'],
        'harness_sha256':{name:hashlib.sha256((SOURCE/'tests'/name).read_bytes()).hexdigest()
            for name in ('test_vita_acceptance.py','vita_acceptance_wire.py','test_vita_live.py','vita_live_support.py')}})
    case=Case(args,args.case)
    if not args.run:
        print('Prepared',case.generation);return
    # A separately labelled unprivileged workload must survive every graph operation.
    call('docker','image','load','-i',args.images/'runtime.oci.tar',timeout=120)
    image=manifest['images']['runtime']['inspection']
    identity=next(k for k in (image['digest'],image['config_digest']) if call('docker','image','inspect',k,ok=False).returncode==0)
    cookie=uuid.uuid4().hex
    sentinel=call('docker','run','-d','--network','none','--read-only','--cap-drop','ALL',
        '--security-opt','no-new-privileges:true','--user','65532:65532','--memory','16m','--pids-limit','8',
        '--label','graphx.p6.sentinel='+cookie,'--entrypoint','/bin/sleep',identity,'900').stdout.strip()
    before=inventory();error=None
    write(case.root/'result.json',{'case':args.case,'passed':False,'state':'running'})
    try:
        if args.case=='baseline':baseline(case)
        else:failure(case,args.case)
    except BaseException as e:
        error=repr(e);raise
    finally:
        cleanup_error=None;sentinel_preserved=False;after=None
        try:
            case.stop();after=inventory();preserved(before,after)
        except BaseException as e:cleanup_error=repr(e)
        try:
            owned=json.loads(call('docker','inspect',sentinel).stdout)[0]
            assert owned['Id']==sentinel and owned['Config']['Labels']['graphx.p6.sentinel']==cookie
            sentinel_preserved=owned['State']['Running']
            call('docker','rm','--force',sentinel)
            assert sentinel_preserved,'unrelated sentinel stopped'
        except BaseException as e:cleanup_error=cleanup_error or repr(e)
        write(case.root/'result.json',{'case':args.case,'passed':error is None and cleanup_error is None,
              'error':error,'cleanup_error':cleanup_error,'sentinel_preserved':sentinel_preserved,'before':before,'after':after})
        if cleanup_error:raise AssertionError(cleanup_error)
    print('P6 case passed:',args.case)


if __name__=='__main__':
    def interrupted(signum,frame): raise KeyboardInterrupt('bounded acceptance interrupted')
    signal.signal(signal.SIGTERM,interrupted)
    main()
