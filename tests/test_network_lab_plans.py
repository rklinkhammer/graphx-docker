#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import sys


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    value = subprocess.run([str(v) for v in args], text=True, capture_output=True,
                           timeout=30, env={**os.environ, "GRAPHX_OVERRIDES": ""})
    if check and value.returncode:
        raise AssertionError(value.stderr or value.stdout)
    return value


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_network_lab_plans.py GRAPHX SOURCE_ROOT")
    graphx, root = Path(sys.argv[1]), Path(sys.argv[2])
    labs = [
        root / "examples/mixed-network/graphx.yaml",
        root / "examples/macvlan/graphx.yaml",
        root / "examples/ipvlan-l2/graphx.yaml",
        root / "examples/ipvlan-l3/graphx.yaml",
        root / "examples/static-route-policy/graphx.yaml",
        root / "examples/sdr-node/external/graphx.yaml",
    ]
    for config in labs:
        require(config.is_file(), f"missing migrated lab: {config}")
        run(graphx, "validate", config)
        plan = run(graphx, "infra", "create", config, "--dry-run").stdout
        require("ovs-vsctl -- add-br" in plan, f"missing OVS realization: {config}")
        require("docker network" not in plan, f"Docker data plane leaked into: {config}")

    mixed_plan = run(graphx, "infra", "create", labs[0], "--dry-run").stdout
    for marker in ("project=mixed-network", "netns[domain-router]", "mirror mirror-mac",
                   "mirror mirror-ipv", "ip netns add", "net.ipv4.ip_forward=1",
                   "nft add rule", "ovs-ofctl add-flow"):
        require(marker in mixed_plan, f"mixed lab lacks {marker}")
    require("policy=established-return ct-state=established,related action=accept" in mixed_plan,
            "M5 routed plans must permit return traffic for accepted connections")

    route_apply = run(graphx, "infra", "route", "apply", labs[4], "--router",
                      "route-router", "--destination", "10.64.30.10/32",
                      "--dry-run").stdout
    route_clear = run(graphx, "infra", "route", "clear", labs[4], "--router",
                      "route-router", "--destination", "10.64.30.10/32",
                      "--dry-run").stdout
    require("ip route replace 10.64.30.10/32 via 10.64.3.10 dev rt-right" in route_apply,
            "M5 manual route apply is not realizable")
    require("ip route delete 10.64.30.10/32" in route_clear,
            "M5 manual route clear is not realizable")

    compatibility = root / "examples/compatibility/v1"
    for fixture in compatibility.glob("*.yaml"):
        run(graphx, "validate", fixture)
        run(graphx, "inspect", fixture)
        first = run(graphx, "config", "migrate", fixture).stdout
        second = run(graphx, "config", "migrate", fixture).stdout
        require(first == second and first.startswith("# GraphX deterministic migration"),
                f"legacy fixture is not deterministically migratable: {fixture}")
        refused = run(graphx, "infra", "create", fixture, "--dry-run", check=False)
        require(refused.returncode != 0 and "retired in M8" in refused.stderr and
                "config migrate" in refused.stderr,
                f"legacy infrastructure was not retired: {fixture}")

    compose = (root / "examples/network-lab.compose.yaml").read_text()
    require("driver: macvlan" not in compose and "driver: ipvlan" not in compose and
            "external: true" not in compose,
            "shared OVS Compose still declares a Docker data plane")
    for lab in ("mixed-network", "macvlan", "ipvlan-l2", "ipvlan-l3"):
        for action in ("up", "down", "status"):
            require((root / "examples" / lab / "scripts" / f"{action}.sh").is_file(),
                    f"missing {lab} canonical {action} wrapper")

    for script in (
        root / "examples/static-route-policy/scripts/demo.sh",
        root / "examples/sdr-node/external/scripts/demo.sh",
    ):
        require(script.is_file(), f"missing external-boundary M5 launcher: {script}")
        require(os.access(script, os.X_OK), f"M5 launcher is not executable: {script}")
        subprocess.run(["bash", "-n", str(script)], check=True)
        launcher = script.read_text()
        require("graphx.yaml" in launcher and "external-ovs-boundary.sh" in launcher,
                f"M5 launcher does not select the common realization and boundary helper: {script}")
        require("trap rollback_up ERR" in launcher,
                f"M5 external-boundary launcher lacks ownership-safe rollback: {script}")
        if script.parent.parent.name == "static-route-policy":
            require("apply-route)" in launcher and "clear-route)" in launcher,
                    "M5 route launcher omits the declared manual transition")
    boundary = root / "examples/external-ovs-boundary.sh"
    require(boundary.is_file() and os.access(boundary, os.X_OK),
            "missing executable M5 external-boundary helper")
    sdr_compose = (root / "examples/sdr-node/external/compose.yaml").read_text()
    require("driver: macvlan" not in sdr_compose and "driver: ipvlan" not in sdr_compose
            and "external: true" not in sdr_compose,
            "active SDR OVS compose still declares a Docker data plane")
    require('user: "${GRAPHX_HOST_UID:?set by demo.sh}:${GRAPHX_HOST_GID:?set by demo.sh}"'
            in sdr_compose,
            "external SDR services must be able to read invoking-user TLS credentials")

    print("GraphX M5 portable laboratory-migration contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
