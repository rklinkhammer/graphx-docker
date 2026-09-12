#!/usr/bin/env python3
"""Linux-root M8 proof that canonical Compose creates no Docker data plane."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(arg) for arg in args], text=True, capture_output=True, timeout=90,
        env={**os.environ, "GRAPHX_OVERRIDES": ""},
    )
    if check and result.returncode:
        raise AssertionError(
            f"{' '.join(map(str, args))}\n{result.stdout}\n{result.stderr}"
        )
    return result


def driver_networks() -> set[str]:
    result: set[str] = set()
    for driver in ("macvlan", "ipvlan"):
        output = run("docker", "network", "ls", "--filter", f"driver={driver}",
                     "--format", "{{.ID}}").stdout
        result.update(output.split())
    return result


def main() -> int:
    if len(sys.argv) != 3 or sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit(
            "M8 live test requires: test_compatibility_closure_live.py GRAPHX SOURCE_ROOT as Linux root"
        )
    graphx, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    labs = (
        ("macvlan", "macvlan-pipeline", ("gxb9b585f04998d",)),
        ("ipvlan-l2", "ipvlan-l2-pipeline",
         ("br-l2-gen", "br-l2-xform", "br-l2-sink")),
        ("ipvlan-l3", "ipvlan-l3-pipeline", ("gxbc96833c8a7d9",)),
        ("mixed-network", "mixed-network", ("br-gx-mac", "br-gx-ipv")),
    )
    before = driver_networks()
    image = run("docker", "image", "inspect", "graphx-demo:latest", check=False)
    if image.returncode:
        raise AssertionError(
            "graphx-demo:latest is required; run the repository image build gate first"
        )
    for relative, project, bridges in labs:
        example = root / "examples" / relative
        compose = root / "examples/network-lab.compose.yaml"
        config = example / "graphx.yaml"
        compose_command = (
            "env", f"GRAPHX_REPO_ROOT={root}", f"GRAPHX_LAB_CONFIG={config}",
            "docker", "compose", "-p", project, "-f", compose,
        )
        run(*compose_command, "down", "--remove-orphans",
            check=False)
        for bridge in bridges:
            run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
        try:
            run(*compose_command, "up", "-d", "--no-build")
            with tempfile.TemporaryDirectory(prefix="graphx-m8-live-",
                                             dir="/var/tmp") as raw:
                state = Path(raw) / "state"
                try:
                    for cycle in range(2):
                        run(graphx, "infra", "create", config, "--state-dir", state)
                        status = run(graphx, "infra", "status", config,
                                     "--state-dir", state).stdout
                        if "container_veth" not in status or not all(
                                bridge in status for bridge in bridges):
                            raise AssertionError(
                                f"{relative}: status omitted canonical OVS/veth ownership"
                            )
                        for bridge in bridges:
                            run("ovs-vsctl", "br-exists", bridge)
                        if driver_networks() != before:
                            raise AssertionError(
                                f"{relative}: startup created a Docker data plane"
                            )
                        run(graphx, "infra", "destroy", config,
                            "--state-dir", state)
                        for bridge in bridges:
                            if run("ovs-vsctl", "br-exists", bridge,
                                   check=False).returncode == 0:
                                raise AssertionError(
                                    f"{relative} cycle {cycle}: bridge survived destroy"
                                )
                        if (state / f"{project}.yaml").exists():
                            raise AssertionError(
                                f"{relative} cycle {cycle}: ledger survived destroy"
                            )
                finally:
                    run(graphx, "infra", "destroy", config, "--state-dir", state,
                        check=False)
        finally:
            run(*compose_command, "down", "--remove-orphans",
                check=False)
            for bridge in bridges:
                run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
    if driver_networks() != before:
        raise AssertionError("M8 live cleanup changed Docker macvlan/ipvlan networks")
    print("GraphX M8 canonical OVS-only lab lifecycles passed twice")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
