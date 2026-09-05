#!/usr/bin/env python3
"""Host TCP/UDP echo peer and guest connectivity probe."""

from __future__ import annotations

import argparse
import signal
import socket
import threading
import time

HOST_SERVICE_PORT = 19001
GUEST_FORWARD_PORT = 18001
stop = threading.Event()


def udp_server() -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("0.0.0.0", HOST_SERVICE_PORT))
        server.settimeout(0.5)
        while not stop.is_set():
            try:
                payload, peer = server.recvfrom(65535)
                print(f"udp rx from {peer}: {payload!r}", flush=True)
                server.sendto(payload, peer)
            except TimeoutError:
                pass


def tcp_server() -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("0.0.0.0", HOST_SERVICE_PORT))
        server.listen(8)
        server.settimeout(0.5)
        while not stop.is_set():
            try:
                connection, peer = server.accept()
            except TimeoutError:
                continue
            with connection:
                connection.settimeout(2)
                payload = connection.recv(65535)
                print(f"tcp rx from {peer}: {payload!r}", flush=True)
                if payload:
                    connection.sendall(payload)


def serve() -> None:
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    threads = [threading.Thread(target=udp_server), threading.Thread(target=tcp_server)]
    for thread in threads:
        thread.start()
    print(f"host peer ready on TCP/UDP {HOST_SERVICE_PORT}", flush=True)
    while not stop.wait(0.5):
        pass
    for thread in threads:
        thread.join()


def tcp_probe() -> bool:
    payload = b"host tcp request"
    try:
        with socket.create_connection(("127.0.0.1", GUEST_FORWARD_PORT), timeout=1) as client:
            client.sendall(payload)
            return client.recv(65535) == payload
    except OSError:
        return False


def udp_probe() -> bool:
    payload = b"host udp request"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(1)
        try:
            client.sendto(payload, ("127.0.0.1", GUEST_FORWARD_PORT))
            reply, _ = client.recvfrom(65535)
            return reply == payload
        except OSError:
            return False


def probe(attempts: int) -> None:
    tcp_ok = False
    udp_ok = False
    for _ in range(attempts):
        tcp_ok = tcp_ok or tcp_probe()
        udp_ok = udp_ok or udp_probe()
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
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--serve", action="store_true")
    mode.add_argument("--probe", action="store_true")
    parser.add_argument("--attempts", type=int, default=15)
    args = parser.parse_args()
    if args.attempts < 1 or args.attempts > 300:
        parser.error("--attempts must be from 1 through 300")
    if args.serve:
        serve()
    else:
        probe(args.attempts)


if __name__ == "__main__":
    main()
