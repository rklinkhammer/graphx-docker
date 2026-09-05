#!/usr/bin/env python3
"""Index bounded Ethernet/IPv4 TCP and UDP summaries from a classic PCAP."""

from __future__ import annotations

import argparse
import ipaddress
import os
import sqlite3
import struct
from pathlib import Path

MAGIC = {
    b"\xd4\xc3\xb2\xa1": ("<", 1_000_000),
    b"\xa1\xb2\xc3\xd4": (">", 1_000_000),
    b"\x4d\x3c\xb2\xa1": ("<", 1_000_000_000),
    b"\xa1\xb2\x3c\x4d": (">", 1_000_000_000),
}
MAX_PACKET_BYTES = 16 * 1024 * 1024


def decode_packet(packet: bytes) -> tuple[str, str, str, int, int, bytes] | None:
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
    if version_ihl >> 4 != 4:
        return None
    ihl = (version_ihl & 0x0F) * 4
    if ihl < 20 or len(packet) < offset + ihl:
        return None
    protocol_number = packet[offset + 9]
    source = str(ipaddress.ip_address(packet[offset + 12 : offset + 16]))
    destination = str(ipaddress.ip_address(packet[offset + 16 : offset + 20]))
    transport = offset + ihl
    if protocol_number == 17 and len(packet) >= transport + 8:
        source_port, destination_port = struct.unpack_from("!HH", packet, transport)
        return "UDP", source, destination, source_port, destination_port, packet[transport + 8 :]
    if protocol_number == 6 and len(packet) >= transport + 20:
        source_port, destination_port = struct.unpack_from("!HH", packet, transport)
        tcp_header = (packet[transport + 12] >> 4) * 4
        if tcp_header < 20 or len(packet) < transport + tcp_header:
            return None
        return "TCP", source, destination, source_port, destination_port, packet[transport + tcp_header :]
    return None


def packets(path: Path):
    with path.open("rb") as capture:
        header = capture.read(24)
        if len(header) != 24 or header[:4] not in MAGIC:
            raise ValueError("input is not a supported classic PCAP file")
        endian, timestamp_scale = MAGIC[header[:4]]
        link_type = struct.unpack_from(f"{endian}I", header, 20)[0]
        if link_type != 1:
            raise ValueError(f"expected Ethernet link type 1, got {link_type}")
        packet_header = struct.Struct(f"{endian}IIII")
        while True:
            record = capture.read(packet_header.size)
            if not record:
                return
            if len(record) != packet_header.size:
                raise ValueError("truncated PCAP packet header")
            seconds, fraction, included, original = packet_header.unpack(record)
            if included > MAX_PACKET_BYTES:
                raise ValueError(f"packet exceeds {MAX_PACKET_BYTES} byte safety limit")
            payload = capture.read(included)
            if len(payload) != included:
                raise ValueError("truncated PCAP packet data")
            yield seconds + fraction / timestamp_scale, original, payload


def index_capture(capture: Path, database: Path, max_records: int, preview_bytes: int) -> int:
    database.parent.mkdir(parents=True, exist_ok=True)
    if database.exists() and not database.is_file():
        raise ValueError("database path is not a regular file")
    connection = sqlite3.connect(database)
    try:
        connection.executescript(
            """
            PRAGMA journal_mode=WAL;
            CREATE TABLE IF NOT EXISTS packet_history (
              id INTEGER PRIMARY KEY,
              captured_at REAL NOT NULL,
              protocol TEXT NOT NULL CHECK(protocol IN ('TCP', 'UDP')),
              source_address TEXT NOT NULL,
              destination_address TEXT NOT NULL,
              source_port INTEGER NOT NULL,
              destination_port INTEGER NOT NULL,
              wire_length INTEGER NOT NULL,
              payload_length INTEGER NOT NULL,
              payload_preview_hex TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS packet_history_time ON packet_history(captured_at);
            DELETE FROM packet_history;
            """
        )
        inserted = 0
        for timestamp, wire_length, packet in packets(capture):
            decoded = decode_packet(packet)
            if decoded is None:
                continue
            protocol, source, destination, source_port, destination_port, payload = decoded
            connection.execute(
                """INSERT INTO packet_history
                   (captured_at, protocol, source_address, destination_address,
                    source_port, destination_port, wire_length, payload_length,
                    payload_preview_hex) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""",
                (
                    timestamp,
                    protocol,
                    source,
                    destination,
                    source_port,
                    destination_port,
                    wire_length,
                    len(payload),
                    payload[:preview_bytes].hex(),
                ),
            )
            inserted += 1
            if inserted % 256 == 0:
                connection.execute(
                    "DELETE FROM packet_history WHERE id <= "
                    "COALESCE((SELECT MAX(id) - ? FROM packet_history), 0)",
                    (max_records,),
                )
                connection.commit()
        connection.execute(
            "DELETE FROM packet_history WHERE id <= "
            "COALESCE((SELECT MAX(id) - ? FROM packet_history), 0)",
            (max_records,),
        )
        connection.commit()
        retained = connection.execute("SELECT COUNT(*) FROM packet_history").fetchone()[0]
        connection.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    finally:
        connection.close()
    os.chmod(database, 0o600)
    return retained


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("database", type=Path)
    parser.add_argument("--max-records", type=int, default=10_000)
    parser.add_argument("--preview-bytes", type=int, default=64)
    args = parser.parse_args()
    if not 1 <= args.max_records <= 1_000_000:
        parser.error("--max-records must be from 1 through 1000000")
    if not 0 <= args.preview_bytes <= 1024:
        parser.error("--preview-bytes must be from 0 through 1024")
    if not args.capture.is_file():
        parser.error("capture must be a readable regular file")
    try:
        retained = index_capture(args.capture, args.database, args.max_records, args.preview_bytes)
    except (OSError, sqlite3.Error, ValueError) as error:
        parser.error(str(error))
    print(f"indexed {retained} TCP/UDP packets in {args.database}")


if __name__ == "__main__":
    main()
