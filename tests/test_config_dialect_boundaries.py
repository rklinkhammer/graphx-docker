#!/usr/bin/env python3
"""S7 static and behavioral checks for compatibility and demo boundaries."""

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
        raise SystemExit("usage: test_s7_boundaries.py SOURCE_ROOT")
    root = Path(sys.argv[1]).resolve()

    config = (root / "src/config.cpp").read_text(encoding="utf-8")
    common = (root / "src/config_common.cpp").read_text(encoding="utf-8")
    v1 = (root / "src/config_v1_compat.cpp").read_text(encoding="utf-8")
    v2 = (root / "src/config_v2.cpp").read_text(encoding="utf-8")
    require("dialect_for(config.version)" in config, "load parser does not select an explicit dialect")
    require("interpret_v1_network" in config and "interpret_v2_network" in config,
            "common YAML parser bypasses version interpreters")
    require("version == 1" in common and "version == 2" in common,
            "supported-version selection is not isolated in config_common")
    for forbidden in ("NetworkDriver::macvlan", "NetworkDriver::ipvlan"):
        require(forbidden not in v2, "v2 reintroduced a legacy Docker network driver")
    require("NetworkProfile::macvlan" in v2 and "NetworkProfile::ipvlan_l3" in v2,
            "v2 semantic profile vocabulary is missing")
    require("NetworkDriver::macvlan" in v1 and "NetworkDriver::ipvlan" in v1,
            "v1 compatibility vocabulary is not quarantined")

    runtime = root / "scripts/lib/demo-runtime.sh"
    subprocess.run(["bash", "-n", str(runtime)], check=True)
    text = runtime.read_text(encoding="utf-8")
    for token in ("graphx_demo_require_compose_runtime", "orbstack",
                  "graphx_demo_dispatch_lima", "graphx_demo_ovs_lab_local",
                  "graphx_demo_network_lab", "graphx_demo_port"):
        require(token in text, f"shared demo runtime omits {token}")

    for lab in ("macvlan", "ipvlan-l2", "ipvlan-l3", "mixed-network"):
        for action in ("up", "status", "down"):
            launcher = root / "examples" / lab / "scripts" / f"{action}.sh"
            subprocess.run(["bash", "-n", str(launcher)], check=True)
            launcher_text = launcher.read_text(encoding="utf-8")
            require("network-lab-ovs.sh" in launcher_text and "docker compose" not in launcher_text,
                    f"{lab}/{action}.sh is no longer a thin launcher")

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

    print("GraphX S7 module and launcher boundaries passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
