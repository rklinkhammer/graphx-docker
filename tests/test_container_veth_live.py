#!/usr/bin/env python3
"""Privileged Linux M4 OVS/container-veth lifecycle regression."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args: object, check: bool = True,
        extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    environment = {**os.environ, "GRAPHX_OVERRIDES": ""}
    if extra_env:
        environment.update(extra_env)
    result = subprocess.run([str(v) for v in args], text=True, capture_output=True,
                            timeout=30, env=environment)
    if check and result.returncode != 0:
        raise AssertionError(f"{' '.join(map(str, args))}\n{result.stdout}\n{result.stderr}")
    return result


def main() -> int:
    if len(sys.argv) != 2 or sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("M4 live test requires: test_container_veth_live.py GRAPHX as Linux root")
    graphx = Path(sys.argv[1]).resolve()
    project, service, container, bridge, host = (
        "graphx-m4-live", "worker", "graphx-m4-live-worker", "br-gxm4live", "gxm4liveh0")
    image = os.environ.get("GRAPHX_M4_TEST_IMAGE", "graphx-demo:latest")

    def start() -> str:
        return run("docker", "run", "-d", "--name", container,
                   "--label", f"com.docker.compose.project={project}",
                   "--label", f"com.docker.compose.service={service}",
                   "--entrypoint", "/bin/sleep", image, "infinity").stdout.strip()

    run("docker", "rm", "-f", container, check=False)
    run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
    try:
        first = start()
        with tempfile.TemporaryDirectory(prefix="graphx-m4-live-", dir="/var/tmp") as raw:
            root = Path(raw)
            state = root / "state"
            config = root / "graphx.yaml"
            config.write_text(f"""\
version: 2
graph:
  id: m4-live
  nodes: [{{ id: worker, kind: source, runtime: docker, ports: [] }}]
  edges: []
transport: {{}}
network:
  networks: [{{ id: data, profile: ethernet, subnets: [10.77.0.0/24], gateway: 10.77.0.1, external: true }}]
  switches: [{{ id: {bridge}, kind: openvswitch, datapath: system, ports: [] }}]
  attachments:
    - {{ id: worker-data, kind: container_veth, owner: worker, network: data,
         address: 10.77.0.2/24, mac: "02:77:00:00:00:02", interface: gxdata0,
         peer: {host}, switch: {bridge}, mtu: 1400 }}
deployment:
  project: {project}
  services:
    worker: {{ image: {image}, command: graphx-generator }}
""", encoding="utf-8")
            run(graphx, "infra", "create", config, "--state-dir", state)
            ledger = (state / "m4-live.yaml").read_text(encoding="utf-8")
            if "version: 2" not in ledger or first not in ledger or "namespace_inode:" not in ledger:
                raise AssertionError("ledger lacks container identity")
            run(graphx, "infra", "status", config, "--state-dir", state)
            link = run("docker", "exec", container, "ip", "-o", "link", "show", "gxdata0").stdout
            address = run("docker", "exec", container, "ip", "-o", "-4", "addr", "show", "gxdata0").stdout
            if "mtu 1400" not in link or "02:77:00:00:00:02" not in link or "10.77.0.2/24" not in address:
                raise AssertionError("container endpoint configuration is incomplete")
            run("docker", "rm", "-f", container)
            second = start()
            status = run(graphx, "infra", "status", config, "--state-dir", state, check=False)
            if status.returncode != 2 or "missing-replaced-or-restarted" not in status.stdout:
                raise AssertionError("container replacement was not detected")
            run(graphx, "infra", "destroy", config, "--state-dir", state)
            run(graphx, "infra", "create", config, "--state-dir", state)
            run(graphx, "infra", "status", config, "--state-dir", state)
            run(graphx, "infra", "destroy", config, "--state-dir", state)
            if first == second:
                raise AssertionError("replacement did not change the full container identity")

            state_file = state / "m4-live.yaml"

            def marker(column: str) -> str:
                return run("ovs-vsctl", "get", "Port", host,
                           f"external_ids:{column}").stdout.strip()

            def replacement_markers() -> list[str]:
                return [
                    f"external_ids:graphx_owner={marker('graphx_owner')}",
                    "external_ids:graphx_attachment=worker-data",
                    f"external_ids:graphx_config_hash={marker('graphx_config_hash')}",
                    "external_ids:graphx_graph=m4-live",
                ]

            def discard_adversarial_run() -> None:
                run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
                run("ip", "link", "delete", host, check=False)
                state_file.unlink(missing_ok=True)

            # A same-name Port with copied markers is still a replacement when
            # its UUID differs. Complete preflight must preserve it and the run.
            run(graphx, "infra", "create", config, "--state-dir", state)
            old_port = run("ovs-vsctl", "get", "Port", host, "_uuid").stdout.strip()
            markers = replacement_markers()
            run("ovs-vsctl", "--", "--if-exists", "del-port", bridge, host,
                "--", "add-port", bridge, host,
                "--", "set", "Port", host, *markers,
                "--", "set", "Interface", host, *markers)
            new_port = run("ovs-vsctl", "get", "Port", host, "_uuid").stdout.strip()
            if new_port == old_port:
                raise AssertionError("Port replacement did not change UUID")
            destroy = run(graphx, "infra", "destroy", config, "--state-dir", state, check=False)
            if (destroy.returncode == 0 or
                    run("ovs-vsctl", "get", "Port", host, "_uuid").stdout.strip() != new_port or
                    run("ovs-vsctl", "br-exists", bridge, check=False).returncode != 0 or
                    not Path(f"/sys/class/net/{host}").exists() or
                    not state_file.exists()):
                raise AssertionError("same-name OVS Port replacement was not preserved")
            discard_adversarial_run()

            # Repoint the recorded Port to a copied-marker Interface with a new
            # UUID. The same complete-set refusal applies to Interface identity.
            run(graphx, "infra", "create", config, "--state-dir", state)
            old_interface = run("ovs-vsctl", "get", "Interface", host, "_uuid").stdout.strip()
            markers = replacement_markers()
            run("ovs-vsctl", "--", "--id=@replacement", "create", "Interface",
                f"name={host}", *markers,
                "--", "set", "Port", host, "interfaces=@replacement")
            new_interface = run("ovs-vsctl", "get", "Interface", host, "_uuid").stdout.strip()
            if new_interface == old_interface:
                raise AssertionError("Interface replacement did not change UUID")
            destroy = run(graphx, "infra", "destroy", config, "--state-dir", state, check=False)
            if (destroy.returncode == 0 or
                    run("ovs-vsctl", "get", "Interface", host, "_uuid").stdout.strip() != new_interface or
                    run("ovs-vsctl", "br-exists", bridge, check=False).returncode != 0 or
                    not Path(f"/sys/class/net/{host}").exists() or
                    not state_file.exists()):
                raise AssertionError("same-name OVS Interface replacement was not preserved")
            discard_adversarial_run()

            # A host link with the recorded name and copied alias is still not
            # owned when its immutable ifindex changed.
            run(graphx, "infra", "create", config, "--state-dir", state)
            owner = marker("graphx_owner").strip('"')
            run("ip", "link", "delete", host)
            run("ip", "link", "add", host, "type", "dummy")
            run("ip", "link", "set", "dev", host, "alias",
                f"graphx:{owner}:worker-data:host")
            destroy = run(graphx, "infra", "destroy", config, "--state-dir", state, check=False)
            if (destroy.returncode == 0 or
                    run("ovs-vsctl", "br-exists", bridge, check=False).returncode != 0 or
                    not Path(f"/sys/class/net/{host}").exists() or
                    not state_file.exists()):
                raise AssertionError("same-name host interface replacement was not preserved")
            discard_adversarial_run()

            # Ordinary rollback and hard-crash recovery must still clean both
            # endpoint and bridge now that bridge deletion checks its internal port.
            failed = run(graphx, "infra", "create", config, "--state-dir", state,
                         check=False, extra_env={"GRAPHX_TEST_FAIL_AFTER_MUTATION": "2"})
            if (failed.returncode == 0 or state_file.exists() or
                    run("ovs-vsctl", "br-exists", bridge, check=False).returncode == 0 or
                    Path(f"/sys/class/net/{host}").exists()):
                raise AssertionError("injected M4 failure did not roll back cleanly")

            crashed = run(graphx, "infra", "create", config, "--state-dir", state,
                          check=False, extra_env={"GRAPHX_TEST_CRASH_AFTER_MUTATION": "2"})
            if (crashed.returncode != 99 or not state_file.exists() or
                    run("ovs-vsctl", "br-exists", bridge, check=False).returncode != 0 or
                    not Path(f"/sys/class/net/{host}").exists()):
                raise AssertionError("injected M4 crash did not leave recoverable state")
            run(graphx, "infra", "recover", config, "--state-dir", state)
            if (state_file.exists() or
                    run("ovs-vsctl", "br-exists", bridge, check=False).returncode == 0 or
                    Path(f"/sys/class/net/{host}").exists()):
                raise AssertionError("M4 crash recovery did not remove owned resources")

            run(graphx, "infra", "create", config, "--state-dir", state)
            run(graphx, "infra", "destroy", config, "--state-dir", state)
    finally:
        run("docker", "rm", "-f", container, check=False)
        run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
        run("ip", "link", "delete", host, check=False)
    print("GraphX M4 live container-veth regressions passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
