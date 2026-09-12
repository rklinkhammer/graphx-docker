#!/usr/bin/env python3
"""Two instances, identical addresses: isolation, interruption and replacement."""
import contextlib
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args, check=True, env=None):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True, timeout=45,
                            env={**os.environ, "GRAPHX_OVERRIDES": "", **(env or {})})
    if check and result.returncode:
        raise AssertionError(f"{args}\n{result.stdout}\n{result.stderr}")
    return result


def require(value, message):
    if not value:
        raise AssertionError(message)


CONFIG = """version: 2
graph:
  id: instance-live
  nodes: [{id: observer, kind: external, runtime: external, lifecycle: external, ports: []}]
  edges: []
transport: {}
deployment: {instance_id: INSTANCE, project: instance-live}
network:
  networks:
    - {id: data, profile: ethernet, subnets: [10.93.0.0/24], gateway: 10.93.0.1, external: true}
    - {id: aux, profile: ethernet, subnets: [10.95.0.0/24], gateway: 10.95.0.1, external: true}
  switches:
    - id: br-gxinstance
      kind: openvswitch
      ports: [{id: span, interface: gxspanhost, peer: gxspanpeer}]
      mirror: {id: span, output_port: span, select_all: true}
    - {id: br-gxaux, kind: openvswitch, ports: []}
  routers:
    - id: left
      kind: linux_namespace
      namespace: gxinstleft
      interfaces:
        - {id: data, network: data, address: 10.93.0.1/24, device: gxleft, peer: gxlefthost, switch: br-gxinstance}
        - {id: aux, network: aux, address: 10.95.0.1/24, device: gxleftaux, peer: gxleftauxh, switch: br-gxaux}
      routes: [{destination: 10.94.0.0/24, via: 10.93.0.2, device: gxleft, install: manual}]
    - id: right
      kind: linux_namespace
      namespace: gxinstright
      interfaces:
        - {id: data, network: data, address: 10.93.0.2/24, device: gxright, peer: gxrighthost, switch: br-gxinstance}
        - {id: aux, network: aux, address: 10.95.0.2/24, device: gxrightaux, peer: gxrightauxh, switch: br-gxaux}
  attachments:
    - {id: right-aux, kind: namespace_veth, owner: right, network: aux, address: 10.95.0.2/24, interface: gxrightaux, peer: gxrightauxh, switch: br-gxaux}
    - {id: left-aux, kind: namespace_veth, owner: left, network: aux, address: 10.95.0.1/24, interface: gxleftaux, peer: gxleftauxh, switch: br-gxaux}
    - {id: left-data, kind: namespace_veth, owner: left, network: data, address: 10.93.0.1/24, interface: gxleft, peer: gxlefthost, switch: br-gxinstance}
    - {id: right-data, kind: namespace_veth, owner: right, network: data, address: 10.93.0.2/24, interface: gxright, peer: gxrighthost, switch: br-gxinstance}
    - {id: span, kind: mirror, owner: br-gxinstance, interface: gxspanhost, switch: br-gxinstance}
"""


@contextlib.contextmanager
def laboratory_directory():
    root = Path(tempfile.mkdtemp(prefix="graphx-instances-", dir="/var/lib/graphx"))
    try:
        yield root
    except BaseException:
        print(f"Retained instance test evidence: {root}", file=sys.stderr)
        raise
    else:
        shutil.rmtree(root)


def main():
    if len(sys.argv) != 2 or sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("requires GRAPHX and root in an authorized Linux laboratory")
    graphx = Path(sys.argv[1]).resolve()
    with laboratory_directory() as root:
        state = root / "state"
        configs = []
        namespaces = []
        bridges = []
        for instance in ("lab-a", "lab-b"):
            config = root / f"{instance}.yaml"
            config.write_text(CONFIG.replace("INSTANCE", instance))
            configs.append(config)
            plan = run(graphx, "infra", "create", config, "--dry-run", "--state-dir", state).stdout
            namespaces.append([line.split()[3] for line in plan.splitlines()
                               if line.startswith("ip netns add ")])
            bridges.append(next(line.split()[3] for line in plan.splitlines()
                                if line.startswith("ovs-vsctl -- add-br ")))
        def lifecycle(action, index, **kwargs):
            return run(graphx, "infra", action, configs[index], "--state-dir", state, **kwargs)
        def ping(index):
            run("ip", "netns", "exec", namespaces[index][0], "ping", "-c", "1", "-W", "2", "10.93.0.2")
        try:
            lifecycle("create", 0)
            lifecycle("create", 1)
            ping(0)
            ping(1)
            ledgers = {p.name: p.read_bytes() for p in state.glob("*.yaml")}
            require(len(ledgers) == 2, "instances did not get separate ledgers")
            require(lifecycle("create", 0, check=False).returncode != 0, "duplicate create accepted")
            require(ledgers == {p.name: p.read_bytes() for p in state.glob("*.yaml")},
                    "duplicate create changed a ledger")
            paths = sorted(state.glob("*.yaml"))
            original = [path.read_bytes() for path in paths]
            paths[0].write_bytes(original[1])
            paths[1].write_bytes(original[0])
            try:
                require(lifecycle("destroy", 0, check=False).returncode != 0,
                        "cross-instance ledger accepted for cleanup")
                refused = run(graphx, "infra", "route", "apply", configs[0], "--router", "left",
                              "--destination", "10.94.0.0/24", "--state-dir", state, check=False)
                require(refused.returncode != 0, "cross-instance ledger accepted for routing")
                ping(0)
                ping(1)
            finally:
                for path, data in zip(paths, original):
                    path.write_bytes(data)
            run(graphx, "infra", "route", "apply", configs[0], "--router", "left",
                "--destination", "10.94.0.0/24", "--state-dir", state)
            require("10.94.0.0/24" not in run("ip", "netns", "exec", namespaces[1][0],
                                             "ip", "route").stdout, "route escaped instance")
            lifecycle("destroy", 0)
            lifecycle("status", 1)
            ping(1)
            # A failed transaction and a crashed transaction must preserve B.
            for variable, expected in (("GRAPHX_TEST_FAIL_AFTER_MUTATION", None),
                                       ("GRAPHX_TEST_CRASH_AFTER_MUTATION", 99)):
                failed = lifecycle("create", 0, check=False, env={variable: "4"})
                require(failed.returncode != 0, "interruption hook failed")
                if expected is not None:
                    require(failed.returncode == expected, "unexpected crash exit")
                    lifecycle("recover", 0)
                lifecycle("status", 1)
                ping(1)
            # Existing short physical names cannot be adopted.
            run("ovs-vsctl", "add-br", bridges[0])
            collision_uuid = run("ovs-vsctl", "get", "Bridge", bridges[0], "_uuid").stdout
            try:
                require(lifecycle("create", 0, check=False).returncode != 0, "collision adopted")
                require(run("ovs-vsctl", "get", "Bridge", bridges[0], "_uuid").stdout == collision_uuid,
                        "collision resource replaced")
            finally:
                run("ovs-vsctl", "del-br", bridges[0])
            lifecycle("create", 0)
            run("ovs-vsctl", "set", "Bridge", bridges[0], "external_ids:graphx_graph=replacement")
            require(lifecycle("destroy", 0, check=False).returncode != 0, "replacement accepted")
            lifecycle("status", 1)
            ping(1)
            run("ovs-vsctl", "set", "Bridge", bridges[0], "external_ids:graphx_graph=instance-live")
            lifecycle("destroy", 0)
            lifecycle("destroy", 1)
            require(not list(state.glob("*.yaml")), "ledger survived completed destroy")
        finally:
            # Cleanup through production ownership checks; preserve unexpected replacements.
            for index in (0, 1):
                lifecycle("destroy", index, check=False)
                lifecycle("recover", index, check=False)
    print("Two-instance OVS ownership lifecycle passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
