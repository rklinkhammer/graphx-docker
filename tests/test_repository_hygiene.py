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
    git_files = subprocess.run(
        ["git", "ls-files", "-z"], cwd=root, capture_output=True
    )
    repository_files = (
        [encoded.decode("utf-8") for encoded in git_files.stdout.split(b"\0") if encoded]
        if git_files.returncode == 0
        else [path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()]
    )

    violations: list[str] = []
    for relative in repository_files:
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

    ci = (root / ".github/workflows/ci.yml").read_text(encoding="utf-8")
    if "apt.llvm.org" in ci:
        raise AssertionError("CI duplicates the toolchain installation owned by the verifier image")
    for mode in ("quality", "sanitizers", "fuzz"):
        invocation = f"scripts/test-linux-container.sh {mode}"
        if ci.count(invocation) != 1:
            raise AssertionError(f"CI must contain one authoritative {mode} invocation")

    print("repository hygiene checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
