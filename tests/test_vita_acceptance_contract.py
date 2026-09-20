#!/usr/bin/env python3
"""P6 observer rejection boundaries without Docker or network privileges."""
import socket
import struct
from vita_acceptance_wire import iq,spectrum,Measurements


def rejected(fn):
    try: fn()
    except (ValueError,struct.error): return
    raise AssertionError('malformed wire accepted')

# The timestamp consists of a 32-bit seconds and a 64-bit fractional word.
p=struct.pack('!II8sIQhhI',0x1c600009,1,bytes.fromhex('00ffffff00000000'),100,0,8192,0,0xc00000)
assert iq(p)['count']==1
for offset,value in [(0,0),(7,0),(8,1),(35,1)]:
 b=bytearray(p);b[offset]=value;rejected(lambda:iq(b))
rejected(lambda:iq(p[:-1]))
b=bytearray(404)
struct.pack_into('!4sHHIIIII',b,0,b'GXP2',1,128,404,1,0,64,64)
struct.pack_into('!QQQQQQ',b,32,100000000,1000000,100,0,100,64000000)
struct.pack_into('!II',b,80,64,0)
struct.pack_into('!QQQ',b,96,100,0,0)
struct.pack_into('!f',b,148+35*4,1)
assert spectrum(b)['valid']==64
for offset,value in [(8,1),(12,1),(80,1),(104,1),(128,1)]:
 wrong=bytearray(b);wrong[offset]=value;rejected(lambda:spectrum(wrong))
missing=bytearray(b);struct.pack_into('!II',missing,80,63,1);missing[128]=128
assert spectrum(missing)['missing']==1

def frame(payload,port,src,dst):
 udp=struct.pack('!HHHH',40000,port,len(payload)+8,0)+payload
 ip=struct.pack('!BBHHHBBH4s4s',0x45,0,len(udp)+20,0,0,64,17,0,socket.inet_aton(src),socket.inet_aton(dst))+udp
 return b'\0'*12+b'\x08\x00'+ip
m=Measurements();m.observe(frame(b,18600,'10.79.0.14','10.79.0.15'))
assert m.report()['spectra'][1]['frequency_checks']==1
m.phase='fault';m.observe(frame(missing,18600,'10.79.0.14','10.79.0.15'))
assert m.phase_counts['fault']['padded']==1
rejected(lambda:m.observe(frame(b,18600,'172.18.0.3','10.79.0.15')))
print('P6 independent wire, quality, timestamp and path rejection checks passed')

# Authorization rejection happens before reading artifacts or creating run state.
import subprocess
import sys
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory);output=root/'absent'
    result=subprocess.run([sys.executable,str(Path(__file__).with_name('test_vita_acceptance.py')),
        '--cli',str(root/'missing-cli'),'--images',str(root/'missing-images'),'--output',str(output),
        '--workspace',str(root/'workspace'),'--case','baseline','--run'],capture_output=True,text=True)
    assert result.returncode!=0 and 'explicit --allow-privileged' in result.stderr and not output.exists()
print('P6 run authorization fails closed before artifact or infrastructure access')

# An independently authored first IQ packet exercises phase and burst validation.
import math
payload=bytearray(4096)
struct.pack_into('!hhhh',payload,0,8192,0,round(8191.75*math.cos(math.pi/10)),round(8191.75*math.sin(math.pi/10)))
wire=struct.pack('!II8sIQ',0x1c600408,1,bytes.fromhex('00ffffff00000000'),100,0)+payload+struct.pack('!I',0xc00400)
m=Measurements();m.observe(frame(b,18600,'10.79.0.14','10.79.0.15'));m.observe(frame(wire,18501,'10.79.0.10','10.79.0.14'))
assert m.streams[1]['signal_checks']==1
wrong=bytearray(wire);struct.pack_into('!h',wrong,28,7000)
bad=Measurements();bad.observe(frame(b,18600,'10.79.0.14','10.79.0.15'))
rejected(lambda:bad.observe(frame(wrong,18501,'10.79.0.10','10.79.0.14')))
print('P6 IQ phase and full-burst independent vector passed')

# Configuration can emit context before arming. The context immediately before
# first IQ, not the earliest context or later burst context, identifies its epoch.
def context(seconds):
    return struct.pack('!IIIQ',0x4000000d,1,seconds,0)+bytes(32)
m=Measurements()
m.observe(frame(context(99),18501,'10.79.0.10','10.79.0.14'))
m.observe(frame(context(100),18501,'10.79.0.10','10.79.0.14'))
m.observe(frame(wire,18501,'10.79.0.10','10.79.0.14'))
m.observe(frame(context(101),18501,'10.79.0.10','10.79.0.14'))
assert m.first_iq[1]['context']==100*10**12 and m.contexts[1]==101*10**12
print('P6 first-IQ context correlation passed')
