#!/usr/bin/env python3
"""Check architecture navigation and the canonical ADR register."""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile
from xml.etree import ElementTree


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
    if len(rows) != 17:
        fail(f"expected 17 ADR index rows, found {len(rows)}")
    numbers = [number for number, _ in rows]
    if numbers != sorted(set(numbers)):
        fail("ADR index numbers are duplicate or unordered")
    indexed = {filename for _, filename in rows}
    actual = {path.name for path in adr_dir.glob("[0-9][0-9][0-9][0-9]-*.md")}
    if indexed != actual:
        fail(f"ADR index mismatch: missing={sorted(actual-indexed)} extra={sorted(indexed-actual)}")
    stale_references: list[str] = []
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

    linked_documents = (
        root / "README.md",
        root / "CONTRIBUTING.md",
        root / "examples" / "README.md",
        root / "docs" / "GraphX_Architecture.md",
        root / "docs" / "demo-guide.md",
        root / "docs" / "manual-test-procedures.md",
        root / "docs" / "release-license-inventory.md",
        root / "docs" / "test-procedure.md",
        index,
    )
    for document in linked_documents:
        for link in local_links(document):
            if not (document.parent / link).resolve().exists():
                fail(f"broken local link in {document.relative_to(root)}: {link}")

    readme = (root / "README.md").read_text(encoding="utf-8")
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    if f"GraphX {version}" not in readme or "framework scaffold" in readme:
        fail("README maturity text does not match VERSION")
    architecture = (root / "docs" / "GraphX_Architecture.md").read_text(encoding="utf-8")
    if f"**Repository baseline:** GraphX {version}" not in architecture:
        fail("architecture baseline does not match VERSION")
    normalized_architecture = re.sub(r"\s+", " ", architecture)
    for required in (
        "M5 adds owned router namespaces",
        "semantic profile flows",
        "M6 adds GraphX-owned TAP lifecycle",
        "M7 adds bounded declarative Ethernet capture",
    ):
        if required not in normalized_architecture:
            fail(f"architecture does not describe the accepted M5 boundary: {required}")
    architecture_docx = root / "docs" / "GraphX_Architecture.docx"
    with zipfile.ZipFile(architecture_docx) as archive:
        document = ElementTree.fromstring(archive.read("word/document.xml"))
    word_namespace = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"
    docx_text = re.sub(r"\s+", " ", " ".join(
        node.text or "" for node in document.iter(f"{word_namespace}t")
    ))
    for required in ("M5 adds owned router namespaces", "M6 adds GraphX-owned TAP lifecycle"):
        if required not in docx_text:
            fail(f"editable architecture document is stale at M5: {required}")
    license_inventory = (root / "docs" / "release-license-inventory.md").read_text(
        encoding="utf-8")
    if f"# GraphX {version} release license inventory" not in license_inventory:
        fail("release license inventory does not match VERSION")
    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    if f"## [{version}]" not in changelog:
        fail("CHANGELOG does not contain the current VERSION")
    for relative in (
        "apps/telemetry/package.json",
        "apps/telemetry/package-lock.json",
        "web/package.json",
        "web/package-lock.json",
    ):
        package = json.loads((root / relative).read_text(encoding="utf-8"))
        if package.get("version") != version:
            fail(f"{relative} does not match VERSION")
        if relative.endswith("package-lock.json") and package.get("packages", {}).get(
                "", {}).get("version") != version:
            fail(f"root package in {relative} does not match VERSION")
    extcap = (root / "tools" / "graphx-extcap").read_text(encoding="utf-8")
    if f'VERSION = "{version}"' not in extcap:
        fail("extcap version does not match VERSION")
    qemu_package = (
        root / "examples" / "qemu-node" / "buildroot-external" / "package" /
        "graphx-qemu-node" / "graphx-qemu-node.mk"
    ).read_text(encoding="utf-8")
    if f"GRAPHX_QEMU_NODE_VERSION = {version}" not in qemu_package:
        fail("QEMU guest package version does not match VERSION")
    if "GRAPHX_QEMU_NODE_LICENSE = MIT" not in qemu_package:
        fail("QEMU guest package does not declare its MIT license")
    if "GRAPHX_QEMU_NODE_LICENSE_FILES = LICENSE" not in qemu_package:
        fail("QEMU guest package does not identify its license file")
    if (root / "examples" / "qemu-node" / "guest" / "LICENSE").read_text(
            encoding="utf-8").strip() != (root / "LICENSE").read_text(encoding="utf-8").strip():
        fail("QEMU guest package license does not match the project license")
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
