#!/usr/bin/env python3
"""Invoke one mutually authenticated SDR control command."""

from __future__ import annotations

import argparse
import json

from processor import control

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("action", choices=("status", "start", "stop", "tune"))
parser.add_argument("frequency_hz", nargs="?", type=int)
args = parser.parse_args()
if (args.action == "tune") != (args.frequency_hz is not None):
    parser.error("tune requires frequency_hz; other actions do not accept it")
print(json.dumps(control(args.action, args.frequency_hz), indent=2, sort_keys=True))
