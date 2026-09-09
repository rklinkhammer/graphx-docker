#!/usr/bin/env python3
"""Run one command with a wall-clock deadline and kill its process group."""

from __future__ import annotations

import os
import signal
import subprocess
import sys


def main() -> int:
    if len(sys.argv) < 3:
        raise SystemExit("usage: run-bounded.py SECONDS COMMAND [ARG ...]")
    try:
        seconds = int(sys.argv[1])
    except ValueError as exc:
        raise SystemExit("SECONDS must be an integer") from exc
    if not 1 <= seconds <= 3600:
        raise SystemExit("SECONDS must be between 1 and 3600")
    process = subprocess.Popen(sys.argv[2:], start_new_session=True)
    try:
        return process.wait(timeout=seconds)
    except subprocess.TimeoutExpired:
        print(f"timed out after {seconds}s: {sys.argv[2]}", file=sys.stderr)
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        return 124


if __name__ == "__main__":
    raise SystemExit(main())
