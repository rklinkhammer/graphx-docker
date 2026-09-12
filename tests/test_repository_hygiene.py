#!/usr/bin/env python3
"""Reject generated runtime artifacts accidentally added to the repository."""

from __future__ import annotations

from pathlib import Path, PurePosixPath
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_repository_hygiene.py SOURCE_ROOT")
    root = Path(sys.argv[1]).resolve()
    tracked = subprocess.run(
        ["git", "ls-files", "-z"], cwd=root, capture_output=True, check=True
    ).stdout.split(b"\0")

    violations: list[str] = []
    for encoded in tracked:
        if not encoded:
            continue
        relative = encoded.decode("utf-8")
        path = PurePosixPath(relative)
        if (
            path.parts[0] in {"outputs", "captures", "build"}
            or "node_modules" in path.parts
            or "__pycache__" in path.parts
            or ".state" in path.parts
            or path.name == ".DS_Store"
            or path.suffix == ".pyc"
        ):
            violations.append(relative)

    if violations:
        raise AssertionError(
            "generated runtime artifacts are tracked:\n" + "\n".join(violations)
        )
    print("repository hygiene checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
