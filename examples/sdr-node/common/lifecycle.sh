#!/usr/bin/env bash

# Shared lifecycle helpers for the portable and native SDR profiles.

graphx_sdr_preflight_port() {
  python3 - "${1:-8080}" <<'PY'
import os
import socket
import sys
import time

try:
    port = int(sys.argv[1])
except ValueError as caught:
    raise SystemExit(f"GRAPHX_SDR_GUI_PORT must be an integer: {caught}") from None
if not 1 <= port <= 65535:
    raise SystemExit("GRAPHX_SDR_GUI_PORT must be from 1 through 65535")

try:
    attempts = int(os.environ.get("GRAPHX_SDR_PORT_ATTEMPTS", "50"))
except ValueError as caught:
    raise SystemExit(f"GRAPHX_SDR_PORT_ATTEMPTS must be an integer: {caught}") from None
if not 1 <= attempts <= 50:
    raise SystemExit("GRAPHX_SDR_PORT_ATTEMPTS must be from 1 through 50")

last_error = None
for _ in range(attempts):
    with socket.socket() as candidate:
        try:
            candidate.bind(("127.0.0.1", port))
            break
        except OSError as caught:
            last_error = caught
            time.sleep(0.1)
else:
    raise SystemExit(f"SDR GUI loopback port {port} is unavailable: {last_error}")
PY
}
