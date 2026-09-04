#!/usr/bin/env python3
"""Phase 10 release identity, bounds, SBOM, workflow, and archive tests."""

from __future__ import annotations

import datetime as dt
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

SOURCE = Path(sys.argv[1]).resolve()
CLI = Path(sys.argv[2]).resolve()
sys.path.insert(0, str(SOURCE / "scripts/release"))

from build_release import sbom  # noqa: E402
from ghcr_release import ReleaseError, assert_absent, delete_tag, tagged_versions  # noqa: E402
from release_common import (MAX_LOCK_BYTES, archive_file_contract,  # noqa: E402
                            bounded_regular_file_size, current_platform, inspect_archive,
                            parse_version, required_archive_files, safe_archive_members,
                            sha256_file, source_version, validate_tag,
                            verify_release_archive)
from verify_release import verify_candidate  # noqa: E402


def rejected(action) -> None:
    try:
        action()
    except ReleaseError:
        return
    raise AssertionError("invalid release input was accepted")


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def reseal(candidate: Path, stem: str) -> None:
    manifest_path = candidate / f"{stem}.manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for artifact in manifest["artifacts"]:
        path = candidate / artifact["name"]
        artifact["size"] = path.stat().st_size
        artifact["sha256"] = sha256_file(path)
    write_json(manifest_path, manifest)
    paths = [candidate / f"{stem}.tar.gz", candidate / f"{stem}.spdx.json", manifest_path]
    (candidate / f"{stem}.sha256").write_text("".join(
        f"{sha256_file(path)}  {path.name}\n" for path in sorted(paths)), encoding="utf-8")


def create_candidate(root: Path, version: str, commit: str, platform_value: str,
                     epoch: int, omitted: frozenset[str] = frozenset()) -> tuple[Path, str]:
    candidate = root / "candidate"
    candidate.mkdir(parents=True)
    stem = f"graphx-{version}-{platform_value}"
    archive_path = candidate / f"{stem}.tar.gz"
    contract = archive_file_contract(version, platform_value)
    with tarfile.open(archive_path, "w:gz") as archive:
        for name in sorted(required_archive_files(version, platform_value) - omitted):
            payload = (f"{version}\n".encode() if name.endswith("/VERSION") else b"test\n")
            member = tarfile.TarInfo(name)
            member.size = len(payload)
            member.mtime = epoch
            member.mode = contract[name]
            archive.addfile(member, io.BytesIO(payload))
    created = dt.datetime.fromtimestamp(epoch, dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    sbom_path = candidate / f"{stem}.spdx.json"
    write_json(sbom_path, sbom(version, commit, platform_value, archive_path, created,
                               "3.0.17"))
    manifest = {
        "schemaVersion": 2, "product": "graphx", "version": version,
        "tag": f"v{version}", "commit": commit, "sourceDateEpoch": epoch,
        "platform": platform_value,
        "artifacts": [
            {"name": archive_path.name, "mediaType": "application/gzip",
             "sha256": sha256_file(archive_path), "size": archive_path.stat().st_size},
            {"name": sbom_path.name, "mediaType": "application/spdx+json",
             "sha256": sha256_file(sbom_path), "size": sbom_path.stat().st_size},
        ],
    }
    write_json(candidate / f"{stem}.manifest.json", manifest)
    reseal(candidate, stem)
    return candidate, stem


version = source_version(SOURCE)
assert subprocess.check_output([CLI, "--version"], text=True) == f"graphx {version}\n"
validate_tag(f"v{version}", version)
for invalid in ("1", "1.2", "01.2.3", "1.02.3", "1.2.03", "1.2.3-rc.1",
                "v1.2.3", "1.2.3\n4"):
    rejected(lambda value=invalid: parse_version(value))
for invalid_tag in (version, f"release-{version}", f"v{version}.1", "v0.0.0"):
    rejected(lambda value=invalid_tag: validate_tag(value, version))

commit = "a" * 40
epoch = 1_700_000_000
platform_value = current_platform()

with tempfile.TemporaryDirectory(prefix="graphx-release-contract-") as temporary:
    root = Path(temporary)
    candidate, stem = create_candidate(root, version, commit, platform_value, epoch)
    verify_candidate(candidate, f"v{version}", commit, platform_value, epoch)

    alternate_platform = "darwin-aarch64" if platform_value != "darwin-aarch64" else "linux-x86_64"
    for field, invalid in (("commit", "0" * 40), ("platform", alternate_platform),
                           ("sourceDateEpoch", epoch + 1)):
        forged = root / f"forged-{field}"
        shutil.copytree(candidate, forged)
        manifest_path = forged / f"{stem}.manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest[field] = invalid
        write_json(manifest_path, manifest)
        reseal(forged, stem)
        rejected(lambda path=forged: verify_candidate(
            path, f"v{version}", commit, platform_value, epoch))

    forged_media = root / "forged-media"
    shutil.copytree(candidate, forged_media)
    manifest_path = forged_media / f"{stem}.manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["artifacts"][0]["mediaType"] = "text/plain"
    write_json(manifest_path, manifest)
    reseal(forged_media, stem)
    rejected(lambda: verify_candidate(
        forged_media, f"v{version}", commit, platform_value, epoch))

    forged_sbom = root / "forged-sbom"
    shutil.copytree(candidate, forged_sbom)
    sbom_path = forged_sbom / f"{stem}.spdx.json"
    document = json.loads(sbom_path.read_text(encoding="utf-8"))
    document["packages"].append({"name": "react", "SPDXID": "SPDXRef-react"})
    write_json(sbom_path, document)
    reseal(forged_sbom, stem)
    rejected(lambda: verify_candidate(
        forged_sbom, f"v{version}", commit, platform_value, epoch))

    malformed = root / "malformed"
    shutil.copytree(candidate, malformed)
    write_json(malformed / f"{stem}.manifest.json", [])
    rejected(lambda: verify_candidate(malformed, f"v{version}", commit, platform_value, epoch))

    missing = root / "missing-required"
    omitted = frozenset({next(iter(required_archive_files(version, platform_value)))})
    missing_candidate, _ = create_candidate(missing, version, commit, platform_value, epoch,
                                            omitted)
    rejected(lambda: verify_candidate(
        missing_candidate, f"v{version}", commit, platform_value, epoch))

with tempfile.TemporaryDirectory(prefix="graphx-release-archive-") as temporary:
    root = Path(temporary)
    malformed_archive = root / "malformed.tar.gz"
    malformed_archive.write_bytes(b"not a gzip tar")
    rejected(lambda: safe_archive_members(malformed_archive))

    unsafe = root / "unsafe.tar.gz"
    with tarfile.open(unsafe, "w:gz") as archive:
        member = tarfile.TarInfo("../escape")
        member.size = 1
        archive.addfile(member, io.BytesIO(b"x"))
    rejected(lambda: safe_archive_members(unsafe))

    duplicate = root / "duplicate.tar.gz"
    with tarfile.open(duplicate, "w:gz") as archive:
        for payload in (b"first", b"second"):
            member = tarfile.TarInfo("graphx/file")
            member.size = len(payload)
            archive.addfile(member, io.BytesIO(payload))
    rejected(lambda: safe_archive_members(duplicate))

    special = root / "special.tar.gz"
    with tarfile.open(special, "w:gz") as archive:
        member = tarfile.TarInfo("graphx/fifo")
        member.type = tarfile.FIFOTYPE
        archive.addfile(member)
    rejected(lambda: safe_archive_members(special))

    for index, members in enumerate((
            (("graphx/path", b"file"), ("graphx/path/child", b"child")),
            (("graphx/path/child", b"child"), ("graphx/path", b"file")))):
        collision = root / f"collision-{index}.tar.gz"
        with tarfile.open(collision, "w:gz") as archive:
            for name, payload in members:
                member = tarfile.TarInfo(name)
                member.size = len(payload)
                archive.addfile(member, io.BytesIO(payload))
        rejected(lambda path=collision: safe_archive_members(path))

    member_limit = root / "member-limit.tar.gz"
    with tarfile.open(member_limit, "w:gz") as archive:
        member = tarfile.TarInfo("graphx/file")
        member.size = 2
        archive.addfile(member, io.BytesIO(b"xx"))
    rejected(lambda: inspect_archive(member_limit, max_member_bytes=1))
    rejected(lambda: bounded_regular_file_size(member_limit, 1, "test archive"))

    expanded_limit = root / "expanded-limit.tar.gz"
    with tarfile.open(expanded_limit, "w:gz") as archive:
        for name in ("graphx/one", "graphx/two"):
            member = tarfile.TarInfo(name)
            member.size = 1
            archive.addfile(member, io.BytesIO(b"x"))
    rejected(lambda: inspect_archive(expanded_limit, max_expanded_bytes=1))

    valid_contract = root / "valid-contract.tar.gz"
    contract = archive_file_contract(version, platform_value)
    with tarfile.open(valid_contract, "w:gz") as archive:
        for name in sorted(required_archive_files(version, platform_value)):
            payload = (f"{version}\n".encode() if name.endswith("/VERSION") else b"test\n")
            member = tarfile.TarInfo(name)
            member.size = len(payload)
            member.mode = contract[name]
            archive.addfile(member, io.BytesIO(payload))
    verify_release_archive(valid_contract, version, platform_value)
    for index, omitted_name in enumerate(sorted(required_archive_files(
            version, platform_value))):
        omitted_archive = root / f"omitted-{index}.tar.gz"
        with tarfile.open(omitted_archive, "w:gz") as archive:
            for name in sorted(required_archive_files(version, platform_value) - {omitted_name}):
                payload = (f"{version}\n".encode() if name.endswith("/VERSION") else b"test\n")
                member = tarfile.TarInfo(name)
                member.size = len(payload)
                member.mode = contract[name]
                archive.addfile(member, io.BytesIO(payload))
        rejected(lambda path=omitted_archive: verify_release_archive(
            path, version, platform_value))

    for index, executable_name in enumerate(sorted(
            name for name, mode in contract.items() if mode == 0o755)):
        wrong_mode_archive = root / f"wrong-mode-{index}.tar.gz"
        with tarfile.open(wrong_mode_archive, "w:gz") as archive:
            for name in sorted(contract):
                payload = (f"{version}\n".encode() if name.endswith("/VERSION") else b"test\n")
                member = tarfile.TarInfo(name)
                member.size = len(payload)
                member.mode = 0o644 if name == executable_name else contract[name]
                archive.addfile(member, io.BytesIO(payload))
        rejected(lambda path=wrong_mode_archive: verify_release_archive(
            path, version, platform_value))

    writable_library = root / "writable-library.tar.gz"
    library_name = next(name for name in contract if name.endswith("/lib/libgraphx.a"))
    with tarfile.open(writable_library, "w:gz") as archive:
        for name in sorted(contract):
            payload = (f"{version}\n".encode() if name.endswith("/VERSION") else b"test\n")
            member = tarfile.TarInfo(name)
            member.size = len(payload)
            member.mode = 0o664 if name == library_name else contract[name]
            archive.addfile(member, io.BytesIO(payload))
    rejected(lambda: verify_release_archive(writable_library, version, platform_value))

    setuid_executable = root / "setuid-executable.tar.gz"
    executable_name = next(name for name, mode in contract.items() if mode == 0o755)
    with tarfile.open(setuid_executable, "w:gz") as archive:
        for name in sorted(contract):
            payload = (f"{version}\n".encode() if name.endswith("/VERSION") else b"test\n")
            member = tarfile.TarInfo(name)
            member.size = len(payload)
            member.mode = 0o4755 if name == executable_name else contract[name]
            archive.addfile(member, io.BytesIO(payload))
    rejected(lambda: verify_release_archive(setuid_executable, version, platform_value))

    unexpected_file = root / "unexpected-file.tar.gz"
    with tarfile.open(unexpected_file, "w:gz") as archive:
        for name in sorted((*contract, f"graphx-{version}-{platform_value}/unexpected")):
            payload = (f"{version}\n".encode() if name.endswith("/VERSION") else b"test\n")
            member = tarfile.TarInfo(name)
            member.size = len(payload)
            member.mode = contract.get(name, 0o644)
            archive.addfile(member, io.BytesIO(payload))
    rejected(lambda: verify_release_archive(unexpected_file, version, platform_value))

with tempfile.TemporaryDirectory(prefix="graphx-release-lock-boundary-") as temporary:
    source = Path(temporary)
    for parent in (source / "apps/telemetry", source / "web"):
        parent.mkdir(parents=True)
        write_json(parent / "package.json", {"version": version})
        write_json(parent / "package-lock.json", {
            "version": version, "packages": {"": {"version": version}}})
    (source / "VERSION").write_text(f"{version}\n", encoding="utf-8")
    bounded_lock = source / "apps/telemetry/package-lock.json"
    original = bounded_lock.read_bytes()
    with bounded_lock.open("ab") as stream:
        remaining = MAX_LOCK_BYTES - len(original)
        chunk = b" " * (1024 * 1024)
        while remaining:
            value = chunk[:min(remaining, len(chunk))]
            stream.write(value)
            remaining -= len(value)
    assert bounded_lock.stat().st_size == MAX_LOCK_BYTES
    assert source_version(source) == version
    with bounded_lock.open("ab") as stream:
        stream.write(b" ")
    rejected(lambda: source_version(source))
    write_json(bounded_lock, {"version": version, "packages": {"": {"version": version}}})
    write_json(source / "web/package.json", [])
    rejected(lambda: source_version(source))
    write_json(source / "web/package.json", {"version": version})
    version_file = source / "VERSION"
    real_version = source / "VERSION.real"
    version_file.replace(real_version)
    version_file.symlink_to(real_version.name)
    rejected(lambda: source_version(source))
    version_file.unlink()
    os.mkfifo(version_file)
    rejected(lambda: source_version(source))

versions = [
    {"id": 1, "metadata": {"container": {"tags": ["staging-1", "v1.2.3"]}}},
    {"id": 2, "metadata": {"container": {"tags": ["v1.2.2"]}}},
]
assert [entry["id"] for entry in tagged_versions(versions, "v1.2.3")] == [1]
rejected(lambda: tagged_versions([{"id": 3}], "v1.2.3"))


class FakeApi:
    def __init__(self, versions):
        self.versions = versions
        self.deleted: list[str] = []

    def request(self, method, path, allow_missing=False):
        del allow_missing
        if method == "GET":
            return self.versions
        assert method == "DELETE"
        self.deleted.append(path)
        return None


numeric_versions = [
    {"id": 1, "metadata": {"container": {"tags": ["staging-1", "1.2.3"]}}},
]
fake = FakeApi(numeric_versions)
delete_tag(fake, "owner", "User", "graphx-runtime", "v1.2.3", "staging-1")
assert fake.deleted and fake.deleted[0].endswith("/versions/1")
rejected(lambda: delete_tag(FakeApi([
    {"id": 4, "metadata": {"container": {"tags": ["1.2.3", "stable"]}}},
]), "owner", "User", "graphx-runtime", "v1.2.3", "staging-1"))
delete_tag(FakeApi([]), "owner", "User", "graphx-runtime", "v1.2.3",
           "staging-1", allow_absent=True)


class NormalizingApi:
    def __init__(self):
        self.paths: list[str] = []

    def request(self, method, path, allow_missing=False):
        del method, allow_missing
        self.paths.append(path)
        if "/releases/tags/" in path:
            return None
        return [{"id": 5, "metadata": {"container": {"tags": ["1.2.2"]}}}]


for accepted_tag in ("1.2.3", "v1.2.3"):
    normalizing = NormalizingApi()
    assert_absent(normalizing, "owner/repository", "owner", "User", accepted_tag,
                  ["graphx-runtime"])
    assert any(path.endswith("/releases/tags/v1.2.3") for path in normalizing.paths)
    assert not any("vv1.2.3" in path for path in normalizing.paths)
delete_tag(FakeApi(numeric_versions), "owner", "User", "graphx-runtime", "1.2.3",
           "staging-1")

oci_validator = SOURCE / "scripts/release/validate_oci_sbom.py"
valid_oci_sbom = json.dumps({
    "_type": "https://in-toto.io/Statement/v0.1",
    "predicateType": "https://spdx.dev/Document",
    "predicate": {"spdxVersion": "SPDX-2.3", "packages": [{"name": "ws"}]},
})
subprocess.run([sys.executable, oci_validator, "--artifact", "telemetry-test",
                "--expected-package", "ws"], input=valid_oci_sbom, text=True,
               stdout=subprocess.DEVNULL, check=True)
invalid_result = subprocess.run(
    [sys.executable, oci_validator, "--artifact", "invalid-test"], input="[]",
    text=True, capture_output=True, check=False)
assert invalid_result.returncode == 2 and "Traceback" not in invalid_result.stderr

dockerfile = (SOURCE / "Dockerfile").read_text(encoding="utf-8")
assert dockerfile.count("FROM debian:bookworm-slim@sha256:") == 2
workflow = (SOURCE / ".github/workflows/release.yml").read_text(encoding="utf-8")
for marker in ("ghcr_release.py assert-absent", "staging-${{ github.run_id }}",
               "sbom: generator=docker/buildkit-syft-scanner@sha256:",
               "validate_oci_sbom.py", "rollback-containers",
               "ghcr_release.py delete-tag"):
    assert marker in workflow, marker
assert "tags: ghcr.io/${{ github.repository_owner }}/graphx-runtime:${{ steps.version.outputs.version }}" not in workflow
assert "needs.publish.result != 'success'" in workflow
rollback = workflow.split("  rollback-containers:", 1)[1]
assert "    environment:" not in rollback

print("GraphX release contract validation passed")
