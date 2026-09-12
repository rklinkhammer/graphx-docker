#!/usr/bin/env python3
"""Check documentation navigation and versioned public artifacts."""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys


def local_links(path: Path) -> list[str]:
    links = re.findall(r"\[[^]]*\]\(([^)]+)\)", path.read_text(encoding="utf-8"))
    return [
        link.split("#", 1)[0]
        for link in links
        if link and "://" not in link and not link.startswith("#")
    ]


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_documentation_consistency.py SOURCE_ROOT")
    root = Path(sys.argv[1]).resolve()
    version = (root / "VERSION").read_text(encoding="ascii").strip()
    tracked = {
        root / relative
        for relative in subprocess.check_output(
            ["git", "ls-files"], cwd=root, text=True
        ).splitlines()
    }

    adr_dir = root / "docs/adr"
    index = adr_dir / "README.md"
    rows = re.findall(
        r"^\| (\d{4}) \|.*\| \[`[^`]+`\]\(([^)]+)\) \|$",
        index.read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    numbers = [number for number, _ in rows]
    indexed = {filename for _, filename in rows}
    actual = {path.name for path in adr_dir.glob("[0-9][0-9][0-9][0-9]-*.md")}
    if not rows or numbers != sorted(set(numbers)) or indexed != actual:
        raise AssertionError("ADR index is incomplete, duplicated, or unordered")

    documents = [
        root / "README.md",
        root / "CONTRIBUTING.md",
        root / "SUPPORT.md",
        root / "SECURITY.md",
        *(
            path
            for path in (root / "docs").rglob("*.md")
            if "archive" not in path.parts and path in tracked
        ),
        *(path for path in (root / "examples").rglob("README.md") if path in tracked),
    ]
    for document in documents:
        for link in local_links(document):
            target = (document.parent / link).resolve()
            if not target.exists():
                raise AssertionError(
                    f"broken local link in {document.relative_to(root)}: {link}"
                )

    readme = (root / "README.md").read_text(encoding="utf-8")
    architecture = (root / "docs/GraphX_Architecture.md").read_text(encoding="utf-8")
    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    inventory = (root / "docs/release-license-inventory.md").read_text(encoding="utf-8")
    for label, condition in (
        ("README", f"GraphX {version}" in readme),
        ("architecture baseline", f"GraphX {version}" in architecture),
        ("changelog", f"## [{version}]" in changelog),
        ("license inventory", f"# GraphX {version}" in inventory),
    ):
        if not condition:
            raise AssertionError(f"{label} does not match VERSION")

    for relative in (
        "apps/telemetry/package.json",
        "apps/telemetry/package-lock.json",
        "web/package.json",
        "web/package-lock.json",
    ):
        package = json.loads((root / relative).read_text(encoding="utf-8"))
        if package.get("version") != version:
            raise AssertionError(f"{relative} does not match VERSION")
        if relative.endswith("package-lock.json") and package["packages"][""]["version"] != version:
            raise AssertionError(f"root package in {relative} does not match VERSION")

    print("documentation consistency checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
