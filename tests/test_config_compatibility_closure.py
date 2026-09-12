#!/usr/bin/env python3
"""Portable compatibility and canonical-example behavior checks."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(arg) for arg in args], text=True, capture_output=True, timeout=30,
        env={**os.environ, "GRAPHX_OVERRIDES": ""},
    )
    if check and result.returncode:
        raise AssertionError(
            f"{' '.join(map(str, args))}\n{result.stdout}\n{result.stderr}"
        )
    return result


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: test_config_compatibility_closure.py GRAPHX SOURCE_ROOT"
        )
    graphx, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()

    fixtures = sorted((root / "examples/compatibility/v1").glob("*.yaml"))
    require(fixtures, "at least one curated version-1 compatibility fixture is required")
    with tempfile.TemporaryDirectory(prefix="graphx-m8-portable-") as raw:
        temporary = Path(raw)
        for fixture in fixtures:
            run(graphx, "validate", fixture)
            run(graphx, "inspect", fixture)
            first = run(graphx, "config", "migrate", fixture).stdout
            second = run(graphx, "config", "migrate", fixture).stdout
            require(first == second and first.startswith("# GraphX deterministic migration"),
                    f"nondeterministic v1 migration: {fixture.name}")
            output = temporary / fixture.stem
            run(graphx, "project", fixture, "--output-dir", output)
            require(output.is_dir(), f"v1 projection failed: {fixture.name}")
            for action in ("create", "destroy", "status", "recover"):
                state = temporary / f"state-{fixture.stem}-{action}"
                result = run(graphx, "infra", action, fixture, "--dry-run",
                             "--state-dir", state, check=False)
                require(result.returncode == 64 and "config migrate" in result.stderr
                        and "config migrate" in result.stderr,
                        f"v1 infra {action} was not safely rejected: {fixture.name}")
                require(not state.exists(),
                        f"v1 infra {action} mutated state: {fixture.name}")
        route = run(graphx, "infra", "route", "apply", fixtures[4],
                    "--router", "route-router", "--destination", "10.64.30.10/32",
                    "--dry-run", check=False)
        require(route.returncode == 64 and "config migrate" in route.stderr
                and "config migrate" in route.stderr,
                "v1 manual-route mutation was not safely rejected")
        capture = run(graphx, "infra", "capture", "export", fixtures[0],
                      "--capture", "legacy", "--output", temporary / "legacy.pcapng",
                      check=False)
        require(capture.returncode == 64 and "config migrate" in capture.stderr
                and "config migrate" in capture.stderr,
                "v1 capture operation was not safely rejected")

    canonical = (
        "mixed-network", "macvlan", "ipvlan-l2", "ipvlan-l3",
        "static-route-policy", "sdr-node/external",
    )
    for relative in canonical:
        example = root / "examples" / relative
        config = example / "graphx.yaml"
        require(config.is_file(), f"missing canonical M8 config: {relative}")
        run(graphx, "validate", config)
        plan = run(graphx, "infra", "create", config, "--dry-run").stdout
        require("ovs-vsctl -- add-br" in plan and "docker network" not in plan,
                f"canonical lab is not OVS-only: {relative}")

    compose = (root / "examples/network-lab.compose.yaml").read_text(encoding="utf-8")
    require("driver: macvlan" not in compose and "driver: ipvlan" not in compose
            and "external: true" not in compose,
            "Docker data-plane network remains in shared laboratory Compose")
    for relative in ("mixed-network", "macvlan", "ipvlan-l2", "ipvlan-l3"):
        example = root / "examples" / relative
        for action in ("up", "status", "down"):
            wrapper = example / "scripts" / f"{action}.sh"
            require(wrapper.is_file(), f"missing canonical wrapper: {wrapper}")
            require(os.access(wrapper, os.X_OK),
                    f"canonical wrapper is not executable: {wrapper}")

    qemu = root / "examples/qemu-node"
    tap_config = qemu / "tap/graphx.yaml"
    run(graphx, "validate", tap_config)
    tap_plan = run(graphx, "infra", "create", tap_config, "--dry-run").stdout
    require("ip tuntap add" in tap_plan and "ovs-vsctl -- add-br" in tap_plan
            and "docker network" not in tap_plan,
            "canonical QEMU plan is not TAP/OVS-only")
    for profile in ("external", "container"):
        readme = (qemu / profile / "README.md").read_text(encoding="utf-8").lower()
        require("deprecated" in readme and "compatibility" in readme,
                f"QEMU {profile} profile is not explicitly deprecated")

    fault = run(graphx, "infra", "fault", root / "examples/macvlan/graphx.yaml",
                "--dry-run", check=False)
    require(fault.returncode == 64 and "network.faults" in fault.stderr,
            "imperative fault entry point was not retired")
    print("GraphX portable compatibility closure passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
