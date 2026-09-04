#!/usr/bin/env python3
"""Validate the source version and its exact release tag."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from release_common import ReleaseError, source_version, validate_tag


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()
    version = source_version(args.source.resolve())
    validate_tag(args.tag, version)
    print(f"GraphX release version {version} matches tag {args.tag}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ReleaseError, OSError, ValueError) as error:
        print(f"graphx-version: {error}", file=sys.stderr)
        sys.exit(2)
