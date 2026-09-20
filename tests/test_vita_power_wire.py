#!/usr/bin/env python3
"""Independent byte/size/frequency oracle for the production power encoder."""
from pathlib import Path
import math
import struct
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='graphx-power-wire-') as directory:
    root=Path(directory)
    subprocess.run([str(Path(sys.argv[1])/'graphx-vita-processing-test'),str(root)],check=True)
    for n in (64,128,256,512,1024,2048):
        wire=(root/f'{n}.bin').read_bytes()
        assert wire[:8]==b'GXP2\x00\x01\x00\x80'
        length,sid,seq,size,hop=struct.unpack_from('!5I',wire,8)
        assert (length,sid,seq,size,hop)==(132+17*n//4,2,0,n,n)
        assert len(wire)==length and length+28<=9000
        assert wire[28:32]==b'\x00\x01\x01\x00'
        center,rate,seconds,ps,endseconds,endps=struct.unpack_from('!6Q',wire,32)
        assert (center,rate)==(100000000,1000003)
        assert seconds*10**12+ps==1000*10**12+123+137*10**12//rate
        assert endseconds*10**12+endps==1000*10**12+123+(137+n)*10**12//rate
        assert struct.unpack_from('!IIQQQQQ',wire,80)==(n,0,0,1000,123,137,0)
        assert wire[128:132+n//4]==bytes(4+n//4)
        bins=struct.unpack_from(f'!{n}f',wire,132+n//4)
        peak=max(range(n),key=bins.__getitem__)
        assert peak==n//2+7 and abs(bins[peak]-.0625)<1e-7
        assert all(math.isfinite(p) and p>=0 for p in bins)
        print(f'N={n}: UDP={length} IPv4={length+28} peak={peak} power={bins[peak]}')
