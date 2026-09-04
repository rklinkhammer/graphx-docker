#!/usr/bin/env python3
"""Build and finalize a local GraphX release candidate without publishing it."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from release_common import (MAX_PACKAGE_JSON_BYTES, ReleaseError, current_platform,
                            json_object, sha256_file, source_version, spdx_id,
                            validate_commit, validate_dependency_version, validate_epoch,
                            validate_platform, validate_tag, verify_release_archive)


def run(command: list[str], cwd: Path, environment: dict[str, str]) -> None:
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def git(source: Path, *arguments: str) -> str:
    return subprocess.check_output(["git", *arguments], cwd=source, text=True).strip()


def sbom(version: str, commit: str, platform_value: str, archive: Path,
         created: str, openssl_version: str) -> dict:
    root_id = "SPDXRef-Package-GraphX"
    archive_digest = sha256_file(archive)
    packages = [{
        "name": "GraphX", "SPDXID": root_id, "versionInfo": version,
        "downloadLocation": "NOASSERTION", "filesAnalyzed": False,
        "licenseConcluded": "MIT", "licenseDeclared": "MIT",
        "primaryPackagePurpose": "APPLICATION",
        "checksums": [{"algorithm": "SHA256", "checksumValue": archive_digest}],
        "externalRefs": [{"referenceCategory": "PACKAGE-MANAGER",
                          "referenceType": "purl",
                          "referenceLocator":
                              f"pkg:generic/graphx@{version}?os={platform_value.split('-', 1)[0]}"
                              f"&arch={platform_value.split('-', 1)[1]}"}],
    }]
    dependencies = [
        {"name": "yaml-cpp", "version": "0.9.0", "license": "MIT"},
        {"name": "OpenSSL", "version": openssl_version, "license": "Apache-2.0"},
    ]
    relationships = [{"spdxElementId": "SPDXRef-DOCUMENT",
                      "relationshipType": "DESCRIBES", "relatedSpdxElement": root_id}]
    for dependency in dependencies:
        identifier = spdx_id(dependency["name"], dependency["version"])
        packages.append({
            "name": dependency["name"], "SPDXID": identifier,
            "versionInfo": dependency["version"], "downloadLocation": "NOASSERTION",
            "filesAnalyzed": False, "licenseConcluded": "NOASSERTION",
            "licenseDeclared": dependency["license"], "primaryPackagePurpose": "LIBRARY",
        })
        relationships.append({"spdxElementId": root_id, "relationshipType": "DEPENDS_ON",
                              "relatedSpdxElement": identifier})
    return {
        "spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT", "name": f"graphx-{version}-{platform_value}",
        "documentNamespace":
            f"https://github.com/rklinkhammer/graphx-docker/releases/tag/v{version}/"
            f"spdx/{commit}/{platform_value}/{archive_digest}",
        "creationInfo": {"created": created, "creators": ["Tool: graphx-release/1"]},
        "packages": packages, "relationships": relationships,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--tag")
    parser.add_argument("--allow-dirty", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    version = source_version(source)
    tag = args.tag or f"v{version}"
    validate_tag(tag, version)
    if not args.allow_dirty and git(source, "status", "--porcelain"):
        raise ReleaseError("release builds require a clean worktree")
    for directory, label in ((build, "build"), (output, "output")):
        if directory.exists() and any(directory.iterdir()):
            raise ReleaseError(f"{label} directory must be absent or empty: {directory}")
        directory.mkdir(parents=True, exist_ok=True)

    commit = validate_commit(git(source, "rev-parse", "HEAD"))
    epoch = validate_epoch(int(os.environ.get("SOURCE_DATE_EPOCH") or
                               git(source, "show", "-s", "--format=%ct", "HEAD")))
    created = dt.datetime.fromtimestamp(epoch, dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    environment = {**os.environ, "SOURCE_DATE_EPOCH": str(epoch), "ZERO_AR_DATE": "1",
                   "TZ": "UTC", "LC_ALL": "C", "COPYFILE_DISABLE": "1"}
    run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_STANDARD=20",
         "-DGRAPHX_BUILD_TESTS=ON", "-DGRAPHX_FORCE_BUNDLED_YAML_CPP=ON"], source, environment)
    run(["cmake", "--build", str(build), "-j", "4"], source, environment)
    run(["ctest", "--test-dir", str(build), "--output-on-failure"], source, environment)
    run(["cpack", "--config", str(build / "CPackConfig.cmake"), "-G", "TGZ",
         "-B", str(output)], source, environment)
    cpack_staging = output / "_CPack_Packages"
    if cpack_staging.is_dir():
        shutil.rmtree(cpack_staging)
    archives = sorted(output.glob("graphx-*.tar.gz"))
    archives = [path for path in archives if not path.name.endswith("-source.tar.gz")]
    if len(archives) != 1:
        raise ReleaseError(f"expected one binary archive, found {len(archives)}")
    platform_value = validate_platform(current_platform())
    archive = output / f"graphx-{version}-{platform_value}.tar.gz"
    if archives[0] != archive:
        archives[0].replace(archive)
    verify_release_archive(archive, version, platform_value)
    dependencies = json_object(build / "generated/graphx-build-dependencies.json",
                               MAX_PACKAGE_JSON_BYTES, "configured dependency versions")
    if set(dependencies) != {"openssl", "yamlCpp"} or dependencies.get("yamlCpp") != "0.9.0":
        raise ReleaseError("configured dependency versions do not match the release contract")
    openssl_version = validate_dependency_version(dependencies.get("openssl"), "OpenSSL version")

    sbom_name = f"graphx-{version}-{platform_value}.spdx.json"
    sbom_path = output / sbom_name
    sbom_path.write_text(json.dumps(sbom(version, commit, platform_value, archive, created,
                                         openssl_version),
                                    indent=2, sort_keys=True) + "\n", encoding="utf-8")
    manifest = {
        "schemaVersion": 2, "product": "graphx", "version": version, "tag": tag,
        "commit": commit, "sourceDateEpoch": epoch, "platform": platform_value,
        "artifacts": [
            {"name": archive.name, "mediaType": "application/gzip",
             "sha256": sha256_file(archive), "size": archive.stat().st_size},
            {"name": sbom_path.name, "mediaType": "application/spdx+json",
             "sha256": sha256_file(sbom_path), "size": sbom_path.stat().st_size},
        ],
    }
    manifest_path = output / f"graphx-{version}-{platform_value}.manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    checksum_paths = [archive, sbom_path, manifest_path]
    (output / f"graphx-{version}-{platform_value}.sha256").write_text("".join(
        f"{sha256_file(path)}  {path.name}\n" for path in sorted(checksum_paths)), encoding="utf-8")
    print(f"GraphX {version} release candidate: {output}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ReleaseError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"graphx-release: {error}", file=sys.stderr)
        sys.exit(2)
