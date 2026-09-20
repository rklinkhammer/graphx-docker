"""Independent bounded P6 wire observations; no production codec or launcher."""
import math
import socket
import struct
import time
from vita_live_support import ethernet_record

PS = 10**12


def spectrum(packet):
    if len(packet) < 132 or packet[:8] != bytes.fromhex('4758503200010080'):
        raise ValueError('spectrum header')
    size, sid, sequence, n, hop = struct.unpack_from('!IIIII', packet, 8)
    if size != len(packet) or sid not in range(1, 5) or n not in (64,128,256,512,1024,2048):
        raise ValueError('spectrum identity/size')
    if size != 132+n//4+4*n or hop not in (n,n//2,n//4):
        raise ValueError('spectrum bounds')
    center, rate, sec, ps, endsec, endps = struct.unpack_from('!QQQQQQ', packet, 32)
    valid, missing = struct.unpack_from('!II', packet, 80)
    epochsec, epochps, ordinal = struct.unpack_from('!QQQ', packet, 96)
    epoch = epochsec*PS+epochps
    if not 1000 <= rate <= 2000000 or max(ps,endps,epochps) >= PS:
        raise ValueError('spectrum time/rate')
    if sec*PS+ps != epoch+ordinal*PS//rate or endsec*PS+endps != epoch+(ordinal+n)*PS//rate:
        raise ValueError('spectrum timeline')
    gaps = packet[128:128+n//8]
    if valid+missing != n or sum(b.bit_count() for b in gaps) != missing:
        raise ValueError('spectrum quality')
    return dict(stream=sid,sequence=sequence,size=n,rate=rate,center=center,epoch=epoch,
                ordinal=ordinal,valid=valid,missing=missing)


def iq(packet):
    if len(packet) < 36 or len(packet)>4128 or len(packet)%4:
        raise ValueError('IQ size')
    header, sid = struct.unpack_from('!II',packet)
    if header>>28 != 1 or header & 0x0ff00000 != 0x0c600000 or (header&65535)*4 != len(packet):
        raise ValueError('IQ header')
    if sid not in range(1,5) or packet[8:16] != bytes.fromhex('00ffffff00000000'):
        raise ValueError('IQ identity')
    sec, ps = struct.unpack_from('!IQ',packet,16)
    trailer = struct.unpack_from('!I',packet,len(packet)-4)[0]
    if ps>=PS or trailer & ~0xc00 != 0x00c00000:
        raise ValueError('IQ timestamp/trailer')
    return dict(stream=sid,time=sec*PS+ps,count=(len(packet)-32)//4,
                frame=(trailer>>10)&3,sequence=(header>>16)&15)


class Measurements:
    def __init__(self):
        self.started=time.monotonic(); self.frames=0; self.fragments=0; self.errors=0
        self.max_iq=0; self.max_spectrum=0; self.streams={}
        self.first_iq={}; self.epochs={}; self.contexts={}; self.tcp=0; self.arp=0
        self.spectra={}; self.phase='baseline'; self.phase_counts={}; self.control_peers=set()

    def observe(self, frame, arrived=None):
        self.frames+=1; arrived=time.time() if arrived is None else arrived
        record=ethernet_record(frame)
        self.arp+=record['ether_type']==0x806
        self.tcp+=record.get('protocol')==6
        if record.get('protocol')==6:
            peers={record['source'],record['destination']}
            if '10.79.0.14' not in peers or len(peers)!=2:
                raise ValueError('control path bypass')
            radio=next(x for x in peers if x!='10.79.0.14')
            if radio not in {f'10.79.0.{i}' for i in range(10,14)}:
                raise ValueError('unknown control peer')
            self.control_peers.add(radio)
        if record.get('fragment'):
            self.fragments+=1; return
        p=record.get('payload',b'');port=record.get('port',0)
        if 18501<=port<=18504 and p:
            sid=port-18500
            if record['source'] != f'10.79.0.{9+sid}' or record['destination']!='10.79.0.14':
                raise ValueError('IQ path bypass')
            if p[0]>>4==4:
                if len(p)!=52 or struct.unpack_from('!I',p,4)[0]!=sid:
                    raise ValueError('context identity/length')
                sec,ps=struct.unpack_from('!IQ',p,8)
                self.contexts[sid]=sec*PS+ps;return
            d=iq(p)
            if d['stream']!=sid: raise ValueError('cross-stream IQ')
            self.max_iq=max(self.max_iq,len(p))
            self.first_iq.setdefault(sid,{**d,'arrival':arrived,'context':self.contexts.get(sid)})
            s=self.streams.setdefault(sid,dict(packets=0,samples=0,reordered=0,sequence_gaps=0,first=d['time'],last=d['time'],last_count=0,signal_checks=0))
            if s['packets']:
                s['reordered']+=d['time']<s['last']
                s['sequence_gaps']+=(d['sequence']-s['sequence']-1)%16
            s.update(last=d['time'],last_count=d['count'],sequence=d['sequence'])
            s['packets']+=1;s['samples']+=d['count']
            if sid in self.epochs:
                epoch,rate=self.epochs[sid];delta=d['time']-epoch
                ordinal=(delta*rate+PS-1)//PS
                if delta<0 or epoch+ordinal*PS//rate!=d['time']:
                    raise ValueError('IQ rational timestamp')
                remaining=262144-ordinal%262144
                if d['count']!=min(1024,remaining): raise ValueError('burst length')
                first=ordinal%262144==0;final=d['count']==remaining
                if d['frame']!=(0 if first and final else 1 if first else 3 if final else 2):
                    raise ValueError('burst marker')
                if s['packets']%256==1:
                    values=struct.unpack_from('!'+('hh'*min(2,d['count'])),p,28)
                    for k,(i,q) in enumerate(zip(values[::2],values[1::2])):
                        phase=2*math.pi*(50000*sid/rate)*(ordinal+k)+(sid-1)*math.pi/4
                        if abs(i-round(8191.75*math.cos(phase)))>1 or abs(q-round(8191.75*math.sin(phase)))>1:
                            raise ValueError('IQ signal/phase')
                    s['signal_checks']+=1
        elif port==18600:
            if record['source']!='10.79.0.14' or record['destination']!='10.79.0.15':
                raise ValueError('spectrum path bypass')
            d=spectrum(p);sid=d['stream'];self.max_spectrum=max(self.max_spectrum,len(p))
            prior=self.epochs.setdefault(sid,(d['epoch'],d['rate']))
            if prior!=(d['epoch'],d['rate']): raise ValueError('unexpected stream epoch/rate change')
            s=self.spectra.setdefault(sid,dict(packets=0,padded=0,full=0,frequency_checks=0,max_error_hz=0))
            s['packets']+=1;s['padded']+=d['missing']>0;s['full']+=d['missing']==0
            phase=self.phase_counts.setdefault(self.phase,{'spectra':0,'padded':0})
            phase['spectra']+=1;phase['padded']+=d['missing']>0
            # Sample numerical checks at bounded frequency; metadata is checked on every packet.
            if d['valid']==d['size'] and s['packets']%128==1:
                power=struct.unpack_from('!'+str(d['size'])+'f',p,132+d['size']//4)
                if not all(math.isfinite(x) and x>=0 for x in power): raise ValueError('invalid power')
                peak=max(range(len(power)),key=power.__getitem__)
                frequency=d['center']+(peak-d['size']//2)*d['rate']/d['size']
                error=abs(frequency-(100000000+50000*sid))
                if error>d['rate']/d['size']/2: raise ValueError('frequency error')
                s['frequency_checks']+=1;s['max_error_hz']=max(s['max_error_hz'],error)

    def report(self):
        return dict(seconds=time.monotonic()-self.started,frames=self.frames,fragments=self.fragments,
                    max_iq=self.max_iq,max_spectrum=self.max_spectrum,streams=self.streams,
                    first_iq=self.first_iq,epochs=self.epochs,contexts=self.contexts,spectra=self.spectra,
                    phase_counts=self.phase_counts,control_peers=sorted(self.control_peers),tcp=self.tcp,arp=self.arp)
