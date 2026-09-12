#!/usr/bin/env python3
"""Privileged Linux network-lab router, profile-flow, and mirror lifecycle regression."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args: object, check: bool = True,
        extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    value = subprocess.run([str(v) for v in args], text=True, capture_output=True, timeout=45,
                           env={**os.environ, "GRAPHX_OVERRIDES": "", **(extra_env or {})})
    if check and value.returncode:
        raise AssertionError(f"{' '.join(map(str, args))}\n{value.stdout}\n{value.stderr}")
    return value


def main() -> int:
    if len(sys.argv) != 2 or sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("network-lab live test requires: test_network_lab_live.py GRAPHX as Linux root")
    graphx = Path(sys.argv[1]).resolve()
    image = os.environ.get("GRAPHX_NETWORK_LAB_TEST_IMAGE", "debian:bookworm-slim")
    project = "graphx-network-lab-live"
    containers = {"left": "graphx-network-lab-live-left", "right": "graphx-network-lab-live-right"}
    bridges = ("br-gxlableft", "br-gxlabright")
    namespace = "gxlabrouter"
    links = ("gxlablh", "gxlabrh", "gxlablrh", "gxlabrrh", "gxlabclo", "gxlabcli",
             "gxlabcro", "gxlabcri", "gxlabexto", "gxlabexti")

    for name in containers.values():
        run("docker", "rm", "-f", name, check=False)
    for bridge in bridges:
        run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
    run("ip", "netns", "delete", namespace, check=False)
    for link in links:
        run("ip", "link", "delete", link, check=False)

    try:
        for service, name in containers.items():
            run("docker", "run", "-d", "--name", name,
                "--label", f"com.docker.compose.project={project}",
                "--label", f"com.docker.compose.service={service}",
                "--entrypoint", "/bin/sleep", image, "infinity")
        with tempfile.TemporaryDirectory(prefix="graphx-network-lab-live-", dir="/var/tmp") as raw:
            root = Path(raw)
            state, config = root / "state", root / "graphx.yaml"
            config.write_text(f"""\
version: 2
graph:
  id: network-lab-live
  nodes:
    - {{id: left, kind: source, runtime: docker, ports: []}}
    - {{id: right, kind: sink, runtime: docker, ports: []}}
  edges: []
transport: {{}}
network:
  networks:
    - {{id: left-net, profile: ipvlan-l2, subnets: [10.88.1.0/24], gateway: 10.88.1.1, uplink: semantic-left, external: true}}
    - {{id: right-net, profile: ipvlan-l2, subnets: [10.88.2.0/24], gateway: 10.88.2.1, uplink: semantic-right, external: true}}
  switches:
    - id: {bridges[0]}
      kind: openvswitch
      ports: [{{id: capture, interface: gxlabclo, peer: gxlabcli}}]
      mirror: {{id: m5-left-span, output_port: capture, select_all: true}}
    - id: {bridges[1]}
      kind: openvswitch
      ports: [{{id: capture, interface: gxlabcro, peer: gxlabcri}}]
      mirror: {{id: m5-right-span, output_port: capture, select_all: true}}
  routers:
    - id: router
      kind: linux_namespace
      namespace: {namespace}
      forwarding: true
      interfaces:
        - {{id: left, network: left-net, address: 10.88.1.1/24, device: gxlablrn, peer: gxlablrh, switch: {bridges[0]}}}
        - {{id: right, network: right-net, address: 10.88.2.1/24, device: gxlabrrn, peer: gxlabrrh, switch: {bridges[1]}}}
      policies:
        - {{id: left-right, source: 10.88.1.0/24, destination: 10.88.2.0/24, action: accept}}
        - {{id: right-left, source: 10.88.2.0/24, destination: 10.88.1.0/24, action: accept}}
  attachments:
    - {{id: left-data, kind: container_veth, owner: left, network: left-net, address: 10.88.1.10/24, mac: "02:88:00:00:00:01", interface: gxdata0, peer: gxlablh, switch: {bridges[0]}, routes: [{{destination: 10.88.2.0/24, via: 10.88.1.1}}]}}
    - {{id: right-data, kind: container_veth, owner: right, network: right-net, address: 10.88.2.20/24, mac: "02:88:00:00:00:01", interface: gxdata0, peer: gxlabrh, switch: {bridges[1]}, routes: [{{destination: 10.88.1.0/24, via: 10.88.2.1}}]}}
    - {{id: router-left, kind: namespace_veth, owner: router, network: left-net, address: 10.88.1.1/24, mac: "02:88:00:00:00:01", interface: gxlablrn, peer: gxlablrh, switch: {bridges[0]}}}
    - {{id: router-right, kind: namespace_veth, owner: router, network: right-net, address: 10.88.2.1/24, mac: "02:88:00:00:00:01", interface: gxlabrrn, peer: gxlabrrh, switch: {bridges[1]}}}
    - {{id: m5-left-span, kind: mirror, owner: {bridges[0]}, interface: gxlabclo, switch: {bridges[0]}}}
    - {{id: m5-right-span, kind: mirror, owner: {bridges[1]}, interface: gxlabcro, switch: {bridges[1]}}}
deployment:
  project: {project}
  services:
    left: {{image: {image}, command: sleep}}
    right: {{image: {image}, command: sleep}}
""", encoding="utf-8")
            for mutation in (3, 8):
                crashed = run(graphx, "infra", "create", config, "--state-dir", state,
                              check=False,
                              extra_env={"GRAPHX_TEST_CRASH_AFTER_MUTATION": str(mutation)})
                if crashed.returncode != 99:
                    raise AssertionError(
                        f"network-lab crash hook {mutation} returned {crashed.returncode}, expected 99")
                run(graphx, "infra", "recover", config, "--state-dir", state)
                if (state / "network-lab-live.yaml").exists():
                    raise AssertionError(f"network-lab crash hook {mutation}: ledger survived recovery")
                for bridge in bridges:
                    if run("ovs-vsctl", "br-exists", bridge, check=False).returncode == 0:
                        raise AssertionError(
                            f"network-lab crash hook {mutation}: bridge survived recovery")
                if run("ip", "netns", "exec", namespace, "true", check=False).returncode == 0:
                    raise AssertionError(
                        f"network-lab crash hook {mutation}: namespace survived recovery")
            for cycle in range(2):
                run(graphx, "validate", config)
                run(graphx, "infra", "create", config, "--state-dir", state)
                status = run(graphx, "infra", "status", config, "--state-dir", state).stdout
                if "linux_namespace" not in status or "mirror" not in status:
                    raise AssertionError("network-lab status omitted namespace or mirror ownership")
                ledger = (state / "network-lab-live.yaml").read_text()
                if "version: 2" not in ledger or "mirror_uuid:" not in ledger:
                    raise AssertionError("ownership ledger omitted mirror identity")
                left_pid = run("docker", "inspect", "-f", "{{.State.Pid}}", containers["left"]).stdout.strip()
                run("nsenter", "-t", left_pid, "-n", "ping", "-c", "2", "-W", "2", "10.88.2.20")
                flows = run("ovs-ofctl", "dump-flows", bridges[0]).stdout
                if "nw_dst=10.88.1.10" not in flows or "arp_tpa=10.88.1.1" not in flows:
                    raise AssertionError("IPvlan semantic flows are incomplete")
                if cycle == 0:
                    ledger_path = state / "network-lab-live.yaml"
                    ledger_before = ledger_path.read_bytes()
                    mirror_before = run(
                        "ovs-vsctl", "--data=bare", "--no-heading", "--columns=_uuid",
                        "find", "Mirror", "name=m5-left-span").stdout.strip()
                    run("ip", "link", "add", "gxlabexto", "type", "veth", "peer", "name",
                        "gxlabexti")
                    run("ovs-vsctl", "add-port", bridges[0], "gxlabexto")
                    refused = run(graphx, "infra", "destroy", config, "--state-dir", state,
                                  check=False)
                    if refused.returncode == 0 or "unexpected ports" not in refused.stderr:
                        raise AssertionError("network-lab destroy accepted an unrecorded OVS bridge port")
                    if ledger_path.read_bytes() != ledger_before:
                        raise AssertionError("network-lab refusal changed the ownership ledger")
                    for bridge in bridges:
                        run("ovs-vsctl", "br-exists", bridge)
                    run("ip", "netns", "exec", namespace, "true")
                    for link in links[:-2]:
                        run("ip", "link", "show", "dev", link)
                    mirror_after = run(
                        "ovs-vsctl", "--data=bare", "--no-heading", "--columns=_uuid",
                        "find", "Mirror", "name=m5-left-span").stdout.strip()
                    if not mirror_before or mirror_after != mirror_before:
                        raise AssertionError("network-lab refusal changed the owned Mirror identity")
                    run("ovs-vsctl", "del-port", bridges[0], "gxlabexto")
                    run("ip", "link", "delete", "dev", "gxlabexto")
                run(graphx, "infra", "destroy", config, "--state-dir", state)
                for bridge in bridges:
                    if run("ovs-vsctl", "br-exists", bridge, check=False).returncode == 0:
                        raise AssertionError(f"cycle {cycle}: bridge survived destroy")
                if run("ip", "netns", "exec", namespace, "true", check=False).returncode == 0:
                    raise AssertionError(f"cycle {cycle}: namespace survived destroy")
    finally:
        for name in containers.values():
            run("docker", "rm", "-f", name, check=False)
        for bridge in bridges:
            run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
        run("ip", "netns", "delete", namespace, check=False)
        for link in links:
            run("ip", "link", "delete", link, check=False)
    print("GraphX network-lab live laboratory lifecycle passed twice")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
