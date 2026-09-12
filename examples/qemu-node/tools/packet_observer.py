#!/usr/bin/env python3
"""Bounded packet observer, history API, and telemetry adapter."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import ipaddress
import json
import os
import secrets
import signal
import socket
import sqlite3
import stat
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

MAGIC = {
    b"\xd4\xc3\xb2\xa1": ("<", 1_000_000),
    b"\xa1\xb2\xc3\xd4": (">", 1_000_000),
    b"\x4d\x3c\xb2\xa1": ("<", 1_000_000_000),
    b"\xa1\xb2\x3c\x4d": (">", 1_000_000_000),
}
PCAPNG_SECTION = b"\x0a\x0d\x0d\x0a"
PCAPNG_LITTLE_ENDIAN = b"\x4d\x3c\x2b\x1a"
PCAPNG_BIG_ENDIAN = b"\x1a\x2b\x3c\x4d"
MAX_PACKET_BYTES = 16 * 1024 * 1024
GUEST_ADDRESS = "10.0.2.15"
GUEST_PORT = 18001
RECEIVER_PORT = 19001


def packet_rules() -> list[dict[str, object]]:
    source = os.environ.get("GRAPHX_PACKET_RULES", "")
    if not source:
        return [
            {"edge_id": "peer-qemu-tcp", "direction": "host-to-qemu", "node_id": "qemu-node",
             "protocol": "TCP", "destination": GUEST_ADDRESS, "destination_port": GUEST_PORT},
            {"edge_id": "peer-qemu-udp", "direction": "host-to-qemu", "node_id": "qemu-node",
             "protocol": "UDP", "destination": GUEST_ADDRESS, "destination_port": GUEST_PORT},
            {"edge_id": "qemu-peer-tcp", "direction": "qemu-to-host", "node_id": "host-peer",
             "protocol": "TCP", "source": GUEST_ADDRESS, "destination_port": RECEIVER_PORT},
            {"edge_id": "qemu-peer-udp", "direction": "qemu-to-host", "node_id": "host-peer",
             "protocol": "UDP", "source": GUEST_ADDRESS, "destination_port": RECEIVER_PORT},
        ]
    try:
        value = json.loads(source)
    except json.JSONDecodeError as error:
        raise ValueError("GRAPHX_PACKET_RULES must be valid JSON") from error
    if not isinstance(value, list) or not 1 <= len(value) <= 64:
        raise ValueError("GRAPHX_PACKET_RULES must contain 1 through 64 rules")
    allowed = {"edge_id", "direction", "node_id", "protocol", "source", "destination",
               "source_port", "destination_port"}
    for index, rule in enumerate(value):
        if not isinstance(rule, dict) or set(rule) - allowed:
            raise ValueError(f"GRAPHX_PACKET_RULES[{index}] contains invalid fields")
        for required in ("edge_id", "direction", "node_id", "protocol"):
            if not isinstance(rule.get(required), str) or not 1 <= len(rule[required]) <= 64:
                raise ValueError(f"GRAPHX_PACKET_RULES[{index}].{required} is invalid")
        if rule["protocol"] not in ("TCP", "UDP"):
            raise ValueError(f"GRAPHX_PACKET_RULES[{index}].protocol must be TCP or UDP")
        if not any(name in rule for name in ("source", "destination", "source_port", "destination_port")):
            raise ValueError(f"GRAPHX_PACKET_RULES[{index}] must constrain an address or port")
        for name in ("source", "destination"):
            if name in rule:
                try:
                    ipaddress.ip_address(rule[name])
                except ValueError as error:
                    raise ValueError(f"GRAPHX_PACKET_RULES[{index}].{name} is invalid") from error
        for name in ("source_port", "destination_port"):
            if name in rule and (not isinstance(rule[name], int) or not 1 <= rule[name] <= 65535):
                raise ValueError(f"GRAPHX_PACKET_RULES[{index}].{name} is invalid")
    return value


PACKET_RULES = packet_rules()


def daemonize(pid_file: Path, log_file: Path) -> bool:
    if not hasattr(os, "fork"):
        raise RuntimeError("observer daemon mode requires a POSIX host")
    child = os.fork()
    if child:
        os.waitpid(child, 0)
        return False
    os.setsid()
    grandchild = os.fork()
    if grandchild:
        os._exit(0)
    log_file.parent.mkdir(parents=True, exist_ok=True)
    null_fd = os.open(os.devnull, os.O_RDONLY)
    log_fd = os.open(log_file, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    os.dup2(null_fd, 0)
    os.dup2(log_fd, 1)
    os.dup2(log_fd, 2)
    os.close(null_fd)
    os.close(log_fd)
    pid_file.write_text(f"{os.getpid()}\n", encoding="ascii")
    os.chmod(pid_file, 0o600)
    return True


def integer(name: str, default: int, minimum: int, maximum: int) -> int:
    value = os.environ.get(name, str(default))
    if not value.isdigit() or not minimum <= int(value) <= maximum:
        raise ValueError(f"{name} must be an integer from {minimum} through {maximum}")
    return int(value)


def boolean(name: str, default: bool) -> bool:
    value = os.environ.get(name, "true" if default else "false").lower()
    if value in ("1", "true", "yes", "on"):
        return True
    if value in ("0", "false", "no", "off"):
        return False
    raise ValueError(f"{name} must be a boolean")


def pcapng_block(block_type: int, body: bytes) -> bytes:
    length = len(body) + 12
    return struct.pack("<II", block_type, length) + body + struct.pack("<I", length)


def pcapng_headers() -> bytes:
    section = struct.pack("<IHHq", 0x1A2B3C4D, 1, 0, -1)
    interface = struct.pack("<HHI", 1, 0, 65535)
    return pcapng_block(0x0A0D0D0A, section) + pcapng_block(1, interface)


def pcapng_packet(timestamp: float, original: int, packet: bytes) -> bytes:
    microseconds = int(timestamp * 1_000_000)
    padding = b"\0" * ((-len(packet)) % 4)
    body = struct.pack(
        "<IIIII", 0, microseconds >> 32, microseconds & 0xFFFFFFFF, len(packet), original
    )
    return pcapng_block(6, body + packet + padding)


def decode_packet(packet: bytes) -> dict[str, object] | None:
    if len(packet) < 14:
        return None
    ether_type = struct.unpack_from("!H", packet, 12)[0]
    offset = 14
    if ether_type == 0x8100 and len(packet) >= 18:
        ether_type = struct.unpack_from("!H", packet, 16)[0]
        offset = 18
    if ether_type != 0x0800 or len(packet) < offset + 20:
        return None
    version_ihl = packet[offset]
    ihl = (version_ihl & 0x0F) * 4
    if version_ihl >> 4 != 4 or ihl < 20 or len(packet) < offset + ihl:
        return None
    total_length = struct.unpack_from("!H", packet, offset + 2)[0]
    if total_length < ihl or offset + total_length > len(packet):
        return None
    packet_end = offset + total_length
    protocol_number = packet[offset + 9]
    source = str(ipaddress.ip_address(packet[offset + 12 : offset + 16]))
    destination = str(ipaddress.ip_address(packet[offset + 16 : offset + 20]))
    transport = offset + ihl
    if protocol_number == 17:
        if packet_end < transport + 8:
            return None
        source_port, destination_port, udp_length = struct.unpack_from("!HHH", packet, transport)
        if udp_length < 8 or transport + udp_length > packet_end:
            return None
        payload = packet[transport + 8 : transport + udp_length]
        protocol = "UDP"
    elif protocol_number == 6:
        if packet_end < transport + 20:
            return None
        source_port, destination_port = struct.unpack_from("!HH", packet, transport)
        tcp_header = (packet[transport + 12] >> 4) * 4
        if tcp_header < 20 or transport + tcp_header > packet_end:
            return None
        payload = packet[transport + tcp_header : packet_end]
        protocol = "TCP"
    else:
        return None
    if not payload:
        return None
    matched = None
    fields = {"protocol": protocol, "source": source, "destination": destination,
              "source_port": source_port, "destination_port": destination_port}
    for rule in PACKET_RULES:
        if all(name not in rule or rule[name] == fields[name]
               for name in ("protocol", "source", "destination", "source_port", "destination_port")):
            matched = rule
            break
    if matched is None:
        return None
    return {
        "protocol": protocol,
        "source_address": source,
        "destination_address": destination,
        "source_port": source_port,
        "destination_port": destination_port,
        "payload": payload,
        "edge_id": matched["edge_id"],
        "direction": matched["direction"],
        "node_id": matched["node_id"],
    }


class Observer:
    def __init__(self, capture: Path, database: Path, pcapng: Path | None = None) -> None:
        self.capture = capture
        self.pcapng = pcapng
        self.database = database
        self.capture_session = os.environ.get(
            "GRAPHX_PACKET_CAPTURE_SESSION", os.environ.get("GRAPHX_QEMU_RUN_ID", "manual")
        )
        if not self.capture_session.replace("-", "").replace("_", "").isalnum() or len(self.capture_session) > 64:
            raise ValueError("GRAPHX_QEMU_RUN_ID must be a bounded identifier")
        normalized = {}
        normalized_config = os.environ.get("GRAPHX_NORMALIZED_CONFIG", "")
        if normalized_config:
            with open(normalized_config, encoding="utf-8") as stream:
                normalized = json.load(stream)
        runtime = normalized.get("observability", {})
        history = runtime.get("history", {})
        capture_config = runtime.get("capture", {})
        network_capture_id = os.environ.get("GRAPHX_NETWORK_CAPTURE_ID", "")
        if network_capture_id:
            captures = normalized.get("network", {}).get("captures", [])
            network_capture = next(
                (value for value in captures if value.get("id") == network_capture_id), None
            )
            if network_capture is None:
                raise ValueError(f"network capture {network_capture_id!r} is not configured")
            capture_config = {**capture_config, "max_file_bytes": network_capture["max_file_bytes"]}
        telemetry = runtime.get("telemetry", {})
        self.max_records = history.get("max_records", 50_000)
        self.history_enabled = boolean("GRAPHX_PACKET_HISTORY_ENABLED", True)
        self.capture_enabled = boolean("GRAPHX_CAPTURE_ENABLED", capture_config.get("enabled", True))
        self.retention = history.get("retention_seconds", 86_400)
        self.max_database_bytes = history.get("max_database_bytes", 64 * 1024 * 1024)
        self.preview_bytes = integer("GRAPHX_PACKET_HISTORY_PREVIEW_BYTES", 64, 0, 1024)
        self.max_capture_bytes = capture_config.get("max_file_bytes", 64 * 1024 * 1024)
        self.max_packets = capture_config.get("max_packets", 100_000)
        self.telemetry_host = telemetry.get("host", "telemetry")
        self.telemetry_port = telemetry.get("port", 9000)
        self.telemetry_secret = os.environ.get("GRAPHX_TELEMETRY_SHARED_SECRET", "")
        if self.telemetry_secret and len(self.telemetry_secret.encode()) < 32:
            raise ValueError("GRAPHX_TELEMETRY_SHARED_SECRET must contain at least 32 bytes")
        self.offset = 24
        self.capture_generation = 0
        self.capture_identity: tuple[int, int] | None = None
        self.invalid_identity: tuple[int, int] | None = None
        self.pcapng_offset = 0
        self.pcapng_identity: tuple[int, int] | None = None
        self.pcapng_checkpoint: tuple[int, bytes] | None = None
        self.pcapng_endian = "<"
        self.pcapng_interfaces: dict[int, float] = {}
        self.endian = "<"
        self.timestamp_scale = 1_000_000
        self.snaplen = 65_535
        self.sequence = 0
        self.observed = 0
        self.dropped = 0
        self.capture_packets = 0
        self.capture_bytes = 0
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.database.parent.mkdir(parents=True, exist_ok=True)
        if self.pcapng is not None:
            self.pcapng.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.database, check_same_thread=False)
        self.connection.execute("PRAGMA journal_mode=WAL")
        self.connection.execute("PRAGMA synchronous=NORMAL")
        self.connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS packet_history (
              id INTEGER PRIMARY KEY,
              capture_session TEXT NOT NULL,
              capture_offset INTEGER NOT NULL,
              captured_at REAL NOT NULL,
              direction TEXT NOT NULL,
              edge_id TEXT NOT NULL,
              protocol TEXT NOT NULL CHECK(protocol IN ('TCP', 'UDP')),
              source_address TEXT NOT NULL,
              destination_address TEXT NOT NULL,
              source_port INTEGER NOT NULL,
              destination_port INTEGER NOT NULL,
              wire_length INTEGER NOT NULL,
              captured_length INTEGER NOT NULL DEFAULT 0,
              original_length INTEGER NOT NULL DEFAULT 0,
              truncated INTEGER NOT NULL DEFAULT 0 CHECK(truncated IN (0, 1)),
              payload_length INTEGER NOT NULL,
              payload_preview_hex TEXT NOT NULL,
              UNIQUE(capture_session, capture_offset)
            );
            CREATE INDEX IF NOT EXISTS packet_history_time ON packet_history(captured_at);
            """
        )
        self.connection.commit()
        os.chmod(self.database, 0o600)
        if self.pcapng is not None:
            with self.pcapng.open("wb") as output:
                output.write(pcapng_headers())
            os.chmod(self.pcapng, 0o644)
            self.capture_bytes = self.pcapng.stat().st_size

    def close(self) -> None:
        self.stop.set()
        with self.lock:
            self.connection.commit()
            self.connection.close()

    def signed(self, payload: dict[str, object]) -> bytes:
        if not self.telemetry_secret:
            return json.dumps(payload, separators=(",", ":")).encode()
        timestamp = int(time.time() * 1000)
        nonce = secrets.token_hex(16)
        compact = json.dumps(payload, separators=(",", ":"))
        text = f"{timestamp}.{nonce}.{compact}".encode()
        signature = hmac.new(self.telemetry_secret.encode(), text, hashlib.sha256).hexdigest()
        return json.dumps(
            {"payload": payload, "auth": {"timestamp": timestamp, "nonce": nonce, "signature": signature}},
            separators=(",", ":"),
        ).encode()

    def publish(self, decoded: dict[str, object], timestamp: float, wire_length: int) -> None:
        self.sequence += 1
        node_id = decoded["node_id"]
        payload = {
            "kind": "network_packet",
            "event": "receive",
            "nodeId": node_id,
            "edgeId": decoded["edge_id"],
            "timestamp": int(timestamp * 1000),
            "sequence": self.sequence,
            "wireBytes": wire_length,
            "payloadBytes": len(decoded["payload"]),
            "protocol": decoded["protocol"],
            "sourceAddress": decoded["source_address"],
            "destinationAddress": decoded["destination_address"],
            "sourcePort": decoded["source_port"],
            "destinationPort": decoded["destination_port"],
            "direction": "observed",
            "observationSource": os.environ.get("GRAPHX_PACKET_OBSERVATION_SOURCE", "qemu-pcap"),
            "type": f"Raw{decoded['protocol']}",
        }
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as output:
                output.sendto(self.signed(payload), (self.telemetry_host, self.telemetry_port))
        except OSError:
            self.dropped += 1

    def retain(self, capture_offset: int, timestamp: float, original_length: int,
               captured_length: int,
               decoded: dict[str, object]) -> bool:
        if not self.history_enabled:
            self.observed += 1
            return True
        payload = decoded["payload"]
        with self.lock:
            cursor = self.connection.execute(
                """INSERT OR IGNORE INTO packet_history
                   (capture_session, capture_offset, captured_at, direction, edge_id, protocol,
                    source_address, destination_address, source_port, destination_port,
                    wire_length, captured_length, original_length, truncated,
                    payload_length, payload_preview_hex)
                   VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (f"{self.capture_session}-{self.capture_generation}", capture_offset, timestamp, decoded["direction"], decoded["edge_id"],
                 decoded["protocol"], decoded["source_address"], decoded["destination_address"],
                 decoded["source_port"], decoded["destination_port"], original_length,
                 captured_length, original_length, int(captured_length < original_length),
                 len(payload), payload[: self.preview_bytes].hex()),
            )
            cutoff = time.time() - self.retention
            self.connection.execute("DELETE FROM packet_history WHERE captured_at < ?", (cutoff,))
            self.connection.execute(
                "DELETE FROM packet_history WHERE id <= "
                "COALESCE((SELECT MAX(id) - ? FROM packet_history), 0)",
                (self.max_records,),
            )
            self.connection.commit()
            if self.observed and self.observed % 256 == 0:
                self.connection.execute("PRAGMA wal_checkpoint(TRUNCATE)")
                database_bytes = sum(
                    path.stat().st_size for path in
                    [self.database, Path(f"{self.database}-wal"), Path(f"{self.database}-shm")]
                    if path.exists()
                )
                if database_bytes > self.max_database_bytes:
                    self.connection.execute(
                        "DELETE FROM packet_history WHERE id <= "
                        "COALESCE((SELECT id FROM packet_history ORDER BY id DESC LIMIT 1 OFFSET ?), 0)",
                        (max(1, self.max_records // 2),),
                    )
                    self.connection.commit()
                    self.connection.execute("VACUUM")
        self.observed += 1
        return cursor.rowcount > 0

    def append_capture(self, timestamp: float, wire_length: int, packet: bytes) -> None:
        if not self.capture_enabled or self.pcapng is None:
            return
        block = pcapng_packet(timestamp, wire_length, packet)
        if self.capture_packets >= self.max_packets or self.capture_bytes + len(block) > self.max_capture_bytes:
            self.dropped += 1
            return
        with self.pcapng.open("ab") as output:
            output.write(block)
        self.capture_packets += 1
        self.capture_bytes += len(block)

    def read_classic_available(self) -> bool:
        try:
            metadata = self.capture.stat()
        except FileNotFoundError:
            return False
        size = metadata.st_size
        identity = (metadata.st_dev, metadata.st_ino)
        if self.capture_identity != identity:
            if self.capture_identity is not None:
                self.capture_generation += 1
            self.capture_identity = identity
            self.invalid_identity = None
            self.offset = 24
        if self.invalid_identity == identity:
            return False
        if size < self.offset:
            if size < 24:
                return False
            self.offset = 24
            self.capture_generation += 1
        with self.capture.open("rb") as source:
            if self.offset == 24:
                header = source.read(24)
                if len(header) < 24:
                    return False
                if header[:4] not in MAGIC:
                    self.dropped += 1
                    self.invalid_identity = identity
                    return False
                self.endian, self.timestamp_scale = MAGIC[header[:4]]
                self.snaplen = struct.unpack_from(f"{self.endian}I", header, 16)[0]
                if not 1 <= self.snaplen <= MAX_PACKET_BYTES:
                    self.dropped += 1
                    self.invalid_identity = identity
                    return False
                if struct.unpack_from(f"{self.endian}I", header, 20)[0] != 1:
                    self.dropped += 1
                    self.invalid_identity = identity
                    return False
            source.seek(self.offset)
            packet_header = struct.Struct(f"{self.endian}IIII")
            progressed = False
            while True:
                capture_offset = source.tell()
                record = source.read(packet_header.size)
                if len(record) < packet_header.size:
                    break
                seconds, fraction, included, original = packet_header.unpack(record)
                if included > MAX_PACKET_BYTES:
                    self.dropped += 1
                    self.invalid_identity = identity
                    return progressed
                packet = source.read(included)
                if len(packet) < included:
                    break
                self.offset = source.tell()
                progressed = True
                if (included == 0 or included > self.snaplen or included > original or
                        original > MAX_PACKET_BYTES or fraction >= self.timestamp_scale):
                    self.dropped += 1
                    continue
                timestamp = seconds + fraction / self.timestamp_scale
                self.append_capture(timestamp, original, packet)
                decoded = decode_packet(packet)
                if decoded is not None:
                    if self.retain(capture_offset, timestamp, original, included, decoded):
                        self.publish(decoded, timestamp, original)
            return progressed

    def open_pcapng_snapshot(self):
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(self.capture, flags)
        try:
            metadata = os.fstat(descriptor)
            published = self.capture.lstat()
            if (not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1 or
                    (metadata.st_mode & 0o022) != 0 or metadata.st_uid not in (0, os.getuid()) or
                    (published.st_dev, published.st_ino) != (metadata.st_dev, metadata.st_ino)):
                raise ValueError("PCAPNG snapshot has an unsafe identity, owner, or mode")
            if metadata.st_size > self.max_capture_bytes + MAX_PACKET_BYTES + 266_240:
                raise ValueError("PCAPNG snapshot exceeds its configured bound")
            return os.fdopen(descriptor, "rb"), metadata
        except BaseException:
            os.close(descriptor)
            raise

    def reset_pcapng(self, identity: tuple[int, int]) -> None:
        if self.pcapng_identity is not None:
            self.capture_generation += 1
        self.pcapng_identity = identity
        self.pcapng_offset = 0
        self.pcapng_checkpoint = None
        self.pcapng_interfaces.clear()

    def read_pcapng_available(self) -> bool:
        try:
            source, metadata = self.open_pcapng_snapshot()
        except FileNotFoundError:
            return False
        with source:
            identity = (metadata.st_dev, metadata.st_ino)
            if identity != self.pcapng_identity:
                continued = False
                if self.pcapng_checkpoint is not None:
                    checkpoint_offset, checkpoint_digest = self.pcapng_checkpoint
                    source.seek(checkpoint_offset)
                    header = source.read(8)
                    if len(header) == 8:
                        length = struct.unpack(f"{self.pcapng_endian}I", header[4:8])[0]
                        source.seek(checkpoint_offset)
                        block = source.read(length) if 12 <= length <= MAX_PACKET_BYTES + 4096 else b""
                        continued = hashlib.sha256(block).digest() == checkpoint_digest
                if continued:
                    self.pcapng_identity = identity
                else:
                    self.reset_pcapng(identity)
            if metadata.st_size < self.pcapng_offset:
                self.reset_pcapng(identity)
            source.seek(self.pcapng_offset)
            progressed = False
            while self.pcapng_offset + 12 <= metadata.st_size:
                block_start = self.pcapng_offset
                header = source.read(12)
                if len(header) != 12:
                    break
                if header[:4] == PCAPNG_SECTION:
                    if header[8:12] == PCAPNG_LITTLE_ENDIAN:
                        endian = "<"
                    elif header[8:12] == PCAPNG_BIG_ENDIAN:
                        endian = ">"
                    else:
                        self.dropped += 1
                        break
                    block_type = 0x0A0D0D0A
                else:
                    endian = self.pcapng_endian
                    block_type = struct.unpack(f"{endian}I", header[:4])[0]
                block_length = struct.unpack(f"{endian}I", header[4:8])[0]
                if (block_length < 12 or block_length % 4 != 0 or
                        block_length > MAX_PACKET_BYTES + 4096 or
                        block_start + block_length > metadata.st_size):
                    break
                remainder = source.read(block_length - 12)
                block = header + remainder
                if len(block) != block_length or struct.unpack(f"{endian}I", block[-4:])[0] != block_length:
                    self.dropped += 1
                    break
                if block_type == 0x0A0D0D0A:
                    self.pcapng_endian = endian
                    self.pcapng_interfaces.clear()
                elif block_type == 1:
                    body = block[8:-4]
                    if len(body) < 8 or struct.unpack(f"{endian}H", body[:2])[0] != 1:
                        self.dropped += 1
                    else:
                        scale = 1_000_000.0
                        option = 8
                        while option + 4 <= len(body):
                            code, length = struct.unpack_from(f"{endian}HH", body, option)
                            option += 4
                            value = body[option:option + length]
                            option += (length + 3) & ~3
                            if code == 0:
                                break
                            if code == 9 and length == 1:
                                resolution = value[0]
                                scale = float(2 ** (resolution & 0x7f) if resolution & 0x80
                                              else 10 ** resolution)
                        self.pcapng_interfaces[len(self.pcapng_interfaces)] = scale
                elif block_type == 6:
                    body = block[8:-4]
                    if len(body) < 20:
                        self.dropped += 1
                    else:
                        interface, high, low, included, original = struct.unpack_from(
                            f"{endian}IIIII", body)
                        packet = body[20:20 + included]
                        scale = self.pcapng_interfaces.get(interface)
                        if (scale is None or included == 0 or included > original or
                                included > MAX_PACKET_BYTES or len(packet) != included):
                            self.dropped += 1
                        else:
                            timestamp = ((high << 32) | low) / scale
                            self.capture_packets += 1
                            decoded = decode_packet(packet)
                            if decoded is not None and self.retain(
                                    block_start, timestamp, original, included, decoded):
                                self.publish(decoded, timestamp, original)
                self.pcapng_offset += block_length
                self.pcapng_checkpoint = (block_start, hashlib.sha256(block).digest())
                progressed = True
            self.capture_bytes = metadata.st_size
            return progressed

    def read_available(self) -> bool:
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
        try:
            descriptor = os.open(self.capture, flags)
        except FileNotFoundError:
            return False
        try:
            magic = os.read(descriptor, 4)
        finally:
            os.close(descriptor)
        if magic == PCAPNG_SECTION:
            if self.pcapng is not None:
                raise ValueError("a PCAPNG source must not specify a second PCAPNG output")
            return self.read_pcapng_available()
        if magic in MAGIC:
            if self.pcapng is None:
                raise ValueError("classic PCAP input requires an explicit PCAPNG output")
            return self.read_classic_available()
        self.dropped += 1
        return False

    def status(self) -> dict[str, object]:
        with self.lock:
            records = self.connection.execute("SELECT COUNT(*) FROM packet_history").fetchone()[0]
        database_bytes = sum(
            path.stat().st_size for path in [self.database, Path(f"{self.database}-wal"), Path(f"{self.database}-shm")]
            if path.exists()
        )
        return {
            "status": "ready" if self.history_enabled else "disabled",
            "enabled": self.history_enabled,
            "records": records,
            "observed": self.observed,
            "dropped": self.dropped,
            "databaseBytes": database_bytes,
            "captureBytes": self.capture_bytes,
            "capturePackets": self.capture_packets,
            "maxRecords": self.max_records,
            "maxDatabaseBytes": self.max_database_bytes,
            "maxCaptureBytes": self.max_capture_bytes,
            "maxCapturePackets": self.max_packets,
        }

    def query(self, limit: int, before: int | None, protocol: str | None) -> dict[str, object]:
        clauses, parameters = [], []
        if before is not None:
            clauses.append("id < ?")
            parameters.append(before)
        if protocol is not None:
            clauses.append("protocol = ?")
            parameters.append(protocol)
        where = f" WHERE {' AND '.join(clauses)}" if clauses else ""
        with self.lock:
            rows = self.connection.execute(
                "SELECT id, captured_at, direction, edge_id, protocol, source_address, "
                "destination_address, source_port, destination_port, wire_length, "
                "captured_length, original_length, truncated, payload_length, "
                f"payload_preview_hex FROM packet_history{where} ORDER BY id DESC LIMIT ?",
                (*parameters, limit + 1),
            ).fetchall()
        more = len(rows) > limit
        rows = rows[:limit]
        records = [{
            "id": row[0], "capturedAt": row[1], "direction": row[2], "edgeId": row[3],
            "protocol": row[4], "sourceAddress": row[5], "destinationAddress": row[6],
            "sourcePort": row[7], "destinationPort": row[8], "wireLength": row[9],
            "capturedLength": row[10], "originalLength": row[11],
            "truncated": bool(row[12]), "payloadLength": row[13],
            "payloadPreviewHex": row[14],
        } for row in rows]
        return {"records": records, "nextCursor": rows[-1][0] if more and rows else None,
                "stats": self.status()}


def make_handler(observer: Observer):
    class Handler(BaseHTTPRequestHandler):
        def send_json(self, status: int, value: object) -> None:
            payload = json.dumps(value, separators=(",", ":")).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)

        def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
            parsed = urlparse(self.path)
            if parsed.path in ("/health", "/status"):
                return self.send_json(200, observer.status())
            if parsed.path != "/history":
                return self.send_json(404, {"error": "not found"})
            query = parse_qs(parsed.query, keep_blank_values=True)
            if any(key not in {"limit", "before", "protocol"} for key in query):
                return self.send_json(400, {"error": "unknown query parameter"})
            try:
                limit_text = query.get("limit", ["100"])[0]
                if not limit_text.isdigit() or not 1 <= int(limit_text) <= 500:
                    raise ValueError("limit must be from 1 through 500")
                before_text = query.get("before", [None])[0]
                if before_text is not None and (not before_text.isdigit() or int(before_text) < 1):
                    raise ValueError("before must be a positive record id")
                protocol = query.get("protocol", [None])[0]
                if protocol not in (None, "TCP", "UDP"):
                    raise ValueError("protocol must be TCP or UDP")
                result = observer.query(int(limit_text), int(before_text) if before_text else None, protocol)
            except ValueError as error:
                return self.send_json(400, {"error": str(error)})
            return self.send_json(200, result)

        def log_message(self, _format: str, *_args: object) -> None:
            return

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--pcapng", type=Path)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--daemonize", action="store_true")
    parser.add_argument("--pid-file", type=Path)
    parser.add_argument("--log-file", type=Path)
    parser.add_argument("--http-bind", default=os.environ.get("GRAPHX_PACKET_HISTORY_BIND", "127.0.0.1"))
    parser.add_argument("--http-port", type=int,
                        default=integer("GRAPHX_PACKET_HISTORY_PORT", 9100, 1, 65535))
    args = parser.parse_args()
    if args.daemonize:
        if args.once or args.pid_file is None or args.log_file is None:
            parser.error("--daemonize requires --pid-file and --log-file and cannot use --once")
        if not daemonize(args.pid_file, args.log_file):
            return
    observer = Observer(args.capture, args.database, args.pcapng)
    server = None
    try:
        signal.signal(signal.SIGTERM, lambda *_: observer.stop.set())
        signal.signal(signal.SIGINT, lambda *_: observer.stop.set())
        if not args.once:
            server = ThreadingHTTPServer((args.http_bind, args.http_port), make_handler(observer))
            threading.Thread(target=server.serve_forever, daemon=True).start()
        while True:
            observer.read_available()
            if args.once:
                break
            if observer.stop.wait(0.1):
                break
    except KeyboardInterrupt:
        pass
    finally:
        if server:
            server.shutdown()
            server.server_close()
        observer.close()
    print(json.dumps(observer.status() if False else {"status": "stopped", "observed": observer.observed}))


if __name__ == "__main__":
    main()
