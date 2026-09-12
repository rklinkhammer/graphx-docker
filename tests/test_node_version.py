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


def selection(root: Path, current: str, installed: str | None):
    with tempfile.TemporaryDirectory(prefix="graphx-node-selection-") as temporary:
        directory = Path(temporary)
        tools = directory / "tools"
        tools.mkdir()
        prefix = directory / "Node 24"
        (prefix / "bin").mkdir(parents=True)
        def executable(path: Path, output: str):
            path.write_text(f"#!/bin/sh\nprintf '%s\\n' '{output}'\n", encoding="utf-8")
            path.chmod(0o755)
        executable(tools / "node", current)
        executable(tools / "npm", "default-npm")
        executable(tools / "brew", str(prefix))
        if installed:
            executable(prefix / "bin/node", installed)
            executable(prefix / "bin/npm", "node24-npm")
        result = subprocess.run(["/bin/bash", "-ec",
            'ROOT=$1; source "$ROOT/scripts/test-feature-common.sh"; require_node24; '
            "node -p 'process.versions.node'; npm --version", "test", root],
            capture_output=True, text=True, env={**os.environ, "PATH": f"{tools}:/usr/bin:/bin"})
        return result


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

    root = preflight.parent.parent
    selected = selection(root, "26.8.1", "24.21.0")
    if selected.returncode or not selected.stdout.endswith("24.21.0\nnode24-npm\n"):
        raise AssertionError(f"Node 24 toolchain was not selected: {selected}")
    preserved = selection(root, "24.19.0", "24.21.0")
    if preserved.returncode or preserved.stdout != "24.19.0\ndefault-npm\n":
        raise AssertionError("an already selected Node 24 toolchain was replaced")
    for installed in (None, "26.8.1"):
        missing = selection(root, "26.8.1", installed)
        if missing.returncode != 2 or "brew install node@24" not in missing.stderr:
            raise AssertionError("missing/invalid Node 24 fallback did not fail with installation guidance")
    print("Node.js version and toolchain selection checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
