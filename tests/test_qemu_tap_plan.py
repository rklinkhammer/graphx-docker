#!/usr/bin/env python3
"""Portable QEMU TAP and OVS contracts."""

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
        raise SystemExit("usage: test_qemu_tap_plan.py GRAPHX SOURCE_ROOT")
    graphx, root = Path(sys.argv[1]), Path(sys.argv[2])
    profile = root / "examples" / "qemu-node" / "tap"
    config = profile / "graphx.yaml"
    launcher = profile / "scripts" / "ovs-lab.sh"
    demo = root / "examples" / "qemu-node" / "scripts" / "demo.sh"

    run(graphx, "validate", config)
    with tempfile.TemporaryDirectory(prefix="graphx-qemu-tap-") as raw:
        plan = run(graphx, "infra", "create", config, "--dry-run",
                   "--state-dir", Path(raw) / "state").stdout
    for marker in ("ip tuntap add dev gxqtap0 mode tap user 65532 group 65532",
                   "ovs-vsctl add-port br-qemu-tap gxqtap0", "mirror qemu-span",
                   "dumpcap attachment=qemu-span format=ethernet-pcapng",
                   "directory=/var/lib/graphx/captures/qemu-tap",
                   "owner=qemu-peer", "address=10.0.2.2/24"):
        require(marker in plan, f"missing QEMU TAP plan marker: {marker}")
    require("docker network" not in plan and "macvlan" not in plan and "ipvlan" not in plan,
            "QEMU TAP must use only the OVS data-plane backend")

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
                f"invalid QEMU attachment was not rejected with {expected}")
    with tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False) as candidate:
        candidate.write(text.replace("tap_uid: 65532", "tap_uid: 0", 1))
        path = Path(candidate.name)
    rejected = run(graphx, "validate", path, check=False)
    path.unlink()
    require(rejected.returncode != 0 and "non-root UID" in rejected.stderr,
            "QEMU TAP must reject root ownership")

    command = run("bash", launcher, "print-qemu-command").stdout
    require("q35,accel=tcg" in command, "QEMU must use the supported TCG accelerator")
    require("tap,id=net0,ifname=gxqtap0,script=no,downscript=no" in command,
            "QEMU must attach to the GraphX-owned TAP")
    require("mac=02:00:00:00:02:15" in command, "QEMU must use the configured guest MAC")
    require("-netdev user" not in command, "QEMU must not use user networking")

    unsupported = run("bash", demo, "start", "--accel", "auto", check=False)
    require(unsupported.returncode == 64 and "usage:" in unsupported.stderr,
            "the QEMU launcher must reject unsupported options")

    qmp = (root / "examples" / "qemu-node" / "tools" / "qmp_control.py").read_text()
    require('commands.add_parser("vm-control")' in qmp and '"stop"' in qmp and '"cont"' in qmp,
            "QEMU needs bounded QMP pause/resume control")
    print("GraphX portable QEMU TAP contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
