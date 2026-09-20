#!/usr/bin/env python3
"""Independent offline rejection and fixture tests; never invokes Docker/OVS."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

from vita_live_support import assert_progress, ethernet_record, graph_document
from test_vita_live import Fixture, preserved

source = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()


def rejects(function):
    try: function()
    except (ValueError, AssertionError): return
    raise AssertionError('invalid evidence accepted')


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
    catalog = images / 'catalog'; shutil.copytree(source / 'config/catalog', catalog)
    schemas = json.loads((catalog / 'wire-schemas.json').read_bytes())
    schemas.update(json.loads((source / 'config/vita/wire-schemas.json').read_bytes()))
    (catalog / 'wire-schemas.json').write_text(json.dumps(schemas))
    for p in (source / 'config/vita/types').glob('*.json'): shutil.copy2(p, catalog / 'types' / p.name)
    lock = json.loads((catalog / 'lock.json').read_bytes())
    names = {p['path'] for p in lock['files']} | {'types/' + p.name for p in (source / 'config/vita/types').glob('*.json')}
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
