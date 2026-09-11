#!/usr/bin/env python3
"""Portable M6 QEMU TAP, OVS profile, and compatibility contracts."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(value) for value in args], text=True, capture_output=True,
                            timeout=30, env={**os.environ, "GRAPHX_OVERRIDES": ""})
    if check and result.returncode != 0:
        raise AssertionError(result.stderr)
    return result


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_m6_qemu_tap.py GRAPHX SOURCE_ROOT")
    graphx, root = Path(sys.argv[1]), Path(sys.argv[2])
    profile = root / "examples" / "qemu-node" / "tap"
    config = profile / "graphx.yaml"
    launcher = (profile / "scripts" / "ovs-lab.sh").read_text(encoding="utf-8")

    run(graphx, "validate", config)
    with tempfile.TemporaryDirectory(prefix="graphx-m6-") as raw:
        plan = run(graphx, "infra", "create", config, "--dry-run",
                   "--state-dir", Path(raw) / "state").stdout
    for marker in ("ip tuntap add dev gxqtap0 mode tap user 65532 group 65532",
                   "ovs-vsctl add-port br-qemu-tap gxqtap0", "mirror qemu-span",
                   "owner=qemu-peer", "address=10.0.2.2/24"):
        require(marker in plan, f"missing M6 plan marker: {marker}")
    require("docker network" not in plan and "macvlan" not in plan and "ipvlan" not in plan,
            "M6 must use only the OVS data-plane backend")

    text = config.read_text(encoding="utf-8")
    invalid_configs = (
        (text.replace("      tap_uid: 65532\n", "", 1), "qemu_tap requires"),
        (text.replace("      tap_gid: 65532\n", "", 1), "qemu_tap requires"),
        (text.replace("      tap_uid: 65532\n", "      peer: forbidden0\n      tap_uid: 65532\n", 1),
         "does not allow peer"),
        (text.replace("      kind: namespace_veth\n", "      kind: namespace_veth\n      tap_uid: 65532\n", 1),
         "allowed only for qemu_tap"),
    )
    for invalid, expected in invalid_configs:
        with tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False) as candidate:
            candidate.write(invalid)
            path = Path(candidate.name)
        rejected = run(graphx, "validate", path, check=False)
        path.unlink()
        require(rejected.returncode != 0 and expected in rejected.stderr,
                f"M6 invalid attachment was not rejected with {expected}")
    with tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False) as candidate:
        candidate.write(text.replace("tap_uid: 65532", "tap_uid: 0", 1))
        path = Path(candidate.name)
    rejected = run(graphx, "validate", path, check=False)
    path.unlink()
    require(rejected.returncode != 0 and "non-root UID" in rejected.stderr,
            "M6 must reject root TAP ownership")

    for marker in ("-netdev tap,id=net0,ifname=gxqtap0,script=no,downscript=no",
                   "--reuid \"$qemu_uid\"", "mac=02:00:00:00:02:15",
                   "qemu-span.pcap", "vm-control", "ovs-appctl fdb/show",
                   "value[\"records\"] > 0", "set -Eeuo pipefail",
                   "refusing to signal PID"):
        require(marker in launcher, f"missing M6 launcher contract: {marker}")
    require("-netdev user" not in launcher and "docker compose" not in launcher,
            "the M6 launcher must not fall back to slirp or Docker networking")

    ownership = (root / "src" / "ownership.cpp").read_text(encoding="utf-8")
    for marker in ("create_tap_endpoint", "tap_owner_matches", "ip\", \"tuntap\", \"add",
                   "AttachmentKind::qemu_tap", "configure_endpoint_vlan(endpoint)",
                   "GRAPHX_M6_CRASH_AFTER", "GRAPHX_M6_FAIL_AFTER"):
        require(marker in ownership, f"missing M6 ownership contract: {marker}")

    qmp = (root / "examples" / "qemu-node" / "tools" / "qmp_control.py").read_text()
    require('commands.add_parser("vm-control")' in qmp and '"stop"' in qmp and '"cont"' in qmp,
            "M6 needs bounded QMP pause/resume control")
    for relative in ("external/graphx.yaml", "container/graphx.yaml"):
        require("qemu-usernet" in (root / "examples" / "qemu-node" / relative).read_text(),
                f"legacy slirp profile {relative} must remain available")
    print("GraphX M6 portable QEMU TAP contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
