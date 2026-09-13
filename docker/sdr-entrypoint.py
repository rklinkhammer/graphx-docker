#!/usr/bin/env python3
"""Fixed installed names for the shared SDR applications; no topology dispatch."""

import os
import runpy
import sys

SCRIPTS = {
    "graphx-sdr-radio": "sdr_simulator.py",
    "graphx-sdr-processor": "processor.py",
    "graphx-sdr-sink": "sink.py",
}

name = os.path.basename(sys.argv[0])
if name not in SCRIPTS:
    print("GraphX SDR applications: " + ", ".join(sorted(SCRIPTS)))
    sys.exit(0 if sys.argv[1:] == ["--help"] else 2)
sys.path.insert(0, "/opt/graphx-sdr/common")
runpy.run_path("/opt/graphx-sdr/common/" + SCRIPTS[name], run_name="__main__")
