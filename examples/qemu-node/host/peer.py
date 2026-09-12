#!/usr/bin/env python3
"""Shared raw TCP/UDP receiver and connectivity probe for the QEMU TAP lab."""

from __future__ import annotations

import argparse
import os
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
    mode.add_argument("--probe", action="store_true")
    parser.add_argument("--bind", default=os.environ.get("GRAPHX_PEER_BIND", "127.0.0.1"))
    parser.add_argument("--target", default=os.environ.get("GRAPHX_PEER_TARGET", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=env_port("GRAPHX_PEER_PORT", GUEST_FORWARD_PORT))
    parser.add_argument("--receiver-port", type=int, default=env_port("GRAPHX_RECEIVER_PORT", HOST_SERVICE_PORT))
    parser.add_argument("--attempts", type=int, default=15)
    args = parser.parse_args()
    if args.attempts < 1 or args.attempts > 300:
        parser.error("--attempts must be from 1 through 300")
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    if args.receiver:
        receiver(args.bind, args.receiver_port)
    else:
        probe(args.target, args.port, args.attempts)


if __name__ == "__main__":
    main()
