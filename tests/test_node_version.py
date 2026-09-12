#!/usr/bin/env python3
"""Verify that the shared Node.js preflight accepts only major version 24."""

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
    preflight = Path(sys.argv[1]).resolve() / "scripts/require-node24.sh"

    accepted = run(preflight, "24.19.0")
    if accepted.returncode != 0:
        raise AssertionError(f"Node.js 24 was rejected: {accepted.stderr}")

    rejected = run(preflight, "26.8.1")
    if rejected.returncode != 2 or "requires Node.js 24.x" not in rejected.stderr:
        raise AssertionError("a non-24 Node.js major was not rejected clearly")

    print("Node.js version preflight checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
