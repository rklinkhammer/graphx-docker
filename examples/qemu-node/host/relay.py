#!/usr/bin/env python3
"""Bounded TCP/UDP relay from the QEMU slirp gateway to a Compose receiver."""

from __future__ import annotations

import argparse
import signal
import socket
import threading

stop = threading.Event()


def relay_tcp(bind: str, port: int, target: str) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((bind, port))
        listener.listen(16)
        listener.settimeout(0.5)
        while not stop.is_set():
            try:
                incoming, _ = listener.accept()
            except TimeoutError:
                continue
            with incoming:
                incoming.settimeout(2)
                try:
                    payload = incoming.recv(65535)
                    if not payload:
                        continue
                    with socket.create_connection((target, port), timeout=2) as outgoing:
                        outgoing.sendall(payload)
                        reply = outgoing.recv(65535)
                    if reply:
                        incoming.sendall(reply)
                except OSError:
                    continue


def relay_udp(bind: str, port: int, target: str) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((bind, port))
        listener.settimeout(0.5)
        while not stop.is_set():
            try:
                payload, peer = listener.recvfrom(65535)
            except TimeoutError:
                continue
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as outgoing:
                    outgoing.settimeout(2)
                    outgoing.sendto(payload, (target, port))
                    reply, _ = outgoing.recvfrom(65535)
                listener.sendto(reply, peer)
            except OSError:
                continue


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=19001)
    parser.add_argument("--target", default="host-receiver")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be from 1 through 65535")
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    threads = [
        threading.Thread(target=relay_tcp, args=(args.bind, args.port, args.target)),
        threading.Thread(target=relay_udp, args=(args.bind, args.port, args.target)),
    ]
    for thread in threads:
        thread.start()
    while not stop.wait(0.5):
        pass
    for thread in threads:
        thread.join()


if __name__ == "__main__":
    main()
