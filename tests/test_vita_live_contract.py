#!/usr/bin/env python3
"""Independent offline rejection and fixture tests; never invokes Docker/OVS."""
import copy
import hashlib
import json
import importlib.util
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

from vita_live_support import assert_progress, capture_has_packet_after, ethernet_record, graph_document, write_json
from test_vita_live import Fixture, preserved, recorder_privilege_policy, set_owned_peer_mtu, remove_owned_bridge

source = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()


def rejects(function):
    try: function()
    except (ValueError, AssertionError): return
    raise AssertionError('invalid evidence accepted')


with tempfile.TemporaryDirectory() as temporary:
    capture = Path(temporary) / 'rotating.pcapng'
    capture.write_bytes(b'bounded capture snapshot')
    def decode_snapshot(command, **kwargs):
        assert command[1:3] == ['-r', '-']
        assert kwargs['input'] == b'bounded capture snapshot'
        capture.unlink()  # Rotation after opening cannot invalidate the snapshot.
        return SimpleNamespace(returncode=0, stdout=b'100.25\n')
    with patch('vita_live_support.subprocess.run', side_effect=decode_snapshot):
        assert capture_has_packet_after(capture, 100)
    assert not capture_has_packet_after(capture, 100)
    capture.write_bytes(b'capture')
    for code, output in ((0, b'99.0\n'), (0, b'not a timestamp\n'), (1, b'101.0\n')):
        with patch('vita_live_support.subprocess.run', return_value=SimpleNamespace(returncode=code, stdout=output)):
            assert not capture_has_packet_after(capture, 100)
    capture.write_bytes(b'\0' * (4194304 + 9022 + 4097))
    with patch('vita_live_support.subprocess.run') as decoder:
        rejects(lambda: capture_has_packet_after(capture, 100))
        decoder.assert_not_called()


for capability in ('NET_RAW', 'CAP_NET_RAW'):
    host = {'CapAdd': [capability], 'CapDrop': ['ALL'], 'Privileged': False}
    assert recorder_privilege_policy(host)
    for added in ([], ['NET_ADMIN'], [capability, 'NET_ADMIN'], [capability, capability]):
        assert not recorder_privilege_policy({**host, 'CapAdd': added})
    assert not recorder_privilege_policy({**host, 'Privileged': True})
    assert not recorder_privilege_policy({**host, 'CapDrop': []})

# MTU fault injection must target only the recorded container peer.
fixture = SimpleNamespace(container=lambda name, running: {'Id': 'container', 'State': {'Pid': 123}})
endpoint = {'id': 'data', 'owner': 'radio1', 'target_interface': 'gx0'}
resource = {'container_id': 'container', 'namespace_inode': 45, 'peer_ifindex': 67}
peer = {'ifindex': 67, 'ifalias': 'graphx:token:data:peer'}
for bad in (None, 'container_id', 'namespace_inode', 'peer_ifindex'):
    candidate = {**resource, **({bad: 'foreign'} if bad else {})}
    commands = []
    def peer_call(*command, **kwargs):
        commands.append(command)
        return 0, json.dumps([peer])
    with patch('test_vita_live.Path.stat', return_value=SimpleNamespace(st_ino=45)), patch(
            'test_vita_live.call', side_effect=peer_call):
        action = lambda: set_owned_peer_mtu(fixture, {'owner_token': 'token'}, endpoint, candidate, 1500)
        if bad: rejects(action)
        else: action()
    assert any('set' in command for command in commands) == (bad is None)

# Bridge outage must atomically bind the name-only deletion API to its UUID.
bridge_state = {'resources': [{'kind': 'ovs_bridge', 'uuid': 'owned-uuid', 'name': 'gxb-owned'}],
                'owner_token': 'owner', 'graph_id': 'graph', 'config_sha256': 'hash'}
with patch('test_vita_live.call') as bridge_call:
    remove_owned_bridge(bridge_state)
    bridge_call.assert_called_once_with(
        'ovs-vsctl', '--timeout=2', '--', 'wait-until', 'Bridge', 'owned-uuid',
        'name="gxb-owned"', 'external_ids:graphx_owner=owner', 'external_ids:graphx_graph=graph',
        'external_ids:graphx_config_hash=hash', '--', 'del-br', 'gxb-owned')
with patch('test_vita_live.call', side_effect=AssertionError('ownership mismatch')):
    rejects(lambda: remove_owned_bridge(bridge_state))

# Counter observation must survive the detector's per-spectrum log volume.
fixture = Fixture.__new__(Fixture)
fixture.container = lambda name: {'Id': 'owned-detector'}
lines = ['detector received=100 invalid=0'] + ['detection stream=1 sequence=1'] * 2000
def detector_logs(*command, **kwargs):
    assert command[:3] == ('docker', 'logs', '--tail') and command[-1] == 'owned-detector'
    return 0, '\n'.join(lines[-int(command[3]):])
with patch('test_vita_live.call', side_effect=detector_logs):
    assert fixture.counters('detector') == 100
    lines += ['detector received=2100 invalid=0'] + ['detection stream=1 sequence=2'] * 2000
    assert fixture.counters('detector') == 2100

# Independent byte fixtures include 0/1/2 VLAN tags, truncation and IP fragmentation.
payload = bytes(i % 251 for i in range(8836))
udp = struct.pack('!HHHH', 1000, 19999, len(payload) + 8, 0) + payload
ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(udp), 7, 0x4000, 64, 17, 0,
                 socket.inet_aton('10.79.0.1'), socket.inet_aton('10.79.0.2')) + udp
for vlan in (b'\x08\x00', b'\x81\x00\x00\x01\x08\x00', b'\x88\xa8\x00\x01\x81\x00\x00\x02\x08\x00'):
    frame = b'\xff' * 6 + b'\x02\x00\x00\x00\x00\x01' + vlan + ip
    record = ethernet_record(frame)
    assert record['payload'] == payload and record['ip_bytes'] == 8864 and not record['fragment']
    rejects(lambda: ethernet_record(frame[:-1]))
rejects(lambda: ethernet_record(b''))
rejects(lambda: graph_document('test', '10.0.0.0/8', 10000))
rejects(lambda: assert_progress({'data': 3}, {'data': 3}, ['data']))
assert_progress({'data': 3}, {'data': 4}, ['data'])
before = {'containers': ['a'], 'networks': ['n'], 'bridges': ['b'], 'namespaces': [],
          'links': ['[{"ifindex":1,"ifname":"a"}]'], 'processes': ['1 dumpcap'], 'volumes': ['v']}
preserved(before, before)
for key in before:
    after = copy.deepcopy(before)
    after[key] = ['[{"ifindex":2,"ifname":"a"}]'] if key == 'links' else []
    if key == 'namespaces': after[key] = ['unexpected']
    rejects(lambda: preserved(before, after))
with tempfile.TemporaryDirectory(prefix='graphx-vita-live-contract-') as directory:
    root = Path(directory).resolve(); images = root / 'images'; images.mkdir()
    # PyYAML is a live Linux prerequisite, not a portable preparation dependency.
    # Run this regression in the Lima guest as well as hosts with PyYAML installed.
    if importlib.util.find_spec('yaml'):
        import yaml
        timestamp = '2026-09-20T11:19:00-04:00'
        ledger = root / 'ownership.yml'
        ledger.write_text('resources: []\nprocesses:\n  - kind: volume\n'
                          '    name: graphx-fixture-history\n'
                          f'    secondary_id: {timestamp}\n'
                          '    pid: 0\n    pending: false\n')
        fixture = Fixture.__new__(Fixture); fixture.ledger = ledger
        state = fixture.state_value()
        retained = state['processes']
        assert retained[0]['secondary_id'] == timestamp
        assert retained[0]['pid'] == 0 and retained[0]['pending'] is False
        report = root / 'retained-history-volumes.json'; write_json(report, retained)
        assert json.loads(report.read_text()) == retained
        # The specialized loader must not change PyYAML behavior for other users.
        assert not isinstance(yaml.safe_load(timestamp), str)
        ledger.write_text('!!python/object/apply:os.system ["false"]')
        try: fixture.state_value()
        except yaml.constructor.ConstructorError: pass
        else: raise AssertionError('unsafe ownership YAML accepted')
        print('Ownership timestamps preserve exact identity and serialize safely')
    catalog = images / 'catalog'; shutil.copytree(source / 'config/catalog', catalog)
    schemas = json.loads((catalog / 'wire-schemas.json').read_bytes())
    schemas.update(json.loads((source / 'config/catalog/wire-schemas.json').read_bytes()))
    (catalog / 'wire-schemas.json').write_text(json.dumps(schemas))
    for p in (source / 'config/catalog/types').glob('*.json'): shutil.copy2(p, catalog / 'types' / p.name)
    lock = json.loads((catalog / 'lock.json').read_bytes())
    names = {p['path'] for p in lock['files']} | {'types/' + p.name for p in (source / 'config/catalog/types').glob('*.json')}
    lock['files'] = [{'path': p, 'sha256': hashlib.sha256((catalog / p).read_bytes()).hexdigest()} for p in sorted(names)]
    (catalog / 'lock.json').write_text(json.dumps(lock))
    args = SimpleNamespace(images=images, cli=build / 'graphx', target='lima', subnet='10.79.0.0/24')
    for name, options in [('p3', {}), ('p4', {'available': True}), ('off', {'capture': False}), ('sentinel', {'sentinel': True})]:
        f = Fixture(args, root / name, **options)
        assert f.compiled.is_dir() and not f.ledger.exists()
        if name != 'sentinel':
            assert len(f.resolved['nodes']) == 7
            assert f.resolved['lifecycle']['startup'] == ('available' if name == 'p4' else 'transactional')
            assert all(n['parameters']['bin_width_denominator1'] == 32 for n in f.resolved['nodes'] if n['node_id'] == 'processor')
        # Compiling a fixture never starts a graph or claims live acceptance.
        assert not f.started
    output = root / 'unauthorized'
    result = subprocess.run([sys.executable, str(source / 'tests/test_vita_live.py'), '--run', '--images', str(images), '--output', str(output)], capture_output=True, text=True)
    assert result.returncode != 0 and not output.exists() and 'explicit --allow-privileged' in result.stderr
print('P3/P4 live harness offline fixtures, byte observations, rejection, identity inventories and authorization guards passed')
