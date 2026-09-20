#!/usr/bin/env python3
"""Run production host transport against bounded authenticated TLS BIO peers."""
from pathlib import Path
import subprocess
import tempfile
from test_vita_radio import BUILD, certificates

with tempfile.TemporaryDirectory(prefix='graphx-vita-transport-') as directory:
    root = Path(directory).resolve()
    certificates(root)
    subprocess.run([str(BUILD/'graphx-vita-transport-test'), str(root)], check=True, timeout=20)
