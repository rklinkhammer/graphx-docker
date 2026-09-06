#!/usr/bin/env python3
"""Check architecture navigation and the canonical ADR register."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys


def fail(message: str) -> None:
    raise AssertionError(message)


def local_links(path: Path) -> list[str]:
    links = re.findall(r"\[[^]]*\]\(([^)]+)\)", path.read_text(encoding="utf-8"))
    return [link.split("#", 1)[0] for link in links if link and "://" not in link and not link.startswith("#")]


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_documentation_consistency.py SOURCE_ROOT")
    root = Path(sys.argv[1]).resolve()
    adr_dir = root / "docs" / "adr"
    index = adr_dir / "README.md"
    index_text = index.read_text(encoding="utf-8")
    rows = re.findall(r"^\| (\d{4}) \|.*\| \[`[^`]+`\]\(([^)]+)\) \|$", index_text, re.MULTILINE)
    if len(rows) != 14:
        fail(f"expected 14 ADR index rows, found {len(rows)}")
    numbers = [number for number, _ in rows]
    if numbers != sorted(set(numbers)):
        fail("ADR index numbers are duplicate or unordered")
    indexed = {filename for _, filename in rows}
    actual = {path.name for path in adr_dir.glob("[0-9][0-9][0-9][0-9]-*.md")}
    if indexed != actual:
        fail(f"ADR index mismatch: missing={sorted(actual-indexed)} extra={sorted(indexed-actual)}")
    stale_references = []
    allowed = {index.resolve(), (root / "CHANGELOG.md").resolve(), Path(__file__).resolve()}
    tracked = subprocess.run(
        ["git", "ls-files"], cwd=root, text=True, capture_output=True, check=True
    ).stdout.splitlines()
    for relative in tracked:
        path = root / relative
        if not path.is_file() or "outputs" in path.parts:
            continue
        if path.name == "documentation_consistency_implementation_plan.md" or path.resolve() in allowed:
            continue
        if "0012-unified-qemu-profiles.md" in path.read_text(encoding="utf-8", errors="replace"):
            stale_references.append(str(path.relative_to(root)))
    if stale_references:
        fail(f"live repository content still references the old QEMU ADR path: {stale_references}")

    for document in (root / "README.md", root / "CONTRIBUTING.md", index):
        for link in local_links(document):
            if not (document.parent / link).resolve().exists():
                fail(f"broken local link in {document.relative_to(root)}: {link}")

    readme = (root / "README.md").read_text(encoding="utf-8")
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if f"GraphX {version}" not in readme or "framework scaffold" in readme:
        fail("README maturity text does not match VERSION")
    for required in (
        "docs/GraphX_Architecture.md", "docs/GraphX_Architecture.docx",
        "docs/adr/README.md", "verification_status.md",
    ):
        if required not in readme:
            fail(f"README does not link {required}")
    print("documentation consistency checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
