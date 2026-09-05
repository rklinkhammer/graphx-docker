#!/usr/bin/env python3
"""Create and verify deterministic identity metadata for QEMU guest artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import tempfile
from pathlib import Path


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def tree_digest(path: Path) -> str:
    value = hashlib.sha256()
    for item in sorted(candidate for candidate in path.rglob("*") if candidate.is_file()):
        value.update(item.relative_to(path).as_posix().encode())
        value.update(b"\0")
        value.update(bytes.fromhex(digest(item)))
    return value.hexdigest()


def atomic_write(path: Path, value: object) -> None:
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(descriptor, 0o644)
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(value, output, sort_keys=True, indent=2)
            output.write("\n")
        os.replace(temporary, path)
    except BaseException:
        try:
            os.close(descriptor)
        except OSError:
            pass
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def make_manifest(images: Path, guest: Path, buildroot_version: str,
                  epoch: int) -> dict[str, object]:
    artifacts = {}
    for name in ("bzImage", "rootfs.cpio.gz"):
        path = images / name
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"missing guest artifact: {path}")
        artifacts[name] = {"sha256": digest(path), "bytes": path.stat().st_size}
    return {"schemaVersion": 1, "buildrootVersion": buildroot_version,
            "sourceDateEpoch": epoch, "guestSourceSha256": tree_digest(guest),
            "artifacts": artifacts}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("create", "verify"))
    parser.add_argument("--images", type=Path, required=True)
    parser.add_argument("--guest", type=Path)
    parser.add_argument("--buildroot-version", default="unknown")
    parser.add_argument("--source-date-epoch", type=int, default=0)
    arguments = parser.parse_args()
    path = arguments.images / "manifest.json"
    try:
        if arguments.command == "create":
            if arguments.guest is None:
                raise ValueError("--guest is required when creating a manifest")
            value = make_manifest(arguments.images, arguments.guest,
                                  arguments.buildroot_version, arguments.source_date_epoch)
            atomic_write(path, value)
        else:
            value = json.loads(path.read_text(encoding="utf-8"))
            artifacts = value.get("artifacts", {})
            for name in ("bzImage", "rootfs.cpio.gz"):
                expected = artifacts.get(name, {}).get("sha256")
                if not isinstance(expected, str) or len(expected) != 64:
                    raise ValueError(f"manifest has no valid hash for {name}")
                if digest(arguments.images / name) != expected:
                    raise ValueError(f"guest artifact does not match manifest: {name}")
            if arguments.guest is not None and tree_digest(arguments.guest) != value.get("guestSourceSha256"):
                raise ValueError("guest source does not match artifact manifest; rebuild the guest")
        print(json.dumps(value, sort_keys=True))
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"artifact-manifest: {error}", file=os.sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
