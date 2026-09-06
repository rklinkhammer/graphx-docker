#!/usr/bin/env python3
"""Bounded TCP result sink for the SDR example."""

from __future__ import annotations

import json
import os
import signal
import socket
import threading

from protocol import publish_heartbeat, recv_line

stop = threading.Event()


def main() -> None:
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("0.0.0.0", int(os.environ.get("SDR_RESULT_PORT", "18402"))))
        listener.listen(16)
        listener.settimeout(0.5)
        count = 0
        print("SDR result sink ready", flush=True)
        while not stop.is_set():
            try:
                connection, peer = listener.accept()
            except TimeoutError:
                continue
            with connection:
                connection.settimeout(2)
                try:
                    payload = recv_line(connection, 4096)
                    result = json.loads(payload)
                    required = {"sequence", "frequency_hz", "sample_count", "power"}
                    if not isinstance(result, dict) or set(result) != required:
                        raise ValueError("invalid result")
                    if not isinstance(result["sequence"], int) or \
                            not 0 <= result["sequence"] <= 0xFFFFFFFF:
                        raise ValueError("invalid sequence")
                    if not isinstance(result["frequency_hz"], int) or \
                            not 1_000_000 <= result["frequency_hz"] <= 6_000_000_000:
                        raise ValueError("invalid frequency")
                    if not isinstance(result["sample_count"], int) or \
                            not 1 <= result["sample_count"] <= 256:
                        raise ValueError("invalid sample count")
                    maximum_power = 2 * 32768 * 32768 * result["sample_count"]
                    if not isinstance(result["power"], int) or \
                            not 0 <= result["power"] <= maximum_power:
                        raise ValueError("invalid power")
                    count += 1
                    print(f"result {count} from={peer[0]} sequence={result['sequence']} "
                          f"power={result['power']}", flush=True)
                    publish_heartbeat("sink", count)
                except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                    print(f"sink rejected result: {type(error).__name__}", flush=True)


if __name__ == "__main__":
    main()
