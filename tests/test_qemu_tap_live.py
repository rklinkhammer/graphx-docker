#!/usr/bin/env python3
"""Privileged Linux QEMU TAP QEMU-TAP/OVS ownership lifecycle regression."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args: object, check: bool = True,
        extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(value) for value in args], text=True, capture_output=True,
                            timeout=30,
                            env={**os.environ, "GRAPHX_OVERRIDES": "", **(extra_env or {})})
    if check and result.returncode != 0:
        raise AssertionError(f"{' '.join(map(str, args))}\n{result.stdout}\n{result.stderr}")
    return result


def main() -> int:
    if len(sys.argv) != 2 or sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("QEMU TAP live test requires: test_qemu_tap_live.py GRAPHX as Linux root")
    graphx = Path(sys.argv[1]).resolve()
    bridge, tap = "br-gxqtlive", "gxqttap0"

    run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
    run("ip", "tuntap", "delete", "dev", tap, "mode", "tap", check=False)
    try:
        with tempfile.TemporaryDirectory(prefix="graphx-qemu-tap-live-", dir="/var/tmp") as raw:
            root = Path(raw)
            state, config = root / "state", root / "graphx.yaml"
            config.write_text(f"""\
version: 2
graph:
  id: qemu-tap-live
  nodes: [{{id: guest, kind: virtual-machine, runtime: qemu, execution: host,
           lifecycle: external, control: none, accelerator: tcg, architecture: x86_64,
           ports: []}}]
  edges: []
transport: {{}}
network:
  networks: [{{id: data, profile: ethernet, subnets: [10.89.0.0/24], gateway: 10.89.0.1, external: false}}]
  switches:
    - id: {bridge}
      kind: openvswitch
      ports: [{{id: guest, interface: {tap}, vlan: {{access_tag: 42, trunks: [43]}}}}]
  attachments:
    - {{id: guest-data, kind: qemu_tap, owner: guest, network: data,
       address: 10.89.0.2/24, mac: "02:89:00:00:00:02", interface: {tap},
       switch: {bridge}, mtu: 1400, tap_uid: 65532, tap_gid: 65532}}
""", encoding="utf-8")
            state_file = state / "qemu-tap-live.yaml"

            failed = run(graphx, "infra", "create", config, "--state-dir", state,
                         check=False, extra_env={"GRAPHX_TEST_FAIL_AFTER_MUTATION": "2"})
            if (failed.returncode == 0 or state_file.exists() or
                    run("ovs-vsctl", "br-exists", bridge, check=False).returncode == 0 or
                    Path(f"/sys/class/net/{tap}").exists()):
                raise AssertionError("QEMU TAP injected failure did not roll back TAP and bridge")

            crashed = run(graphx, "infra", "create", config, "--state-dir", state,
                          check=False, extra_env={"GRAPHX_TEST_CRASH_AFTER_MUTATION": "2"})
            if (crashed.returncode != 99 or not state_file.exists() or
                    not Path(f"/sys/class/net/{tap}").exists()):
                raise AssertionError("QEMU TAP crash did not leave recoverable TAP state")
            run(graphx, "infra", "recover", config, "--state-dir", state)
            if state_file.exists() or Path(f"/sys/class/net/{tap}").exists():
                raise AssertionError("QEMU TAP recovery left TAP or ledger residue")

            for cycle in range(2):
                run(graphx, "infra", "create", config, "--state-dir", state)
                status = run(graphx, "infra", "status", config, "--state-dir", state).stdout
                ledger = state_file.read_text(encoding="utf-8")
                tuntap = run("ip", "tuntap", "show", "dev", tap).stdout
                evidence = {
                    "status kind": "qemu_tap" in status,
                    "ledger format": "version: 2" in ledger,
                    "ledger owner": "tap_owner: 65532:65532" in ledger,
                    "kernel UID": "user 65532" in tuntap,
                    "kernel GID": "group 65532" in tuntap,
                }
                if not all(evidence.values()):
                    raise AssertionError(f"QEMU TAP TAP ownership identity evidence is incomplete: {evidence}; tuntap={tuntap!r}")
                if run("ovs-vsctl", "get", "Port", tap, "tag").stdout.strip() != "42":
                    raise AssertionError("QEMU TAP TAP access VLAN was not realized")
                if "43" not in run("ovs-vsctl", "get", "Port", tap, "trunks").stdout:
                    raise AssertionError("QEMU TAP TAP trunk VLAN was not realized")
                run("ip", "link", "show", "dev", tap)
                if cycle == 0:
                    ledger_before = state_file.read_bytes()
                    port_before = run("ovs-vsctl", "get", "Port", tap, "_uuid").stdout.strip()
                    run("ovs-vsctl", "--", "--if-exists", "del-port", bridge, tap,
                        "--", "add-port", bridge, tap)
                    refused = run(graphx, "infra", "destroy", config, "--state-dir", state,
                                  check=False)
                    port_after = run("ovs-vsctl", "get", "Port", tap, "_uuid").stdout.strip()
                    if (refused.returncode == 0 or port_after == port_before or
                            state_file.read_bytes() != ledger_before or
                            not Path(f"/sys/class/net/{tap}").exists()):
                        raise AssertionError("QEMU TAP destroy did not preserve a replacement OVS Port")
                    run("ovs-vsctl", "--if-exists", "del-br", bridge)
                    run("ip", "tuntap", "delete", "dev", tap, "mode", "tap")
                    state_file.unlink()
                    continue
                run(graphx, "infra", "destroy", config, "--state-dir", state)
                if (state_file.exists() or Path(f"/sys/class/net/{tap}").exists() or
                        run("ovs-vsctl", "br-exists", bridge, check=False).returncode == 0):
                    raise AssertionError("QEMU TAP ordinary destroy left owned resources")
    finally:
        run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
        run("ip", "tuntap", "delete", "dev", tap, "mode", "tap", check=False)
    print("GraphX QEMU TAP live QEMU TAP lifecycle passed twice")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
