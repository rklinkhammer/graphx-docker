#!/usr/bin/env python3
"""Validate every authoritative example configuration with the built CLI."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_example_configs.py GRAPHX SOURCE_ROOT")
    graphx = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    configs = [root / "graphx.yaml", *sorted((root / "examples").glob("**/graphx.yaml"))]
    environment = {**os.environ, "GRAPHX_OVERRIDES": ""}
    for config in configs:
        result = subprocess.run(
            [graphx, "validate", config], text=True, capture_output=True,
            timeout=15, env=environment, check=False,
        )
        if result.returncode:
            relative = config.relative_to(root)
            raise AssertionError(f"{relative} is invalid:\n{result.stderr}")
    print(f"validated {len(configs)} authoritative configurations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
