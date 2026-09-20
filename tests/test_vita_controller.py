#!/usr/bin/env python3
"""Exercise the GraphX library controller against the real radio over mTLS."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
from test_vita_radio import BUILD, certificates, fixture, port

with tempfile.TemporaryDirectory(prefix='graphx-vita-controller-') as directory:
    root=Path(directory).resolve()
    credentials=root/'credentials'; credentials.mkdir(); certificates(credentials)
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
        udp.bind(('127.0.0.1',0))
        tcp=port(); node=fixture(root,1,udp.getsockname()[1],tcp)
        release=root/'release'; token='b'*64; release.write_text(token)
        with (root/'radio.log').open('w') as log:
            app=subprocess.Popen([str(BUILD/'graphx-vita-radio'),'--node','radio','--config',str(node),
                '--release-file',str(release),'--release-token',token],stdout=log,stderr=log,
                env=dict(os.environ,GRAPHX_CREDENTIALS=str(credentials)))
            try:
                deadline=time.monotonic()+5
                while 'ready node=radio' not in (root/'radio.log').read_text():
                    assert app.poll() is None,(root/'radio.log').read_text()
                    assert time.monotonic()<deadline
                    time.sleep(.01)
                result=subprocess.run([str(BUILD/'graphx-vita-controller-test'),str(tcp),'1',str(credentials)],
                                      capture_output=True,text=True,timeout=15)
                assert result.returncode==0,(result.stdout,result.stderr,(root/'radio.log').read_text())
                print(result.stdout,end='')
            finally:
                app.terminate()
                try: app.wait(timeout=3)
                except subprocess.TimeoutExpired: app.kill();app.wait();raise
            assert app.returncode==0,(root/'radio.log').read_text()
