#!/usr/bin/env python3
"""Privileged Linux regressions for M3 filesystem and OVS identity boundaries."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


BRIDGES = ("br-gx-mac", "br-gx-ipv")


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(value) for value in args], text=True, capture_output=True,
        timeout=20, env={**os.environ, "GRAPHX_OVERRIDES": ""}, check=False,
    )
    if check and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(map(str, args))}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def bridge_exists(name: str) -> bool:
    return run("ovs-vsctl", "br-exists", name, check=False).returncode == 0


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_m3_ownership_live.py GRAPHX SOURCE_ROOT")
    if sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("M3 live ownership tests require root on Linux")
    graphx = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    require(run("ovs-vsctl", "show", check=False).returncode == 0,
            "system Open vSwitch is unavailable")
    require(not any(bridge_exists(name) for name in BRIDGES),
            "M3 live bridge names must be absent before the test")

    with tempfile.TemporaryDirectory(prefix="graphx-m3-live-", dir="/var/tmp") as raw:
        temporary = Path(raw)
        config = temporary / "mixed-v2.yaml"
        config.write_text(run(
            graphx, "config", "migrate",
            root / "examples" / "mixed-network" / "graphx.yaml",
        ).stdout, encoding="utf-8")

        missing_root = temporary / "missing-state"
        status = run(
            graphx, "infra", "status", config, "--state-dir", missing_root,
            check=False,
        )
        require(status.returncode == 2 and not missing_root.exists(),
                "status must not create an absent state root")

        state_root = temporary / "state"
        state_root.mkdir(mode=0o700)
        state_path = state_root / "mixed-network.yaml"
        lock_path = state_root / "mixed-network.lock"
        status = run(
            graphx, "infra", "status", config, "--state-dir", state_root,
            check=False,
        )
        require(status.returncode == 2 and not lock_path.exists(),
                "status must not create a lock in an existing empty state root")
        missing_target = temporary / "missing-target"
        state_path.symlink_to(missing_target)
        create = run(
            graphx, "infra", "create", config, "--state-dir", state_root,
            check=False,
        )
        require(create.returncode != 0, "create must reject a dangling state symlink")
        require(state_path.is_symlink() and state_path.readlink() == missing_target,
                "create must preserve the dangling state symlink")
        require(not missing_target.exists() and not lock_path.exists(),
                "create must not mutate the symlink target or per-graph lock")
        require(not any(bridge_exists(name) for name in BRIDGES),
                "dangling state refusal must precede OVS mutation")
        state_path.unlink()

        run(graphx, "infra", "create", config, "--state-dir", state_root)
        run("ovs-vsctl", "set", "Bridge", BRIDGES[0],
            "external_ids:graphx_graph=changed-by-regression")
        status = run(
            graphx, "infra", "status", config, "--state-dir", state_root,
            check=False,
        )
        require(status.returncode == 2 and "missing-or-replaced" in status.stdout,
                "status must reject a changed graph marker")
        destroy = run(
            graphx, "infra", "destroy", config, "--state-dir", state_root,
            check=False,
        )
        require(destroy.returncode != 0 and all(bridge_exists(name) for name in BRIDGES),
                "destroy preflight must preserve the complete set on graph-marker drift")
        run("ovs-vsctl", "set", "Bridge", BRIDGES[0],
            "external_ids:graphx_graph=mixed-network")
        run(graphx, "infra", "destroy", config, "--state-dir", state_root)
        require(not any(bridge_exists(name) for name in BRIDGES),
                "owned bridges must be removed after restoring identity")

    print("GraphX M3 live ownership regressions passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
