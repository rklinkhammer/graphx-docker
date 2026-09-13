#!/usr/bin/env python3
"""Separately authorized S15/T03 actual guest boot acceptance; no VM provisioning."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import pwd
import shutil
import socket
import stat
import struct
import subprocess
import sys
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allow-privileged', action='store_true')
    parser.add_argument('--target', choices=['lima','native-linux'], required=True)
    parser.add_argument('--case', choices=['S15','T03'], required=True)
    parser.add_argument('--release', type=Path, required=True)
    parser.add_argument('--images', type=Path, required=True)
    parser.add_argument('--guests', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not args.allow_privileged or sys.platform != 'linux' or os.geteuid() != 0:
        parser.error('requires separate explicit authorization, --allow-privileged and local Linux root')
    import yaml
    source = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(source/'scripts/release'))
    from guest_release import verify
    verify(args.guests, allow_dirty=True)
    account = pwd.getpwuid(65532)
    assert account.pw_name == 'graphx-qemu' and account.pw_gid == 65532
    cli = args.release.resolve()/'bin/graphx'
    root = args.output.resolve()
    assert root.is_relative_to(Path('/var/lib/graphx')), 'runtime evidence must stay in Linux guest storage'
    root.mkdir(parents=True, exist_ok=False)

    def call(*command, timeout=180, ok=True):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=timeout)
        if ok:
            assert result.returncode == 0, (command, result.stdout, result.stderr)
        return result

    def inventory():
        return {name: call(*command).stdout.splitlines() for name, command in {
            'containers':['docker','ps','-aq','--no-trunc'],
            'networks':['docker','network','ls','--format','{{.ID}}'],
            'volumes':['docker','volume','ls','--format','{{.Name}}'],
            'bridges':['ovs-vsctl','list-br'], 'namespaces':['ip','netns','list'],
            'links':['ip','-o','link','show'], 'processes':['ps','-eo','pid,comm'],
        }.items()}

    call('docker','info')
    call('docker','compose','version')
    call('ovs-vsctl','--timeout=5','show')
    before = inventory()
    (root/'before.json').write_text(json.dumps(before, indent=2)+'\n')
    original = source/('examples/qemu-node/tap/graphx.yml' if args.case == 'S15' else
                       'design/graph-generation/variants/mixed-container-qemu-sdr/graphx.yml')
    authored = yaml.safe_load(original.read_text())
    graph = 'p8-'+uuid.uuid4().hex[:12]
    authored['graph']['id'] = graph
    authored['catalog'] = 'catalog/lock.json'
    with socket.socket() as available:
        available.bind(('127.0.0.1', 0))
        authored.setdefault('platform', {})['console'] = {'port':available.getsockname()[1]}
    workspace = root/'workspace'
    shutil.copytree(args.guests/'catalog', workspace/'catalog')
    config = workspace/'graphx.yml'
    config.write_text(yaml.safe_dump(authored))
    compiled, state = root/'compiled', root/'state'
    call(cli,'compile',config,'--target',args.target,'--catalog-root',workspace/'catalog',
         '--source-root',args.guests/'source','--credential-root',root/'credentials','--output',compiled)
    resolved = json.loads((compiled/'resolved.json').read_text())
    options = ['--output',compiled,'--state-root',state,'--release',args.release,
               '--images',args.images,'--allow-privileged']
    ledger = state/graph/'ownership.yml'
    checks = []
    result = {'result':'incomplete','case':args.case,'target':args.target,
              'host_architecture':platform.machine(),'guest_architecture':'x86_64',
              'accelerator':'tcg','checks':checks}

    def container(node):
        return next(p['stable_id'] for p in yaml.safe_load(ledger.read_text())['processes']
                    if p['kind'] == 'container' and p['name'] == 'graphx-'+graph+'-'+node)

    def logs(node):
        return call('docker','logs','--tail','4096',container(node)).stdout

    completed = False
    try:
        up = call(cli,'run','up',*options,timeout=240,ok=False)
        (root/'up.log').write_text(up.stdout+up.stderr)
        assert up.returncode == 0, up.stderr
        active = yaml.safe_load(ledger.read_text())
        (root/'ready-ownership.yml').write_text(ledger.read_text())
        (root/'status.log').write_text(call(cli,'run','status',*options).stdout)
        guests = [p for p in active['processes'] if p.get('runtime_directory')]
        assert guests, 'no managed guest process recorded'
        for process in guests:
            pid = int(process['stable_id'])
            status = Path(f'/proc/{pid}/status').read_text()
            assert 'Uid:\t65532\t65532\t65532\t65532' in status
            command = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
            assert b'q35,accel=tcg' in command and b'-nodefaults' in command
            assert command.count(b'-netdev') == 1 and not any(b'user,' in arg for arg in command)
            with Qmp(process) as qmp:
                assert qmp.command('query-status')['running'] is True
                assert qmp.command('query-kvm')['enabled'] is False
                deadline = time.monotonic()+20
                serial = ''
                while time.monotonic() < deadline:
                    serial += qmp.command('ringbuf-read', {'device':'serial','size':1048576})
                    assert len(serial.encode()) <= 2*1024*1024, 'serial evidence exceeded bound'
                    if 'guest boot architecture=x86_64' in serial:
                        if args.case == 'T03' or (' tcp bytes=' in serial and ' udp bytes=' in serial):
                            break
                    time.sleep(.2)
                assert 'guest boot architecture=x86_64' in serial, 'no actual guest kernel/agent evidence'
                if args.case == 'S15':
                    assert ' tcp bytes=' in serial and ' udp bytes=' in serial
                (root/(process['name']+'-serial.log')).write_text(serial)
            checks.append('actual x86_64 guest boot, TCG, UID 65532, one TAP, identity-checked local QMP and framed readiness')
        if args.case == 'S15':
            peer = state/graph/'logs/host-peer.log'
            deadline = time.monotonic()+10
            while ' tcp bytes=' not in peer.read_text() or ' udp bytes=' not in peer.read_text():
                assert time.monotonic() < deadline, peer.read_text()
                time.sleep(.1)
            (root/'peer.log').write_text(peer.read_text())
            checks.append('TCP and UDP delivered in both guest/namespace directions')
        else:
            deadline = time.monotonic()+20
            while not all('result ' in logs(name) for name in ('sink-east','sink-west')):
                assert time.monotonic() < deadline, 'SDR results missing'
                time.sleep(.2)
            script = """import sys,json
sys.path.insert(0,'/opt/graphx-sdr/common')
import processor
from node_settings import load,binding
from credential_files import configure_tls
node=load(sys.argv[1],sys.argv[2],'sdr.processor')
configure_tls(node)
processor.control_binding=binding(node,'control')
print(json.dumps(processor.control(sys.argv[3],int(sys.argv[4]) if len(sys.argv)>4 else None)))
"""
            def control(node, action, *arguments):
                return json.loads(call('docker','exec',container(node),'python3','-c',script,node,
                                       '/run/graphx/node.json',action,*arguments).stdout)
            evidence = {'before':control('processor-east','status'),
                        'tune':control('processor-east','tune','101000000'),
                        'after':control('processor-east','status'),
                        'independent':control('processor-west','status')}
            assert evidence['after']['frequency_hz'] == 101000000
            assert evidence['independent']['frequency_hz'] == 100000000
            assert control('processor-east','stop')['running'] is False
            assert control('processor-east','start')['running'] is True
            (root/'sdr-control.json').write_text(json.dumps(evidence,indent=2)+'\n')
            for node in ('processor-east','processor-west','sink-east','sink-west'):
                (root/(node+'.log')).write_text(logs(node))
            checks.append('actual radio guest UDP samples, processor TCP results, mutually authenticated tune/start/stop and independent container radio')
        completed = True
    finally:
        down = call(cli,'run','down',*options,ok=False)
        (root/'down.log').write_text(down.stdout+down.stderr)
        after = inventory()
        (root/'after.json').write_text(json.dumps(after,indent=2)+'\n')
        result['cleanup_returncode'] = down.returncode
        (root/'results.json').write_text(json.dumps(result,indent=2)+'\n')
        assert down.returncode == 0, down.stderr
        for kind in ('containers','networks','bridges','namespaces'):
            assert sorted(before[kind]) == sorted(after[kind]), kind
        links = lambda entries: {line.split(':',2)[1].split('@')[0].strip() for line in entries}
        assert links(before['links']) == links(after['links'])
        processes = lambda entries: {line.strip() for line in entries if any(
            name in line for name in ('qemu-system','graphx','dumpcap'))}
        assert processes(before['processes']) == processes(after['processes'])
        assert set(before['volumes']) <= set(after['volumes'])
        result['result'] = 'pass' if completed else 'incomplete'
        checks.append('existing infrastructure preserved; QEMU, TAP and common runtime resources stopped')
        (root/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('P8 guest acceptance passed:',root)


class Qmp:
    def __init__(self, process):
        self.process = process
        self.fd = -1
        self.socket = socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
        self.socket.settimeout(5)
        self.buffer = b''

    def __enter__(self):
        self.fd = os.open(self.process['runtime_directory'],os.O_DIRECTORY|os.O_NOFOLLOW)
        metadata = os.fstat(self.fd)
        assert f'{metadata.st_dev}:{metadata.st_ino}' == self.process['runtime_identity']
        path = f'/proc/self/fd/{self.fd}/qmp.sock'
        before = os.lstat(path)
        assert stat.S_ISSOCK(before.st_mode) and before.st_uid == 65532 and stat.S_IMODE(before.st_mode) == 0o600
        self.socket.connect(path)
        pid, uid, gid = struct.unpack('3i',self.socket.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))
        assert (pid,uid,gid) == (int(self.process['stable_id']),65532,65532)
        assert os.lstat(path).st_ino == before.st_ino
        assert 'QMP' in self.receive()
        self.command('qmp_capabilities')
        return self

    def __exit__(self,*_):
        self.socket.close()
        os.close(self.fd)

    def receive(self):
        while b'\n' not in self.buffer:
            part = self.socket.recv(8192)
            assert part, 'QMP disconnected'
            self.buffer += part
            assert len(self.buffer) <= 2*1024*1024, 'QMP response exceeded bound'
        line,self.buffer = self.buffer.split(b'\n',1)
        return json.loads(line)

    def command(self,name,arguments=None):
        request = {'execute':name,'id':name}
        if arguments is not None:
            request['arguments'] = arguments
        self.socket.sendall(json.dumps(request).encode()+b'\n')
        for _ in range(32):
            response = self.receive()
            if 'event' in response:
                continue
            assert response.get('id') == name and 'return' in response, response
            return response['return']
        raise AssertionError('excessive QMP events')


if __name__ == '__main__':
    main()
