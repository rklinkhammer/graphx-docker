#!/usr/bin/env python3
"""Print recent records from the QEMU packet-history database."""

from __future__ import annotations

import argparse
import datetime
import sqlite3
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--protocol", choices=("TCP", "UDP"))
    args = parser.parse_args()
    if not 1 <= args.limit <= 1000:
        parser.error("--limit must be from 1 through 1000")
    if not args.database.is_file():
        parser.error("database must be a readable regular file")

    query = (
        "SELECT captured_at, protocol, source_address, source_port, "
        "destination_address, destination_port, payload_length, payload_preview_hex "
        "FROM packet_history"
    )
    parameters: list[object] = []
    if args.protocol:
        query += " WHERE protocol = ?"
        parameters.append(args.protocol)
    query += " ORDER BY captured_at DESC LIMIT ?"
    parameters.append(args.limit)

    try:
        connection = sqlite3.connect(f"file:{args.database}?mode=ro", uri=True)
        rows = connection.execute(query, parameters).fetchall()
        connection.close()
    except sqlite3.Error as error:
        parser.error(str(error))
    for timestamp, protocol, source, source_port, destination, destination_port, length, preview in rows:
        instant = datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc).isoformat()
        print(
            f"{instant} {protocol} {source}:{source_port} -> "
            f"{destination}:{destination_port} payload={length} preview={preview}"
        )


if __name__ == "__main__":
    main()
