#!/usr/bin/env python3
"""Verify that the shared Node.js preflight accepts only major version 26."""

from __future__ import annotations

import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile


def run(preflight: Path, version: str) -> subprocess.CompletedProcess[str]:
    with tempfile.TemporaryDirectory(prefix="graphx-node-version-") as temporary:
        node = Path(temporary) / "node"
        node.write_text(f"#!/bin/sh\nprintf '%s\\n' '{version}'\n", encoding="utf-8")
        node.chmod(node.stat().st_mode | stat.S_IXUSR)
        return subprocess.run(
            ["/bin/bash", preflight],
            check=False,
            capture_output=True,
            text=True,
            env={**os.environ, "PATH": temporary},
        )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_node_version.py SOURCE_ROOT")
    preflight = Path(sys.argv[1]).resolve() / "scripts/require-node26.sh"

    accepted = run(preflight, "26.8.1")
    if accepted.returncode != 0:
        raise AssertionError(f"Node.js 26 was rejected: {accepted.stderr}")

    for version in ("22.19.0", "24.20.0", "25.0.0", "27.0.0"):
        rejected = run(preflight, version)
        if rejected.returncode != 2 or "requires Node.js 26.x" not in rejected.stderr:
            raise AssertionError(f"a non-26 Node.js major was not rejected clearly: {version}")

    print("Node.js version preflight checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
