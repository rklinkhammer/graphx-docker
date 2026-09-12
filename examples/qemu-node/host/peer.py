#!/usr/bin/env python3
"""Shared raw TCP/UDP origin, receiver, probe, and out-of-band GraphX control adapter."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import secrets
import signal
import socket
import threading
import time

HOST_SERVICE_PORT = 19001
GUEST_FORWARD_PORT = 18001
stop = threading.Event()


def env_port(name: str, default: int) -> int:
    value = os.environ.get(name, str(default))
    if not value.isdigit() or not 1 <= int(value) <= 65535:
        raise ValueError(f"{name} must be an integer from 1 through 65535")
    return int(value)


def signed(payload: dict[str, object], secret: str) -> bytes:
    if not secret:
        return json.dumps(payload, separators=(",", ":")).encode()
    timestamp = int(time.time() * 1000)
    nonce = secrets.token_hex(16)
    compact = json.dumps(payload, separators=(",", ":"))
    signature = hmac.new(secret.encode(), f"{timestamp}.{nonce}.{compact}".encode(), hashlib.sha256).hexdigest()
    return json.dumps(
        {"payload": payload, "auth": {"timestamp": timestamp, "nonce": nonce, "signature": signature}},
        separators=(",", ":"),
    ).encode()


def verified(data: bytes, secret: str) -> dict[str, object] | None:
    try:
        value = json.loads(data)
        if not secret:
            return value if isinstance(value, dict) and "auth" not in value else None
        payload, auth = value["payload"], value["auth"]
        timestamp, nonce, supplied = auth["timestamp"], auth["nonce"], auth["signature"]
        if not isinstance(timestamp, int) or abs(int(time.time() * 1000) - timestamp) > 30_000:
            return None
        if not isinstance(nonce, str) or len(nonce) != 32 or not isinstance(supplied, str):
            return None
        compact = json.dumps(payload, separators=(",", ":"))
        expected = hmac.new(secret.encode(), f"{timestamp}.{nonce}.{compact}".encode(), hashlib.sha256).hexdigest()
        return payload if hmac.compare_digest(expected, supplied) else None
    except (KeyError, TypeError, ValueError, json.JSONDecodeError):
        return None


def udp_server(bind: str, port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((bind, port))
        server.settimeout(0.5)
        while not stop.is_set():
            try:
                payload, peer = server.recvfrom(65535)
                print(f"udp rx from {peer}: {payload!r}", flush=True)
                server.sendto(payload, peer)
            except TimeoutError:
                pass


def tcp_server(bind: str, port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((bind, port))
        server.listen(8)
        server.settimeout(0.5)
        while not stop.is_set():
            try:
                connection, peer = server.accept()
            except TimeoutError:
                continue
            with connection:
                connection.settimeout(2)
                try:
                    payload = connection.recv(65535)
                    print(f"tcp rx from {peer}: {payload!r}", flush=True)
                    if payload:
                        connection.sendall(payload)
                except OSError:
                    continue


def receiver(bind: str, port: int) -> None:
    threads = [threading.Thread(target=udp_server, args=(bind, port)),
               threading.Thread(target=tcp_server, args=(bind, port))]
    for thread in threads:
        thread.start()
    print(f"host receiver ready on {bind} TCP/UDP {port}", flush=True)
    while not stop.wait(0.5):
        pass
    for thread in threads:
        thread.join()


def tcp_exchange(host: str, port: int, payload: bytes) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.5) as client:
            client.sendall(payload)
            return client.recv(65535) == payload
    except OSError:
        return False


def udp_exchange(host: str, port: int, payload: bytes) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(0.5)
        try:
            client.sendto(payload, (host, port))
            reply, _ = client.recvfrom(65535)
            return reply == payload
        except OSError:
            return False


def telemetry_event(output: socket.socket, address: tuple[str, int], secret: str,
                    sequence: int) -> None:
    payload = {"kind": "trace", "event": "heartbeat", "nodeId": "host-origin",
               "timestamp": int(time.time() * 1000), "sequence": sequence}
    output.sendto(signed(payload, secret), address)


def origin(target: str, port: int, interval: float) -> None:
    telemetry_host = os.environ.get("GRAPHX_TELEMETRY_HOST", "telemetry")
    telemetry_port = env_port("GRAPHX_TELEMETRY_PORT", 9000)
    secret = os.environ.get("GRAPHX_TELEMETRY_SHARED_SECRET", "")
    if secret and len(secret.encode()) < 32:
        raise ValueError("GRAPHX_TELEMETRY_SHARED_SECRET must contain at least 32 bytes")
    paused = False
    sequence = 1
    last_heartbeat = 0.0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as control:
        control.bind(("0.0.0.0", 0))
        control.settimeout(0.1)
        telemetry = (telemetry_host, telemetry_port)
        print(f"host origin ready; target {target} TCP/UDP {port}", flush=True)
        while not stop.is_set():
            now = time.monotonic()
            if now - last_heartbeat >= 1:
                try:
                    telemetry_event(control, telemetry, secret, sequence)
                except OSError:
                    pass
                last_heartbeat = now
            try:
                command_data, command_peer = control.recvfrom(16_384)
                command = verified(command_data, secret)
                if command and command.get("kind") == "control" and command.get("targetNode") == "host-origin" and command.get("action") in ("pause", "resume") and int(command.get("expiresAt", 0)) >= int(time.time() * 1000):
                    paused = command["action"] == "pause"
                    acknowledgement = {
                        "kind": "control_ack", "nodeId": "host-origin",
                        "action": command["action"], "accepted": True,
                        "commandId": command["commandId"],
                        "state": "paused" if paused else "running",
                    }
                    control.sendto(signed(acknowledgement, secret), command_peer)
            except TimeoutError:
                pass
            if paused:
                continue
            tcp_payload = f"host tcp sequence={sequence}".encode()
            udp_payload = f"host udp sequence={sequence}".encode()
            tcp_ok = tcp_exchange(target, port, tcp_payload)
            udp_ok = udp_exchange(target, port, udp_payload)
            print(f"origin sequence={sequence} tcp={'ok' if tcp_ok else 'failed'} udp={'ok' if udp_ok else 'failed'}", flush=True)
            sequence += 1
            stop.wait(interval)


def probe(host: str, port: int, attempts: int) -> None:
    tcp_ok = False
    udp_ok = False
    for _ in range(attempts):
        tcp_ok = tcp_ok or tcp_exchange(host, port, b"host tcp request")
        udp_ok = udp_ok or udp_exchange(host, port, b"host udp request")
        if tcp_ok and udp_ok:
            break
        time.sleep(1)
    if tcp_ok:
        print("PASS tcp guest echo")
    if udp_ok:
        print("PASS udp guest echo")
    if not (tcp_ok and udp_ok):
        raise SystemExit("guest did not answer both TCP and UDP probes")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--receiver", action="store_true")
    mode.add_argument("--origin", action="store_true")
    mode.add_argument("--probe", action="store_true")
    parser.add_argument("--bind", default=os.environ.get("GRAPHX_PEER_BIND", "127.0.0.1"))
    parser.add_argument("--target", default=os.environ.get("GRAPHX_PEER_TARGET", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=env_port("GRAPHX_PEER_PORT", GUEST_FORWARD_PORT))
    parser.add_argument("--receiver-port", type=int, default=env_port("GRAPHX_RECEIVER_PORT", HOST_SERVICE_PORT))
    parser.add_argument("--attempts", type=int, default=15)
    parser.add_argument("--interval", type=float, default=float(os.environ.get("GRAPHX_ORIGIN_INTERVAL_SECONDS", "1")))
    args = parser.parse_args()
    if args.attempts < 1 or args.attempts > 300:
        parser.error("--attempts must be from 1 through 300")
    if not 0.1 <= args.interval <= 60:
        parser.error("--interval must be from 0.1 through 60")
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    if args.receiver:
        receiver(args.bind, args.receiver_port)
    elif args.origin:
        origin(args.target, args.port, args.interval)
    else:
        probe(args.target, args.port, args.attempts)


if __name__ == "__main__":
    main()
