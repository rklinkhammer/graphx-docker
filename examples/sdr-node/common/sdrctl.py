#!/usr/bin/env python3
"""Invoke one mutually authenticated SDR control command."""

from __future__ import annotations

import argparse
import json

import processor
from node_settings import binding, load

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--node", required=True)
parser.add_argument("--config", required=True)
parser.add_argument("action", choices=("status", "start", "stop", "tune"))
parser.add_argument("frequency_hz", nargs="?", type=int)
args = parser.parse_args()
if (args.action == "tune") != (args.frequency_hz is not None):
    parser.error("tune requires frequency_hz; other actions do not accept it")
processor.control_binding = binding(load(args.node, args.config, "sdr.processor"), "control")
print(json.dumps(processor.control(args.action, args.frequency_hz), indent=2, sort_keys=True))
