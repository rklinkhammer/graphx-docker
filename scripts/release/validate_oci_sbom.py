#!/usr/bin/env python3
"""Validate a bounded BuildKit SPDX SBOM extracted from a staged OCI image."""

from __future__ import annotations

import argparse
import json
import sys

from release_common import ReleaseError

MAX_SBOM_BYTES = 32 * 1024 * 1024


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--artifact", required=True)
    parser.add_argument("--expected-package", action="append", default=[])
    args = parser.parse_args()
    payload = sys.stdin.buffer.read(MAX_SBOM_BYTES + 1)
    if len(payload) > MAX_SBOM_BYTES:
        raise ReleaseError("OCI SBOM exceeds the 32 MiB validation limit")
    try:
        document = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ReleaseError("OCI SBOM is not valid JSON") from error
    if (isinstance(document, dict) and
            document.get("predicateType") == "https://spdx.dev/Document" and
            isinstance(document.get("predicate"), dict)):
        document = document["predicate"]
    if not isinstance(document, dict) or document.get("spdxVersion") not in (
            "SPDX-2.2", "SPDX-2.3"):
        raise ReleaseError("OCI SBOM is not an SPDX 2.2/2.3 document")
    packages = document.get("packages")
    if not isinstance(packages, list) or not packages or not all(
            isinstance(package, dict) for package in packages):
        raise ReleaseError("OCI SBOM has no package inventory")
    names = {package.get("name") for package in packages
             if isinstance(package.get("name"), str)}
    missing = sorted(set(args.expected_package) - names)
    if missing:
        raise ReleaseError(f"{args.artifact} OCI SBOM is missing expected packages: {', '.join(missing)}")
    print(f"validated {args.artifact} OCI SBOM with {len(packages)} packages")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ReleaseError, OSError, ValueError) as error:
        print(f"graphx-oci-sbom: {error}", file=sys.stderr)
        sys.exit(2)
