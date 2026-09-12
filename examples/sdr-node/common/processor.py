#!/usr/bin/env python3
"""Bounded SDR processor, result producer, and authenticated SDR controller."""

from __future__ import annotations

import ipaddress
import json
import os
import signal
import socket
import ssl
import threading
import time

from protocol import decode_samples, recv_line, signed, telemetry_endpoint, verified

stop = threading.Event()


def expected_sample_source(value: str) -> str:
    """Return one canonical IPv4 source or reject an ambiguous endpoint policy."""
    try:
        address = ipaddress.ip_address(value)
    except ValueError as error:
        raise ValueError("SDR_SAMPLE_SOURCE must be one IPv4 address") from error
    if address.version != 4:
        raise ValueError("SDR_SAMPLE_SOURCE must be one IPv4 address")
    return str(address)


def decode_sample_datagram(payload: bytes, peer: tuple[str, int], source: str):
    """Enforce the configured device address before parsing ordinary UDP bytes."""
    if peer[0] != source:
        raise ValueError("SDR sample source does not match SDR_SAMPLE_SOURCE")
    return decode_samples(payload)


def telemetry_control() -> None:
    """Register this controller with telemetry and relay GUI control to the SDR."""
    host, port = telemetry_endpoint()
    secret = os.environ.get("GRAPHX_TELEMETRY_SHARED_SECRET", "")
    sequence = 0
    last_heartbeat = 0.0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        endpoint.bind(("0.0.0.0", 0))
        endpoint.settimeout(0.2)
        while not stop.is_set():
            now = time.monotonic()
            if now - last_heartbeat >= 1:
                sequence += 1
                event = {"kind": "trace", "event": "heartbeat", "nodeId": "processor",
                         "timestamp": int(time.time() * 1000), "sequence": sequence}
                try:
                    endpoint.sendto(signed(event, secret), (host, port))
                except OSError:
                    pass
                last_heartbeat = now
            try:
                packet, peer = endpoint.recvfrom(16_384)
            except TimeoutError:
                continue
            command = verified(packet, secret)
            if not command or command.get("kind") != "control" or \
                    command.get("targetNode") != "processor":
                continue
            action = command.get("action")
            accepted = action in ("pause", "resume") and \
                int(command.get("expiresAt", 0)) >= int(time.time() * 1000)
            state = "unknown"
            if accepted:
                try:
                    if action == "pause":
                        control("stop")
                        state = "paused"
                    elif action == "resume":
                        control("start")
                        state = "running"
                except (OSError, ssl.SSLError, RuntimeError, ValueError):
                    accepted = False
            acknowledgement = {"kind": "control_ack", "nodeId": "processor",
                               "action": action, "accepted": accepted,
                               "commandId": command.get("commandId"), "state": state}
            try:
                endpoint.sendto(signed(acknowledgement, secret), peer)
            except OSError:
                pass


def control(action: str, frequency_hz: int | None = None) -> dict[str, object]:
    context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH,
                                         cafile=os.environ["SDR_TLS_CA"])
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    context.load_cert_chain(os.environ["SDR_TLS_CERT"], os.environ["SDR_TLS_KEY"])
    request = {"action": action}
    if frequency_hz is not None:
        request["frequency_hz"] = frequency_hz
    with socket.create_connection((os.environ.get("SDR_CONTROL_TARGET", "sdr-simulator"),
                                   int(os.environ.get("SDR_CONTROL_PORT", "18401"))), timeout=2) as raw:
        with context.wrap_socket(raw, server_hostname=os.environ.get("SDR_TLS_SERVER_NAME",
                                                                     "sdr-simulator")) as secure:
            secure.sendall(json.dumps(request, separators=(",", ":")).encode() + b"\n")
            response = recv_line(secure, 4096)
    value = json.loads(response)
    if not value.get("accepted"):
        raise RuntimeError(f"SDR control rejected: {value.get('error', 'unknown error')}")
    return value


def send_result(result: dict[str, object]) -> None:
    payload = json.dumps(result, separators=(",", ":")).encode() + b"\n"
    if len(payload) > 4096:
        raise RuntimeError("result exceeds 4096 bytes")
    with socket.create_connection((os.environ.get("SDR_RESULT_TARGET", "sink"),
                                   int(os.environ.get("SDR_RESULT_PORT", "18402"))), timeout=2) as output:
        output.sendall(payload)


def main() -> None:
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    bind = os.environ.get("SDR_SAMPLE_BIND", "0.0.0.0")
    port = int(os.environ.get("SDR_SAMPLE_PORT", "18400"))
    sample_source = expected_sample_source(os.environ["SDR_SAMPLE_SOURCE"])
    for _ in range(30):
        try:
            print(f"SDR control status: {control('status')}", flush=True)
            break
        except (OSError, ssl.SSLError, RuntimeError, json.JSONDecodeError):
            if stop.wait(0.5):
                return
    else:
        raise RuntimeError("SDR TLS control did not become ready")
    threading.Thread(target=telemetry_control, daemon=True).start()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as source:
        source.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        source.bind((bind, port))
        source.settimeout(0.5)
        accepted = 0
        print(f"processor ready on UDP {bind}:{port}", flush=True)
        while not stop.is_set():
            try:
                payload, peer = source.recvfrom(2048)
                sequence, frequency, samples = decode_sample_datagram(payload, peer, sample_source)
                power = sum(i * i + q * q for i, q in samples)
                send_result({"sequence": sequence, "frequency_hz": frequency,
                             "sample_count": len(samples), "power": power})
                accepted += 1
                if accepted % 10 == 0:
                    print(f"processed sequence={sequence} from={peer[0]} power={power}", flush=True)
            except TimeoutError:
                pass
            except (ValueError, OSError, json.JSONDecodeError) as error:
                print(f"processor rejected input: {type(error).__name__}", flush=True)


if __name__ == "__main__":
    main()
