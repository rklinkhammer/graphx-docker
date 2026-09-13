#!/usr/bin/env python3
"""Create and verify deterministic identity metadata for QEMU guest artifacts."""

from __future__ import annotations

import argparse
import hashlib
import stat
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
        value.update(str(stat.S_IMODE(item.stat().st_mode)).encode())
        value.update(b"\0")
        value.update(bytes.fromhex(digest(item)))
    return value.hexdigest()


if __name__ == "__main__":
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]/'scripts/release'))
    from guest_release import verify
    from release_common import ReleaseError
    parser = argparse.ArgumentParser(description='Verify the common guest release artifact contract')
    parser.add_argument('--guests', type=Path, required=True)
    parser.add_argument('--allow-dirty', action='store_true')
    args = parser.parse_args()
    try:
        verify(args.guests, args.allow_dirty)
    except (ReleaseError, ValueError, OSError, KeyError, TypeError) as error:
        print('guest artifacts:', error, file=sys.stderr)
        raise SystemExit(2)
    print('Guest artifact manifest verified')
