#!/usr/bin/env python3
"""Shared, dependency-free release validation helpers."""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import stat
import tarfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

VERSION_PATTERN = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
DEPENDENCY_VERSION_PATTERN = re.compile(r"^[0-9][0-9A-Za-z.+_-]{0,63}$")
MAX_LOCK_BYTES = 32 * 1024 * 1024
MAX_PACKAGE_JSON_BYTES = 1024 * 1024
MAX_PACKAGES = 10_000
MAX_ARCHIVE_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_MEMBER_BYTES = 256 * 1024 * 1024
MAX_ARCHIVE_EXPANDED_BYTES = 512 * 1024 * 1024
MAX_ARCHIVE_MEMBERS = 10_000
MAX_RELEASE_EPOCH = 4_102_444_800  # 2100-01-01T00:00:00Z
SUPPORTED_PLATFORMS = {
    "darwin-aarch64", "darwin-x86_64", "linux-aarch64", "linux-x86_64",
}


class ReleaseError(RuntimeError):
    """An actionable release-contract failure."""


@dataclass(frozen=True)
class ArchiveInspection:
    names: tuple[str, ...]
    regular_files: frozenset[str]
    regular_file_modes: dict[str, int]
    captured: dict[str, bytes]


ARCHIVE_EXECUTABLE_FILES = frozenset({
    "bin/graphx",
    "bin/graphx-generator",
    "bin/graphx-transform",
    "bin/graphx-sink",
    "libexec/graphx/graphx-extcap",
})

ARCHIVE_PUBLIC_HEADERS = frozenset({
    "include/graphx/capture.hpp",
    "include/graphx/config.hpp",
    "include/graphx/envelope.hpp",
    "include/graphx/framing.hpp",
    "include/graphx/in_process_transport.hpp",
    "include/graphx/infra.hpp",
    "include/graphx/network.hpp",
    "include/graphx/node.hpp",
    "include/graphx/observability.hpp",
    "include/graphx/shared_memory_transport.hpp",
    "include/graphx/tcp_transport.hpp",
    "include/graphx/transport.hpp",
    "include/graphx/transport_factory.hpp",
    "include/graphx/types.hpp",
    "include/graphx/unix_domain_socket_transport.hpp",
    "include/graphx/version.hpp",
})

ARCHIVE_DOCUMENTATION_FILES = frozenset({
    "share/doc/graphx/docs/GraphX_Phases_1_to_7_Linux_Implementation_and_Verification_Runbook.md",
    "share/doc/graphx/docs/adr/0001-authoritative-configuration.md",
    "share/doc/graphx/docs/adr/0002-network-infrastructure-layer.md",
    "share/doc/graphx/docs/adr/0003-typed-receive-and-bounded-runtime.md",
    "share/doc/graphx/docs/adr/0004-envelope-v2-identities-and-compatibility.md",
    "share/doc/graphx/docs/adr/0005-quality-gates-and-fuzzing.md",
    "share/doc/graphx/docs/adr/0006-phase-5-security-boundaries.md",
    "share/doc/graphx/docs/adr/0007-centralized-bounded-otlp-and-operational-health.md",
    "share/doc/graphx/docs/adr/0008-isolated-sqlite-telemetry-history.md",
    "share/doc/graphx/docs/adr/0009-authorized-runtime-control-plane.md",
    "share/doc/graphx/docs/adr/0010-bounded-pcapng-wireshark-extcap.md",
    "share/doc/graphx/docs/adr/0011-immutable-release-artifacts-and-explicit-compatibility.md",
    "share/doc/graphx/docs/capture.md",
    "share/doc/graphx/docs/compatibility-policy.md",
    "share/doc/graphx/docs/complete-system-demo.md",
    "share/doc/graphx/docs/control-plane.md",
    "share/doc/graphx/docs/history.md",
    "share/doc/graphx/docs/in-process-transport.md",
    "share/doc/graphx/docs/network-infrastructure.md",
    "share/doc/graphx/docs/observability.md",
    "share/doc/graphx/docs/protocol.md",
    "share/doc/graphx/docs/release-process.md",
    "share/doc/graphx/docs/runtime-lifecycle.md",
    "share/doc/graphx/docs/security.md",
    "share/doc/graphx/docs/shared-memory-transport.md",
    "share/doc/graphx/docs/tcp-transport.md",
    "share/doc/graphx/docs/test-procedure.md",
    "share/doc/graphx/docs/unix-domain-socket-transport.md",
    "share/doc/graphx/docs/upgrade.md",
})

ARCHIVE_DATA_FILES = frozenset({
    "lib/libgraphx.a",
    "lib/libyaml-cpp.a",
    "lib/cmake/GraphX/GraphXConfig.cmake",
    "lib/cmake/GraphX/GraphXConfigVersion.cmake",
    "lib/cmake/GraphX/GraphXTargets.cmake",
    "lib/cmake/GraphX/GraphXTargets-release.cmake",
    "share/graphx/graphx.yaml",
    "share/graphx/schema/graphx.schema.json",
    "share/graphx/wireshark/graphx.lua",
    "share/graphx/wireshark/README.md",
    "share/doc/graphx/LICENSE",
    "share/doc/graphx/THIRD_PARTY.md",
    "share/doc/graphx/README.md",
    "share/doc/graphx/VERSION",
    "share/doc/graphx/CHANGELOG.md",
    "share/doc/graphx/SECURITY.md",
    "share/doc/graphx/SUPPORT.md",
    "share/doc/graphx/CONTRIBUTING.md",
    "share/doc/graphx/licenses/yaml-cpp-LICENSE",
})


def archive_file_contract(version: str, platform_value: str) -> dict[str, int]:
    """Return the authoritative regular-file path and portable-mode contract."""
    root = f"graphx-{version}-{platform_value}"
    data_files = ARCHIVE_PUBLIC_HEADERS | ARCHIVE_DOCUMENTATION_FILES | ARCHIVE_DATA_FILES
    contract = {f"{root}/{path}": 0o644 for path in data_files}
    contract.update({f"{root}/{path}": 0o755 for path in ARCHIVE_EXECUTABLE_FILES})
    return contract


def parse_version(value: str) -> str:
    value = value.strip()
    if not VERSION_PATTERN.fullmatch(value):
        raise ReleaseError("version must be canonical MAJOR.MINOR.PATCH without leading zeros")
    return value


def bounded_bytes(path: Path, limit: int, label: str) -> bytes:
    if path.is_symlink():
        raise ReleaseError(f"{label} must be a regular file, not a symbolic link")
    try:
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NONBLOCK", 0)
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        descriptor = os.open(path, flags)
        with os.fdopen(descriptor, "rb") as stream:
            metadata = os.fstat(stream.fileno())
            if not stat.S_ISREG(metadata.st_mode):
                raise ReleaseError(f"{label} must be a regular file")
            if metadata.st_size > limit:
                raise ReleaseError(f"{label} exceeds the {limit}-byte release parser limit")
            value = stream.read(limit + 1)
    except FileNotFoundError as error:
        raise ReleaseError(f"{label} is missing") from error
    if len(value) > limit:
        raise ReleaseError(f"{label} exceeds the {limit}-byte release parser limit")
    return value


def json_object(path: Path, limit: int, label: str) -> dict:
    try:
        value = json.loads(bounded_bytes(path, limit, label).decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ReleaseError(f"{label} is not valid UTF-8 JSON") from error
    if not isinstance(value, dict):
        raise ReleaseError(f"{label} must contain a JSON object")
    return value


def source_version(source: Path) -> str:
    version_file = source / "VERSION"
    try:
        version = parse_version(bounded_bytes(version_file, 64, "VERSION").decode("utf-8"))
    except UnicodeDecodeError as error:
        raise ReleaseError("VERSION must be valid UTF-8") from error
    for relative in ("apps/telemetry/package.json", "web/package.json"):
        document = json_object(source / relative, MAX_PACKAGE_JSON_BYTES, relative)
        if document.get("version") != version:
            raise ReleaseError(f"{relative} version does not match VERSION")
    for relative in ("apps/telemetry/package-lock.json", "web/package-lock.json"):
        document = json_object(source / relative, MAX_LOCK_BYTES, relative)
        if document.get("version") != version:
            raise ReleaseError(f"{relative} version does not match VERSION")
        entries = document.get("packages")
        if not isinstance(entries, dict) or len(entries) > MAX_PACKAGES:
            raise ReleaseError(f"{relative} has an invalid or oversized package map")
        root = entries.get("")
        if not isinstance(root, dict) or root.get("version") != version:
            raise ReleaseError(f"{relative} root package version does not match VERSION")
    return version


def validate_tag(tag: str, version: str) -> None:
    if tag != f"v{version}":
        raise ReleaseError(f"release tag must be exactly v{version}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def bounded_regular_file_size(path: Path, limit: int, label: str) -> int:
    if path.is_symlink():
        raise ReleaseError(f"{label} must be a regular file, not a symbolic link")
    try:
        metadata = path.stat()
    except FileNotFoundError as error:
        raise ReleaseError(f"{label} is missing") from error
    if not stat.S_ISREG(metadata.st_mode):
        raise ReleaseError(f"{label} must be a regular file")
    if metadata.st_size > limit:
        raise ReleaseError(f"{label} exceeds the {limit}-byte release limit")
    return metadata.st_size


def inspect_archive(path: Path, *, captures: dict[str, int] | None = None,
                    max_archive_bytes: int = MAX_ARCHIVE_BYTES,
                    max_member_bytes: int = MAX_ARCHIVE_MEMBER_BYTES,
                    max_expanded_bytes: int = MAX_ARCHIVE_EXPANDED_BYTES,
                    max_members: int = MAX_ARCHIVE_MEMBERS) -> ArchiveInspection:
    bounded_regular_file_size(path, max_archive_bytes, "release archive")
    names: list[str] = []
    normalized_names: set[str] = set()
    regular_files: set[str] = set()
    regular_file_modes: dict[str, int] = {}
    captured: dict[str, bytes] = {}
    expanded_bytes = 0
    captures = captures or {}
    try:
        with tarfile.open(path, "r:gz") as archive:
            for member in archive:
                if len(names) >= max_members:
                    raise ReleaseError(f"release archive exceeds the {max_members}-member limit")
                raw_name = member.name
                name = PurePosixPath(raw_name)
                normalized = str(name)
                if (not raw_name or "\\" in raw_name or name.is_absolute() or
                        normalized in ("", ".") or ".." in name.parts or
                        normalized != raw_name.rstrip("/") or
                        not (member.isfile() or member.isdir())):
                    raise ReleaseError(f"unsafe archive member: {member.name}")
                if normalized in normalized_names:
                    raise ReleaseError(f"duplicate archive member: {member.name}")
                if any(str(parent) in regular_files for parent in name.parents
                       if str(parent) != "."):
                    raise ReleaseError(f"archive file/directory collision: {member.name}")
                if member.isfile() and any(
                        existing.startswith(f"{normalized}/") for existing in normalized_names):
                    raise ReleaseError(f"archive file/directory collision: {member.name}")
                if member.isfile():
                    if member.size > max_member_bytes:
                        raise ReleaseError(
                            f"archive member exceeds the {max_member_bytes}-byte limit: {member.name}")
                    expanded_bytes += member.size
                    if expanded_bytes > max_expanded_bytes:
                        raise ReleaseError(
                            f"release archive exceeds the {max_expanded_bytes}-byte expanded limit")
                    regular_files.add(normalized)
                    regular_file_modes[normalized] = member.mode & 0o7777
                    if normalized in captures:
                        limit = captures[normalized]
                        stream = archive.extractfile(member)
                        if stream is None:
                            raise ReleaseError(f"archive member cannot be read: {member.name}")
                        payload = stream.read(limit + 1)
                        if len(payload) > limit:
                            raise ReleaseError(
                                f"archive member exceeds the {limit}-byte content limit: {member.name}")
                        captured[normalized] = payload
                normalized_names.add(normalized)
                names.append(normalized)
    except (tarfile.TarError, EOFError) as error:
        raise ReleaseError("release archive is not a valid gzip tar file") from error
    return ArchiveInspection(tuple(names), frozenset(regular_files), regular_file_modes,
                             captured)


def safe_archive_members(path: Path, **limits: int) -> list[str]:
    return list(inspect_archive(path, **limits).names)


def required_archive_files(version: str, platform_value: str) -> frozenset[str]:
    return frozenset(archive_file_contract(version, platform_value))


def verify_release_archive(path: Path, version: str, platform_value: str) -> list[str]:
    root = f"graphx-{version}-{platform_value}"
    version_name = f"{root}/share/doc/graphx/VERSION"
    inspection = inspect_archive(path, captures={version_name: 64})
    if any(name != root and not name.startswith(f"{root}/") for name in inspection.names):
        raise ReleaseError("release archive contains an unexpected top-level path")
    contract = archive_file_contract(version, platform_value)
    required = frozenset(contract)
    missing = sorted(required - inspection.regular_files)
    if missing:
        raise ReleaseError(f"release archive is missing required file: {missing[0]}")
    unexpected = sorted(inspection.regular_files - required)
    if unexpected:
        raise ReleaseError(f"release archive contains unexpected file: {unexpected[0]}")
    for name, expected_mode in sorted(contract.items()):
        actual_mode = inspection.regular_file_modes[name]
        if actual_mode != expected_mode:
            raise ReleaseError(
                f"release archive file mode is {actual_mode:04o}, expected "
                f"{expected_mode:04o}: {name}")
    payload = inspection.captured.get(version_name)
    if payload is None:
        raise ReleaseError("release archive has no bounded VERSION file")
    try:
        archived_version = payload.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ReleaseError("release archive VERSION is not valid UTF-8") from error
    if parse_version(archived_version) != version:
        raise ReleaseError("release archive VERSION does not match the manifest")
    return list(inspection.names)


def validate_commit(value: object) -> str:
    if not isinstance(value, str) or not COMMIT_PATTERN.fullmatch(value):
        raise ReleaseError("commit must be a full lowercase 40-character Git object ID")
    return value


def validate_dependency_version(value: object, label: str) -> str:
    if not isinstance(value, str) or not DEPENDENCY_VERSION_PATTERN.fullmatch(value):
        raise ReleaseError(f"{label} must be a bounded canonical dependency version")
    return value


def validate_platform(value: object) -> str:
    if not isinstance(value, str) or value not in SUPPORTED_PLATFORMS:
        raise ReleaseError("platform is not a supported canonical release platform")
    return value


def current_platform() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "arm64": "aarch64"}.get(machine, machine)
    return validate_platform(f"{system}-{machine}")


def validate_epoch(value: object) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= MAX_RELEASE_EPOCH:
        raise ReleaseError("sourceDateEpoch must be an integer between 0 and 4102444800")
    return value


def spdx_id(name: str, version: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9.-]", "-", f"{name}-{version}")
    suffix = hashlib.sha256(f"{name}@{version}".encode()).hexdigest()[:12]
    return f"SPDXRef-Package-{safe[:80]}-{suffix}"
