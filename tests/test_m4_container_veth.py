#!/usr/bin/env python3
"""Portable M4 configuration, planning, and ownership contracts."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


CONFIG = """\
version: 2
graph:
  id: m4-portable
  nodes:
    - { id: worker, kind: source, runtime: docker, ports: [] }
  edges: []
transport: {}
network:
  networks:
    - { id: data, profile: ethernet, subnets: [10.77.0.0/24], gateway: 10.77.0.1, external: true }
  switches:
    - { id: br-m4-port, kind: openvswitch, datapath: system, ports: [] }
  attachments:
    - id: worker-data
      kind: container_veth
      owner: worker
      network: data
      address: 10.77.0.2/24
      mac: "02:77:00:00:00:02"
      interface: gxdata0
      peer: gxm4host0
      switch: br-m4-port
      mtu: 1400
      routes: [{ destination: 10.78.0.0/24, via: 10.77.0.1 }]
deployment:
  project: graphx-m4
  services:
    worker: { image: graphx-demo:latest, command: graphx-generator }
"""


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(v) for v in args], text=True, capture_output=True,
                            timeout=20, env={**os.environ, "GRAPHX_OVERRIDES": ""})
    if check and result.returncode != 0:
        raise AssertionError(result.stderr)
    return result


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_m4_container_veth.py GRAPHX SOURCE_ROOT")
    graphx, root = Path(sys.argv[1]), Path(sys.argv[2])
    with tempfile.TemporaryDirectory(prefix="graphx-m4-") as raw:
        config = Path(raw) / "graphx.yaml"
        config.write_text(CONFIG, encoding="utf-8")
        run(graphx, "validate", config)
        plan = run(graphx, "infra", "create", config, "--dry-run",
                   "--state-dir", Path(raw) / "state").stdout
        for marker in ("project=graphx-m4", "service=worker", "ip link add gxm4host0",
                       "ovs-vsctl add-port br-m4-port", "address=10.77.0.2/24", "mtu=1400"):
            require(marker in plan, f"missing M4 plan marker: {marker}")
        require("docker network" not in plan and "macvlan" not in plan and "ipvlan" not in plan,
                "M4 must not create a Docker data-plane network")
        missing_project = config.read_text().replace("  project: graphx-m4\n", "")
        config.write_text(missing_project, encoding="utf-8")
        rejected = run(graphx, "validate", config, check=False)
        require(rejected.returncode != 0 and "deployment.project" in rejected.stderr,
                "container attachments require explicit deployment identity")

    source = "\n".join(
        (root / path).read_text(encoding="utf-8")
        for path in (
            "src/infra/lifecycle_coordinator.cpp",
            "src/infra/endpoint_resources.cpp",
            "src/infra/ovs_resources.cpp",
            "src/infra/ownership_state.cpp",
        )
    )
    for marker in ("com.docker.compose.project", "com.docker.compose.service", ".State.Pid",
                   "/ns/net", "namespace_inode", "peer_ifindex", "graphx_attachment",
                   "GRAPHX_M4_CRASH_AFTER", "missing-replaced-or-restarted",
                   "endpoint_names_absent_or_recorded", "internal_port_uuid"):
        require(marker in source, f"missing M4 ownership contract: {marker}")
    print("GraphX M4 portable container-veth contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
