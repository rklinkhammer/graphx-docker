#!/usr/bin/env python3
"""Verify a GraphX candidate against a trusted release identity."""

from __future__ import annotations

import argparse
import datetime as dt
import re
import subprocess
import sys
from pathlib import Path

from release_common import (
    MAX_ARCHIVE_BYTES,
    ReleaseError,
    SHA256_PATTERN,
    bounded_bytes,
    bounded_regular_file_size,
    current_platform,
    json_object,
    parse_version,
    sha256_file,
    source_version,
    spdx_id,
    validate_commit,
    validate_dependency_version,
    validate_epoch,
    validate_platform,
    validate_tag,
    verify_release_archive,
)

SHA256_LINE = re.compile(r"^([0-9a-f]{64})  ([^/\\]+)$")
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_SBOM_BYTES = 16 * 1024 * 1024
MAX_CHECKSUM_BYTES = 64 * 1024


def git(source: Path, *arguments: str) -> str:
    return subprocess.check_output(["git", *arguments], cwd=source, text=True).strip()


def require_keys(value: dict, expected: set[str], label: str) -> None:
    if set(value) != expected:
        raise ReleaseError(f"{label} has missing or unsupported fields")


def verify_native_sbom(document: dict, version: str, commit: str, platform_value: str,
                       archive_digest: str, created: str) -> None:
    require_keys(document, {"spdxVersion", "dataLicense", "SPDXID", "name",
                            "documentNamespace", "creationInfo", "packages",
                            "relationships"}, "SPDX document")
    stem = f"graphx-{version}-{platform_value}"
    expected_namespace = (
        f"https://github.com/rklinkhammer/graphx-docker/releases/tag/v{version}/"
        f"spdx/{commit}/{platform_value}/{archive_digest}"
    )
    if (document.get("spdxVersion") != "SPDX-2.3" or
            document.get("dataLicense") != "CC0-1.0" or
            document.get("SPDXID") != "SPDXRef-DOCUMENT" or
            document.get("name") != stem or
            document.get("documentNamespace") != expected_namespace or
            document.get("creationInfo") != {
                "created": created, "creators": ["Tool: graphx-release/1"]}):
        raise ReleaseError("SPDX document identity does not match the release manifest")

    packages = document.get("packages")
    if not isinstance(packages, list) or len(packages) != 3 or not all(
            isinstance(package, dict) for package in packages):
        raise ReleaseError("native SPDX document must describe GraphX, yaml-cpp, and OpenSSL")
    package_by_name = {package.get("name"): package for package in packages}
    if set(package_by_name) != {"GraphX", "yaml-cpp", "OpenSSL"}:
        raise ReleaseError("native SPDX package scope does not match native archive contents")
    graphx = package_by_name["GraphX"]
    if (graphx.get("SPDXID") != "SPDXRef-Package-GraphX" or
            graphx.get("versionInfo") != version or
            graphx.get("downloadLocation") != "NOASSERTION" or
            graphx.get("filesAnalyzed") is not False or
            graphx.get("licenseConcluded") != "MIT" or
            graphx.get("licenseDeclared") != "MIT" or
            graphx.get("primaryPackagePurpose") != "APPLICATION" or
            graphx.get("checksums") != [{
                "algorithm": "SHA256", "checksumValue": archive_digest}]):
        raise ReleaseError("SPDX GraphX package identity does not match the archive")
    yaml = package_by_name["yaml-cpp"]
    yaml_id = spdx_id("yaml-cpp", "0.9.0")
    if (yaml.get("SPDXID") != yaml_id or yaml.get("versionInfo") != "0.9.0" or
            yaml.get("downloadLocation") != "NOASSERTION" or
            yaml.get("filesAnalyzed") is not False or
            yaml.get("licenseConcluded") != "NOASSERTION" or
            yaml.get("licenseDeclared") != "MIT" or
            yaml.get("primaryPackagePurpose") != "LIBRARY"):
        raise ReleaseError("SPDX yaml-cpp package identity is invalid")
    openssl = package_by_name["OpenSSL"]
    openssl_version = validate_dependency_version(openssl.get("versionInfo"), "OpenSSL version")
    openssl_id = spdx_id("OpenSSL", openssl_version)
    if (openssl.get("SPDXID") != openssl_id or
            openssl.get("downloadLocation") != "NOASSERTION" or
            openssl.get("filesAnalyzed") is not False or
            openssl.get("licenseConcluded") != "NOASSERTION" or
            openssl.get("licenseDeclared") != "Apache-2.0" or
            openssl.get("primaryPackagePurpose") != "LIBRARY"):
        raise ReleaseError("SPDX OpenSSL package identity is invalid")
    expected_relationships = [
        {"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES",
         "relatedSpdxElement": "SPDXRef-Package-GraphX"},
        {"spdxElementId": "SPDXRef-Package-GraphX", "relationshipType": "DEPENDS_ON",
         "relatedSpdxElement": yaml_id},
        {"spdxElementId": "SPDXRef-Package-GraphX", "relationshipType": "DEPENDS_ON",
         "relatedSpdxElement": openssl_id},
    ]
    if document.get("relationships") != expected_relationships:
        raise ReleaseError("SPDX relationships do not match the native package contract")


def verify_candidate(directory: Path, expected_tag: str, expected_commit: str,
                     expected_platform: str, expected_epoch: int) -> None:
    directory = directory.resolve()
    if not directory.is_dir():
        raise ReleaseError("release candidate directory is missing")
    if not expected_tag.startswith("v"):
        raise ReleaseError("expected tag must start with v")
    version = parse_version(expected_tag[1:])
    validate_tag(expected_tag, version)
    expected_commit = validate_commit(expected_commit)
    expected_platform = validate_platform(expected_platform)
    expected_epoch = validate_epoch(expected_epoch)
    stem = f"graphx-{version}-{expected_platform}"
    names = {
        "archive": f"{stem}.tar.gz",
        "sbom": f"{stem}.spdx.json",
        "manifest": f"{stem}.manifest.json",
        "checksum": f"{stem}.sha256",
    }
    entries: list[Path] = []
    for entry in directory.iterdir():
        entries.append(entry)
        if len(entries) > 4:
            raise ReleaseError("release directory contains unexpected entries")
    if len(entries) != 4 or {entry.name for entry in entries} != set(names.values()):
        raise ReleaseError("release directory must contain exactly the four expected candidate files")
    if any(entry.is_symlink() or not entry.is_file() for entry in entries):
        raise ReleaseError("release candidate entries must be regular files")

    manifest_path = directory / names["manifest"]
    manifest = json_object(manifest_path, MAX_MANIFEST_BYTES, "release manifest")
    require_keys(manifest, {"schemaVersion", "product", "version", "tag", "commit",
                            "sourceDateEpoch", "platform", "artifacts"}, "release manifest")
    if (manifest.get("schemaVersion") != 2 or manifest.get("product") != "graphx" or
            manifest.get("version") != version or manifest.get("tag") != expected_tag or
            manifest.get("commit") != expected_commit or
            manifest.get("platform") != expected_platform or
            manifest.get("sourceDateEpoch") != expected_epoch):
        raise ReleaseError("release manifest does not match the trusted release identity")

    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != 2 or not all(
            isinstance(entry, dict) for entry in artifacts):
        raise ReleaseError("manifest must describe one archive and one SBOM")
    expected_artifacts = {
        names["archive"]: "application/gzip",
        names["sbom"]: "application/spdx+json",
    }
    artifact_paths: dict[str, Path] = {}
    for entry in artifacts:
        require_keys(entry, {"name", "mediaType", "sha256", "size"}, "artifact entry")
        name = entry.get("name")
        if not isinstance(name, str) or name not in expected_artifacts or name in artifact_paths:
            raise ReleaseError("manifest artifact name does not match the release identity")
        if entry.get("mediaType") != expected_artifacts[name]:
            raise ReleaseError(f"artifact media type mismatch: {name}")
        size = entry.get("size")
        digest = entry.get("sha256")
        path = directory / name
        limit = MAX_ARCHIVE_BYTES if name == names["archive"] else MAX_SBOM_BYTES
        if (isinstance(size, bool) or not isinstance(size, int) or size <= 0 or
                not isinstance(digest, str) or not SHA256_PATTERN.fullmatch(digest) or
                bounded_regular_file_size(path, limit, name) != size or
                sha256_file(path) != digest):
            raise ReleaseError(f"artifact identity mismatch: {name}")
        artifact_paths[name] = path
    if set(artifact_paths) != set(expected_artifacts):
        raise ReleaseError("manifest artifact set is incomplete")

    archive = artifact_paths[names["archive"]]
    archive_digest = sha256_file(archive)
    verify_release_archive(archive, version, expected_platform)
    created = dt.datetime.fromtimestamp(expected_epoch, dt.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%SZ")
    document = json_object(artifact_paths[names["sbom"]], MAX_SBOM_BYTES, "SPDX document")
    verify_native_sbom(document, version, expected_commit, expected_platform,
                       archive_digest, created)

    checksum_path = directory / names["checksum"]
    try:
        lines = bounded_bytes(checksum_path, MAX_CHECKSUM_BYTES, "checksum file").decode(
            "utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise ReleaseError("checksum file is not valid UTF-8") from error
    expected: dict[str, str] = {}
    for line in lines:
        match = SHA256_LINE.fullmatch(line)
        if match is None or match.group(2) in expected:
            raise ReleaseError("checksum file contains a malformed or duplicate entry")
        expected[match.group(2)] = match.group(1)
    required = {names["archive"], names["sbom"], names["manifest"]}
    if set(expected) != required:
        raise ReleaseError("checksum file must describe exactly the archive, SBOM, and manifest")
    for name in required:
        if expected[name] != sha256_file(directory / name):
            raise ReleaseError(f"checksum mismatch: {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--source", type=Path,
                        help="derive trusted identity from this checked-out source tree")
    parser.add_argument("--expected-tag")
    parser.add_argument("--expected-commit")
    parser.add_argument("--expected-platform")
    parser.add_argument("--expected-epoch", type=int)
    args = parser.parse_args()
    explicit = (args.expected_tag, args.expected_commit, args.expected_platform,
                args.expected_epoch)
    if args.source is not None:
        if any(value is not None for value in explicit):
            raise ReleaseError("--source cannot be combined with explicit expected identity")
        source = args.source.resolve()
        version = source_version(source)
        expected_tag = f"v{version}"
        expected_commit = git(source, "rev-parse", "HEAD")
        expected_platform = current_platform()
        expected_epoch = int(git(source, "show", "-s", "--format=%ct", "HEAD"))
    else:
        if any(value is None for value in explicit):
            raise ReleaseError("provide --source or all four --expected-* identity values")
        expected_tag, expected_commit, expected_platform, expected_epoch = explicit
    verify_candidate(args.directory, str(expected_tag), str(expected_commit),
                     str(expected_platform), int(expected_epoch))
    print(f"GraphX {str(expected_tag)[1:]} {expected_platform} release candidate verified")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ReleaseError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"graphx-release-verify: {error}", file=sys.stderr)
        sys.exit(2)
