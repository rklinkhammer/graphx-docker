#!/usr/bin/env python3
"""Bounded SDR demo wire format and GraphX telemetry helpers."""

from __future__ import annotations

import hashlib
import hmac
import json
import os
import secrets
import socket
import struct
import time
from functools import lru_cache

MAGIC = b"SDR1"
HEADER = struct.Struct("!4sIQH")
SAMPLE = struct.Struct("!hh")
MAX_SAMPLES = 256


@lru_cache(maxsize=1)
def telemetry_endpoint() -> tuple[str, int]:
    """Read the shared telemetry destination from normalized GraphX configuration."""
    path = os.environ.get("GRAPHX_NORMALIZED_CONFIG", "")
    if not path:
        return "telemetry", 9000
    with open(path, encoding="utf-8") as stream:
        telemetry = json.load(stream)["observability"]["telemetry"]
    return telemetry["host"], telemetry["port"]


def recv_line(connection: socket.socket, maximum: int = 4096) -> bytes:
    """Receive exactly one bounded line from a one-request connection."""
    value = bytearray()
    while b"\n" not in value:
        chunk = connection.recv(min(1024, maximum + 1 - len(value)))
        if not chunk:
            raise ValueError("connection closed before the line terminator")
        value.extend(chunk)
        if len(value) > maximum:
            raise ValueError("line exceeds the configured bound")
    if value[-1:] != b"\n" or value.count(b"\n") != 1:
        raise ValueError("connection contains trailing or multiple records")
    return bytes(value)


def encode_samples(sequence: int, frequency_hz: int, count: int = 16) -> bytes:
    if not 0 <= sequence <= 0xFFFFFFFF or not 1_000_000 <= frequency_hz <= 6_000_000_000:
        raise ValueError("invalid SDR sequence or frequency")
    if not 1 <= count <= MAX_SAMPLES:
        raise ValueError("sample count is out of range")
    pairs = b"".join(SAMPLE.pack((sequence + index) % 32768, -((sequence + index) % 32768))
                     for index in range(count))
    return HEADER.pack(MAGIC, sequence, frequency_hz, count) + pairs


def decode_samples(payload: bytes) -> tuple[int, int, list[tuple[int, int]]]:
    if len(payload) < HEADER.size:
        raise ValueError("truncated SDR header")
    magic, sequence, frequency_hz, count = HEADER.unpack_from(payload)
    if magic != MAGIC or not 1_000_000 <= frequency_hz <= 6_000_000_000 or \
            not 1 <= count <= MAX_SAMPLES:
        raise ValueError("invalid SDR header")
    if len(payload) != HEADER.size + count * SAMPLE.size:
        raise ValueError("SDR payload length does not match sample count")
    samples = [SAMPLE.unpack_from(payload, HEADER.size + index * SAMPLE.size)
               for index in range(count)]
    return sequence, frequency_hz, samples


def signed(payload: dict[str, object], secret: str) -> bytes:
    if not secret:
        return json.dumps(payload, separators=(",", ":")).encode()
    timestamp = int(time.time() * 1000)
    nonce = secrets.token_hex(16)
    compact = json.dumps(payload, separators=(",", ":"))
    signature = hmac.new(secret.encode(), f"{timestamp}.{nonce}.{compact}".encode(),
                         hashlib.sha256).hexdigest()
    return json.dumps({"payload": payload, "auth": {"timestamp": timestamp, "nonce": nonce,
                                                      "signature": signature}},
                      separators=(",", ":")).encode()


def verified(data: bytes, secret: str) -> dict[str, object] | None:
    """Verify one bounded telemetry/control datagram."""
    try:
        value = json.loads(data)
        if not secret:
            return value if isinstance(value, dict) and "auth" not in value else None
        payload, auth = value["payload"], value["auth"]
        timestamp = auth["timestamp"]
        nonce = auth["nonce"]
        supplied = auth["signature"]
        if not isinstance(timestamp, int) or abs(int(time.time() * 1000) - timestamp) > 30_000:
            return None
        if not isinstance(nonce, str) or len(nonce) != 32 or not isinstance(supplied, str):
            return None
        compact = json.dumps(payload, separators=(",", ":"))
        expected = hmac.new(secret.encode(), f"{timestamp}.{nonce}.{compact}".encode(),
                            hashlib.sha256).hexdigest()
        return payload if hmac.compare_digest(expected, supplied) else None
    except (KeyError, TypeError, ValueError, json.JSONDecodeError):
        return None


def publish_heartbeat(node_id: str, sequence: int) -> None:
    host, port = telemetry_endpoint()
    secret = os.environ.get("GRAPHX_TELEMETRY_SHARED_SECRET", "")
    event = {"kind": "trace", "event": "heartbeat", "nodeId": node_id,
             "timestamp": int(time.time() * 1000), "sequence": sequence}
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as output:
            output.sendto(signed(event, secret), (host, port))
    except OSError:
        pass
