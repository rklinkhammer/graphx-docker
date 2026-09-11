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
    if len(rows) != 18:
        fail(f"expected 18 ADR index rows, found {len(rows)}")
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
        "docs/adr/README.md", "docs/project-decisions.md",
        "docs/archive/README.md",
    ):
        if required not in readme:
            fail(f"README does not link {required}")

    agents = (root / "AGENTS.md").read_text(encoding="utf-8")
    decisions = (root / "docs/project-decisions.md").read_text(encoding="utf-8")
    manual = (root / "docs/manual-test-procedures.md").read_text(encoding="utf-8")
    procedure = (root / "docs/test-procedure.md").read_text(encoding="utf-8")
    complete_demo = (root / "docs/complete-system-demo.md").read_text(encoding="utf-8")
    if "docs/project-decisions.md" not in agents:
        fail("AGENTS.md does not route agents to the current decision guide")
    for required in ("OrbStack", "Lima", "system Open vSwitch", "Version 1"):
        if required not in decisions:
            fail(f"current decision guide omits {required}")
    for retired in (
        "Docker Desktop", "macos-up.sh", "macos-down.sh", "linux-up.sh",
        "linux-down.sh", "scripts/fault.sh", "compose.ovs.yaml",
        "graphx-ovs.yaml",
    ):
        if retired in manual:
            fail(f"manual procedure names retired runtime or launcher: {retired}")
    for required in (
        "docker context use orbstack", "docker info", "infrastructure/lima/start.sh",
        "scripts/network-lab.sh mixed-network up",
    ):
        if required not in manual:
            fail(f"manual procedure omits current command: {required}")
    if "docker context use orbstack" not in procedure or "docker info" not in procedure:
        fail("short test procedure does not preflight OrbStack")
    for required in (
        "Forwarded browser port versus Docker published port",
        'GRAPHX_ALLOWED_ORIGINS="http://localhost:18080,http://127.0.0.1:18080"',
        "browser-visible origin",
        "ssh -N -L 18080:127.0.0.1:8080 user@linux-host",
    ):
        if required not in complete_demo:
            fail(f"complete demo omits forwarded WebSocket guidance: {required}")
    normalized_manual = re.sub(r"\s+", " ", manual)
    if "GRAPHX_ALLOWED_ORIGINS" not in manual or "local port 18080" not in normalized_manual:
        fail("manual procedure omits forwarded WebSocket origin verification")
    demo_guide = (root / "docs/demo-guide.md").read_text(encoding="utf-8")
    for lab in ("macvlan", "ipvlan-l2", "ipvlan-l3", "mixed-network"):
        for action in ("plan", "up", "status", "down"):
            command = f"scripts/network-lab.sh {lab} {action}"
            if command not in demo_guide:
                fail(f"demo guide omits cross-platform network-lab command: {command}")
    if "OrbStack is not involved" not in demo_guide or "infrastructure/lima/verify.sh" not in demo_guide:
        fail("demo guide does not explain the macOS Lima network-lab boundary")
    root_history = [
        path.name for pattern in ("phase_*_handoff.md", "phase_*_verification.md", "migration_m*.md")
        for path in root.glob(pattern)
    ]
    if root_history:
        fail(f"historical phase records remain in repository root: {sorted(root_history)}")
    if not (root / "docs/archive/ovs-migration/migration_m8_reverification.md").is_file():
        fail("archive omits the terminal M8 verification record")
    print("documentation consistency checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
