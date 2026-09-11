#!/usr/bin/env python3
"""Portable M8 compatibility-closure and canonical-example contract checks."""

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
            "usage: test_m8_compatibility_closure.py GRAPHX SOURCE_ROOT"
        )
    graphx, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()

    production = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for directory in (root / "apps", root / "include", root / "src")
        for path in directory.rglob("*") if path.is_file()
    )
    for retired in ("std::vector<InfraAction>", "netem_command", "docker network create"):
        require(retired not in production, f"retired realization remains: {retired}")

    fixtures = sorted((root / "examples/compatibility/v1").glob("*.yaml"))
    require(len(fixtures) == 7, "M8 must retain exactly seven curated v1 fixtures")
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
                require(result.returncode == 64 and "retired in M8" in result.stderr
                        and "config migrate" in result.stderr,
                        f"v1 infra {action} was not safely rejected: {fixture.name}")
                require(not state.exists(),
                        f"v1 infra {action} mutated state: {fixture.name}")
        route = run(graphx, "infra", "route", "apply", fixtures[4],
                    "--router", "route-router", "--destination", "10.64.30.10/32",
                    "--dry-run", check=False)
        require(route.returncode == 64 and "retired in M8" in route.stderr
                and "config migrate" in route.stderr,
                "v1 manual-route mutation was not safely rejected")
        capture = run(graphx, "infra", "capture", "export", fixtures[0],
                      "--capture", "legacy", "--output", temporary / "legacy.pcapng",
                      check=False)
        require(capture.returncode == 64 and "retired in M8" in capture.stderr
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
        require(not (example / "graphx-ovs.yaml").exists(),
                f"parallel OVS config remains: {relative}")

    for relative in ("mixed-network", "macvlan", "ipvlan-l2", "ipvlan-l3"):
        example = root / "examples" / relative
        compose = (example / "compose.yaml").read_text(encoding="utf-8")
        require("driver: macvlan" not in compose and "driver: ipvlan" not in compose
                and "external: true" not in compose,
                f"Docker data-plane network remains in {relative}")
        for action in ("up", "status", "down"):
            wrapper = example / "scripts" / f"{action}.sh"
            require(wrapper.is_file(), f"missing canonical wrapper: {wrapper}")
            require(os.access(wrapper, os.X_OK),
                    f"canonical wrapper is not executable: {wrapper}")
            require("network-lab-ovs.sh" in wrapper.read_text(encoding="utf-8"),
                    f"wrapper bypasses common OVS lifecycle: {wrapper}")
        require(not (example / "compose.ovs.yaml").exists(),
                f"parallel OVS Compose file remains: {relative}")

    removed = (
        root / "docker/ovs",
        root / "examples/mixed-network/scripts/macos-up.sh",
        root / "examples/mixed-network/scripts/linux-up.sh",
        root / "examples/mixed-network/compose",
    )
    for path in removed:
        require(not path.exists(), f"retired M8 path remains: {path}")

    retired_entrypoints = (
        "linux-up.sh", "linux-down.sh", "macos-up.sh", "macos-down.sh",
        "compose.ovs.yaml", "graphx-ovs.yaml", "scripts/ovs-up.sh",
        "scripts/ovs-down.sh", "scripts/ovs-status.sh", "scripts/fault.sh",
    )
    active_files = [root / "README.md", root / "SUPPORT.md"]
    active_files.extend((root / "scripts").rglob("*.sh"))
    active_files.extend((root / "examples").rglob("*.sh"))
    active_files.extend(
        path for path in (root / "docs").rglob("*.md")
        if "adr" not in path.parts and "archive" not in path.parts
        and not path.name.startswith("GraphX_Phases_")
    )
    active_text = "\n".join(
        path.read_text(encoding="utf-8", errors="replace") for path in active_files
    )
    for entrypoint in retired_entrypoints:
        require(entrypoint not in active_text,
                f"active launcher or documentation names retired path: {entrypoint}")
    feature_runner = (root / "scripts/test-features.sh").read_text(encoding="utf-8")
    require('export GRAPHX_BIN="$BUILD_DIR/graphx"' in feature_runner,
            "native Linux verification does not select its Linux GraphX binary")
    require("infra fault apply" not in feature_runner,
            "feature verification invokes the retired imperative fault entry point")
    require("network-observability.plan" in feature_runner
            and "auto-clear=identity-checked" in feature_runner,
            "feature verification does not assert the declarative bounded-fault plan")

    qemu = root / "examples/qemu-node"
    default = (qemu / "scripts/demo.sh").read_text(encoding="utf-8")
    tap_launcher = (qemu / "tap/scripts/ovs-lab.sh").read_text(encoding="utf-8")
    tap_config = qemu / "tap/graphx.yaml"
    require("tap/scripts/ovs-lab.sh" in default and "demo-profile.sh" not in default,
            "default QEMU launcher is not TAP/OVS")
    require("graphx.yaml" in tap_launcher and "-netdev user" not in tap_launcher,
            "TAP launcher contains a user-network fallback")
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
    require(fault.returncode == 64 and "retired in M8" in fault.stderr
            and "network.faults" in fault.stderr,
            "imperative fault entry point was not retired")

    for document in (root / "docs/m8-compatibility.md",
                     root / "docs/adr/0018-compatibility-closure.md"):
        require(document.is_file(), f"missing M8 documentation: {document}")
    print("GraphX M8 portable compatibility closure passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
