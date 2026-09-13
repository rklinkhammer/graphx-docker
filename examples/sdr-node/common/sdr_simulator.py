#!/usr/bin/env python3
"""Deterministic UDP SDR source with a mutually authenticated TLS control port."""

from __future__ import annotations

import json
import os
import signal
import socket
import ssl
import threading
import time

from protocol import capture_bytes, configure_telemetry, encode_samples, publish_heartbeat, recv_line
from credential_files import tls_context
from node_settings import arguments, binding, release

stop = threading.Event()
state_lock = threading.Lock()
state = {"running": True, "frequency_hz": 100_000_000}
MAX_CONTROL_BYTES = 4096


def control_server(listener: socket.socket | None = None,
                   ready: threading.Event | None = None) -> None:
    files = {key: os.environ[key] for key in ('SDR_TLS_CERT', 'SDR_TLS_KEY', 'SDR_TLS_CLIENT_CA', 'GRAPHX_CREDENTIALS') if key in os.environ}
    context = tls_context(True, files)
    if listener is None:
        listener = socket.socket()
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        raise ValueError("a pre-bound control listener is required")
        listener.listen(8)
    with listener:
        listener.settimeout(0.5)
        if ready is not None:
            ready.set()
        while not stop.is_set():
            try:
                connection, _ = listener.accept()
            except TimeoutError:
                continue
            with connection:
                connection.settimeout(2)
                try:
                    context = tls_context(True, files)
                    with context.wrap_socket(connection, server_side=True) as secure:
                        secure.settimeout(2)
                        request = recv_line(secure, MAX_CONTROL_BYTES)
                        capture_bytes(request, 'receive control RawSdrControl')
                        response = apply_command(json.loads(request))
                        secure.sendall(json.dumps(response, separators=(",", ":")).encode() + b"\n")
                except (OSError, ssl.SSLError, json.JSONDecodeError, TypeError, ValueError) as error:
                    print(f"control rejected: {type(error).__name__}: {str(error)[:160]}", flush=True)


def apply_command(command: object) -> dict[str, object]:
    if not isinstance(command, dict):
        return {"accepted": False, "error": "invalid command"}
    action = command.get("action")
    expected_fields = {"action", "frequency_hz"} if action == "tune" else {"action"}
    if set(command) != expected_fields:
        return {"accepted": False, "error": "invalid command"}
    with state_lock:
        if action == "start":
            state["running"] = True
        elif action == "stop":
            state["running"] = False
        elif action == "tune":
            frequency = command.get("frequency_hz")
            if type(frequency) is not int or not 1_000_000 <= frequency <= 6_000_000_000:
                return {"accepted": False, "error": "frequency is out of range"}
            state["frequency_hz"] = frequency
        elif action != "status":
            return {"accepted": False, "error": "unsupported action"}
        return {"accepted": True, **state}


def main() -> None:
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    args, node = arguments('sdr.simulator')
    configure_telemetry(node)
    samples = binding(node, 'samples')
    control = binding(node, 'control')
    target, port = samples['settings']['destination'], samples['settings']['port']
    interval = 0.2
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((control['settings']['bind'], control['settings']['port']))
    listener.listen(8)
    ready = threading.Event()
    threading.Thread(target=control_server, args=(listener, ready), daemon=True).start()
    deadline = time.monotonic() + node['readiness']['timeout_ms'] / 1000
    while not ready.wait(0.02):
        if stop.is_set() or time.monotonic() >= deadline:
            listener.close()
            raise TimeoutError('control listener initialization failed')
    sequence = 0
    last_heartbeat = 0.0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as output:
        output.bind((samples['source_address'], 0))
        if not release(args, node, stop):
            return
        print(f"SDR simulator ready; UDP target {target}:{port}, TLS control enabled", flush=True)
        while not stop.wait(interval):
            now = time.monotonic()
            if now - last_heartbeat >= 1:
                publish_heartbeat(node["node_id"], sequence)
                last_heartbeat = now
            with state_lock:
                running, frequency = state["running"], state["frequency_hz"]
            if running:
                payload = encode_samples(sequence, frequency)
                output.sendto(payload, (target, port))
                capture_bytes(payload, 'send samples RawSdrIqFrame')
                sequence = (sequence + 1) & 0xFFFFFFFF


if __name__ == "__main__":
    main()
