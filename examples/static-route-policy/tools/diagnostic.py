#!/usr/bin/env python3
"""Bounded one-way UDP probe used only by the disposable Phase 14 lab."""

from __future__ import annotations

import argparse
import socket
import struct
import sys

MAGIC = b"GXR1"
MAX_TOKEN_BYTES = 128
HEADER = struct.Struct("!4sH")


def payload(token: str) -> bytes:
    encoded = token.encode("ascii")
    if not encoded or len(encoded) > MAX_TOKEN_BYTES:
        raise ValueError("token length must be between 1 and 128 ASCII bytes")
    return HEADER.pack(MAGIC, len(encoded)) + encoded


def parse_packet(data: bytes) -> str:
    if len(data) < HEADER.size:
        raise ValueError("truncated diagnostic datagram")
    magic, length = HEADER.unpack_from(data)
    if magic != MAGIC or length < 1 or length > MAX_TOKEN_BYTES:
        raise ValueError("invalid diagnostic datagram header")
    if len(data) != HEADER.size + length:
        raise ValueError("diagnostic datagram length mismatch")
    return data[HEADER.size:].decode("ascii")


def send(bind: str, destination: str, port: int, token: str) -> int:
    data = payload(token)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        endpoint.bind((bind, 0))
        endpoint.settimeout(1.0)
        endpoint.sendto(data, (destination, port))
    print(f"sent token={token} bytes={len(data)} destination={destination}:{port}")
    return 0


def listen(bind: str, port: int, token: str, timeout: float) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        endpoint.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        endpoint.bind((bind, port))
        endpoint.settimeout(timeout)
        try:
            data, peer = endpoint.recvfrom(HEADER.size + MAX_TOKEN_BYTES + 1)
        except TimeoutError:
            print(f"timeout token={token} bind={bind}:{port}")
            return 3
    try:
        received = parse_packet(data)
    except (UnicodeDecodeError, ValueError) as error:
        print(f"rejected reason={error}")
        return 4
    if received != token:
        print(f"rejected reason=token-mismatch received={received}")
        return 5
    print(f"received token={token} bytes={len(data)} source={peer[0]}:{peer[1]}")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    subcommands = result.add_subparsers(dest="command", required=True)
    sender = subcommands.add_parser("send")
    sender.add_argument("--bind", required=True)
    sender.add_argument("--destination", required=True)
    sender.add_argument("--port", required=True, type=int)
    sender.add_argument("--token", required=True)
    receiver = subcommands.add_parser("listen")
    receiver.add_argument("--bind", required=True)
    receiver.add_argument("--port", required=True, type=int)
    receiver.add_argument("--token", required=True)
    receiver.add_argument("--timeout", type=float, default=1.0)
    return result


def main() -> int:
    arguments = parser().parse_args()
    if not 1 <= arguments.port <= 65535:
        raise ValueError("port must be between 1 and 65535")
    if arguments.command == "send":
        return send(arguments.bind, arguments.destination, arguments.port, arguments.token)
    if not 0.1 <= arguments.timeout <= 5.0:
        raise ValueError("timeout must be between 0.1 and 5 seconds")
    return listen(arguments.bind, arguments.port, arguments.token, arguments.timeout)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, UnicodeError, ValueError) as error:
        print(f"diagnostic failed: {error}", file=sys.stderr)
        raise SystemExit(2)
