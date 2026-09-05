#!/usr/bin/env bash
set -euo pipefail
python3 /opt/graphx-qemu/host/peer.py --probe --target 127.0.0.1 --port 18001 --attempts 1 >/dev/null
