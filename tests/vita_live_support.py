"""Bounded, independently observable P3/P4 fixtures. No commands run on import."""
from __future__ import annotations

import ipaddress
import json
import socket
import struct
import threading
import time
from pathlib import Path

NODES = ['radio1', 'radio2', 'radio3', 'radio4', 'processor', 'detector', 'recorder']


def graph_document(graph_id, subnet, console_port, *, available=False, capture=True):
    network = ipaddress.ip_network(subnet)
    if network.version != 4 or network.prefixlen != 24 or not network.is_private:
        raise ValueError('qualification requires a private IPv4 /24')
    graph = {
        'version': 3, 'catalog': 'catalog/lock.json', 'graph': {'id': graph_id},
        'lifecycle': {'startup': 'available' if available else 'transactional', 'readiness_ms': 5000},
        'platform': {'console': {'port': console_port}, 'capture': {'enabled': capture, 'provider': 'application'}},
        'nodes': {}, 'connections': {},
        'credentials': {n: {'identity': n, 'members': ['ca.pem', 'cert.pem', 'key.pem'],
                            'provider': 'lab-generated'} for n in ('radio', 'controller')},
        'network': {'networks': [{'id': 'data', 'profile': 'ethernet', 'realization': 'ovs', 'subnets': [subnet]}],
                    'switches': [{'id': 'switch', 'kind': 'openvswitch', 'datapath': 'system',
                                  'ports': [{'id': 'capture'}],
                                  'mirror': {'id': 'mirror', 'output_port': 'capture', 'select_all': True}}],
                    'attachments': [], 'edge_paths': {}, 'captures': []}}
    for i in range(1, 5):
        graph['nodes'][f'radio{i}'] = {'type': 'vita.radio', 'execution': {'kind': 'container'},
                                      'credentials': {'control': 'radio'}, 'parameters': {'radio_index': i}}
    # FFT length 2048: 1 MHz / (15625/32 Hz). Largest approved spectrum datagram.
    graph['nodes']['processor'] = {'type': 'vita.processor', 'execution': {'kind': 'container'},
        'credentials': {f'control{i}': 'controller' for i in range(1, 5)},
        'parameters': {f'bin_width_denominator{i}': 32 for i in range(1, 5)}}
    graph['nodes']['detector'] = {'type': 'vita.detector', 'execution': {'kind': 'container'}}
    graph['nodes']['recorder'] = {'type': 'vita.recorder', 'execution': {'kind': 'container'}}
    for i, name in enumerate(NODES[:-1], 10):
        graph['network']['attachments'].append({'id': name + '-data', 'kind': 'container_veth',
            'owner': name, 'network': 'data', 'address': str(network.network_address + i) + '/24',
            'switch': 'switch', 'mtu': 9000})
    graph['network']['attachments'].append({'id': 'mirror', 'kind': 'mirror', 'owner': 'recorder',
        'switch': 'switch', 'port': 'capture', 'delivery': 'container', 'mtu': 9000})
    if capture:
        graph['network']['captures'] = [{'id': 'diagnostic', 'attachment': 'mirror', 'snaplen': 9022,
            'max_file_bytes': 4194304, 'max_files': 2, 'rotation_seconds': 2, 'retention_seconds': 60}]

    def edge(name, start, end, tcp=False):
        a, b = start.split('.')[0], end.split('.')[0]
        graph['connections'][name] = {'from': start, 'to': end, 'transport': 'tcp' if tcp else 'udp',
            'attachments': {'from': a + '-data', 'to': b + '-data'},
            'settings': {'port': 18000 + len(graph['connections']), 'framing': 'none'}}
        if tcp:
            graph['connections'][name]['security'] = {'profile': 'mtls', 'server_name': 'radio'}
        else:
            graph['connections'][name]['settings']['max_datagram_bytes'] = 8836 if name == 'spectra' else 4128
        graph['network']['edge_paths'][name] = [a, 'data', 'switch', b]
    for i in range(1, 5):
        edge(f'control{i}', f'processor.control{i}', f'radio{i}.control', True)
        edge(f'data{i}', f'radio{i}.samples', f'processor.data{i}')
    edge('spectra', 'processor.spectra', 'detector.spectra')
    return graph


def ethernet_record(frame):
    """Independent Ethernet/IP observation only, not an application VITA codec."""
    if len(frame) < 14:
        raise ValueError('short Ethernet frame')
    offset, kind = 14, int.from_bytes(frame[12:14], 'big')
    for _ in range(2):
        if kind not in (0x8100, 0x88a8):
            break
        if len(frame) < offset + 4:
            raise ValueError('short VLAN header')
        kind = int.from_bytes(frame[offset+2:offset+4], 'big'); offset += 4
    record = {'ether_type': kind, 'bytes': len(frame)}
    if kind != 0x0800:
        return record
    ip = frame[offset:]
    if len(ip) < 20 or ip[0] >> 4 != 4:
        raise ValueError('short IPv4 packet')
    header = (ip[0] & 15) * 4
    length = int.from_bytes(ip[2:4], 'big')
    if header < 20 or length < header or len(ip) < length:
        raise ValueError('truncated IPv4 packet')
    record.update(source=socket.inet_ntoa(ip[12:16]), destination=socket.inet_ntoa(ip[16:20]),
                  protocol=ip[9], fragment=bool(int.from_bytes(ip[6:8], 'big') & 0x3fff), ip_bytes=length)
    if ip[9] == 17 and not record['fragment']:
        if length < header + 8:
            raise ValueError('short UDP packet')
        udp = ip[header:length]; size = int.from_bytes(udp[4:6], 'big')
        if size < 8 or size != len(udp):
            raise ValueError('truncated UDP packet')
        record.update(port=int.from_bytes(udp[2:4], 'big'), payload=udp[8:])
    return record


class FrameObserver:
    """One bounded AF_PACKET queue; retain only counts and explicitly numbered probes."""
    def __init__(self, interface, subnet, cookie):
        self.socket = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
        self.socket.bind((interface, 0)); self.socket.settimeout(.1)
        self.subnet = ipaddress.ip_network(subnet); self.cookie = cookie
        self.stop = threading.Event(); self.lock = threading.Lock()
        self.counts = dict(frames=0, bytes=0, fragments=0, truncated=0, arp=0, tcp=0,
                           data=0, context=0, spectra=0, outside=0, probe=0, max_data_payload=0, max_spectrum_payload=0)
        self.probes = []; self.error = None
        self.thread = threading.Thread(target=self.receive, daemon=True)
        self.thread.start()

    def receive(self):
        try:
            while not self.stop.is_set():
                try:
                    frame = self.socket.recv(9023)
                except socket.timeout:
                    continue
                with self.lock:
                    self.counts['frames'] += 1; self.counts['bytes'] += len(frame)
                    try:
                        r = ethernet_record(frame)
                    except ValueError:
                        self.counts['truncated'] += 1; continue
                    if self.cookie in frame[14:]:
                        if len(self.probes) >= 128: raise ValueError('probe observation bound exceeded')
                        self.probes.append(frame); self.counts['probe'] += 1
                    if r['ether_type'] == 0x0806: self.counts['arp'] += 1
                    if 'source' not in r: continue
                    if ipaddress.ip_address(r['source']) not in self.subnet: self.counts['outside'] += 1
                    self.counts['fragments'] += r['fragment']
                    self.counts['tcp'] += r['protocol'] == 6
                    payload = r.get('payload', b'')
                    if r.get('port') in (18001, 18003, 18005, 18007) and payload:
                        self.counts['data'] += payload[0] >> 4 == 1
                        if payload[0] >> 4 == 1: self.counts['max_data_payload'] = max(self.counts['max_data_payload'], len(payload))
                        self.counts['context'] += payload[0] >> 4 == 4
                    if r.get('port') == 18008:
                        self.counts['spectra'] += 1
                        self.counts['max_spectrum_payload'] = max(self.counts['max_spectrum_payload'], len(payload))
        except Exception as error:
            self.error = error

    def snapshot(self):
        with self.lock:
            if self.error: raise self.error
            return dict(self.counts)

    def close(self):
        self.stop.set(); self.thread.join(1); self.socket.close()
        if self.thread.is_alive(): raise RuntimeError('observer shutdown deadline')
        return self.snapshot()


def assert_progress(before, after, keys):
    for key in keys:
        if after.get(key, 0) <= before.get(key, 0):
            raise AssertionError(f'no fresh {key} progress: {before} -> {after}')


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')
