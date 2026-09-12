#!/usr/bin/env python3
"""Behavioral checks for shared demo runtime boundaries."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_config_dialect_boundaries.py SOURCE_ROOT")
    root = Path(sys.argv[1]).resolve()

    runtime = root / "scripts/lib/demo-runtime.sh"
    subprocess.run(["bash", "-n", str(runtime)], check=True)

    for lab in ("macvlan", "ipvlan-l2", "ipvlan-l3", "mixed-network"):
        for action in ("up", "status", "down"):
            launcher = root / "examples" / lab / "scripts" / f"{action}.sh"
            subprocess.run(["bash", "-n", str(launcher)], check=True)

    with tempfile.TemporaryDirectory(prefix="graphx-s7-runtime-") as temporary:
        fake = Path(temporary)
        (fake / "uname").write_text("#!/bin/sh\nprintf 'Darwin\\n'\n", encoding="utf-8")
        (fake / "docker").write_text(
            "#!/bin/sh\n"
            "if [ \"$1 $2\" = 'context show' ]; then printf '%s\\n' \"$GRAPHX_TEST_CONTEXT\"; exit 0; fi\n"
            "if [ \"$1 $2\" = 'compose version' ] || [ \"$1\" = info ]; then exit 0; fi\n"
            "exit 1\n", encoding="utf-8")
        for executable in (fake / "uname", fake / "docker"):
            executable.chmod(0o755)
        command = f'source "{runtime}"; graphx_demo_require_compose_runtime'
        environment = {**os.environ, "PATH": f"{fake}:{os.environ['PATH']}"}
        rejected = subprocess.run(["bash", "-c", command], env={**environment,
                                  "GRAPHX_TEST_CONTEXT": "desktop-linux"},
                                  text=True, capture_output=True)
        require(rejected.returncode == 2 and "use OrbStack" in rejected.stderr,
                "macOS ordinary Compose does not reject a non-OrbStack context")
        subprocess.run(["bash", "-c", command], env={**environment,
                       "GRAPHX_TEST_CONTEXT": "orbstack"}, check=True)

    print("shared demo runtime boundaries passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
