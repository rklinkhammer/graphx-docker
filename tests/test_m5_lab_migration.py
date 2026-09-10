#!/usr/bin/env python3
import hashlib
import os
from pathlib import Path
import subprocess
import sys


LEGACY_HASHES = {
    "mixed-network": "ede943d05d0e70cd4bb36970bf94be9ea81787568f313766a807a3c14b6b65fc",
    "macvlan": "40e09c1cb7ed78b118efeb10dbfe42fc8b06eb10312091f4df7afd9f0920f69d",
    "ipvlan-l2": "da18ff3dfd11a72a834dd6933e161db0a18b92a1ddfc352340208fd274f40f37",
    "ipvlan-l3": "598abfa52b274b8d6a3362bc67b141f0852dc590a61b552d99c5960d3b06864b",
}


def run(*args: object) -> subprocess.CompletedProcess[str]:
    value = subprocess.run([str(v) for v in args], text=True, capture_output=True,
                           timeout=30, env={**os.environ, "GRAPHX_OVERRIDES": ""})
    if value.returncode:
        raise AssertionError(value.stderr or value.stdout)
    return value


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_m5_lab_migration.py GRAPHX SOURCE_ROOT")
    graphx, root = Path(sys.argv[1]), Path(sys.argv[2])
    labs = [
        root / "examples/mixed-network/graphx-ovs.yaml",
        root / "examples/macvlan/graphx-ovs.yaml",
        root / "examples/ipvlan-l2/graphx-ovs.yaml",
        root / "examples/ipvlan-l3/graphx-ovs.yaml",
        root / "examples/static-route-policy/graphx-ovs.yaml",
        root / "examples/sdr-node/external/graphx-ovs.yaml",
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

    for lab, expected in LEGACY_HASHES.items():
        output = run(graphx, "infra", "create", root / "examples" / lab / "graphx.yaml",
                     "--dry-run").stdout.encode()
        require(hashlib.sha256(output).hexdigest() == expected,
                f"M0 legacy fingerprint changed: {lab}")

    for lab in ("mixed-network", "macvlan", "ipvlan-l2", "ipvlan-l3"):
        compose = (root / "examples" / lab / "compose.ovs.yaml").read_text()
        require("driver: macvlan" not in compose and "driver: ipvlan" not in compose and
                "external: true" not in compose,
                f"active OVS compose still declares a Docker data plane: {lab}")
        for action in ("up", "down", "status"):
            require((root / "examples" / lab / "scripts" / f"ovs-{action}.sh").is_file(),
                    f"missing {lab} OVS {action} wrapper")

    for script in (
        root / "examples/static-route-policy/scripts/ovs-lab.sh",
        root / "examples/sdr-node/external/scripts/ovs-lab.sh",
    ):
        require(script.is_file(), f"missing external-boundary M5 launcher: {script}")
        require(os.access(script, os.X_OK), f"M5 launcher is not executable: {script}")
        subprocess.run(["bash", "-n", str(script)], check=True)
        launcher = script.read_text()
        require("graphx-ovs.yaml" in launcher and "external-ovs-boundary.sh" in launcher,
                f"M5 launcher does not select the common realization and boundary helper: {script}")
        require("trap rollback_up ERR" in launcher,
                f"M5 external-boundary launcher lacks ownership-safe rollback: {script}")
        if script.parent.parent.name == "static-route-policy":
            require("apply-route)" in launcher and "clear-route)" in launcher,
                    "M5 route launcher omits the declared manual transition")
    boundary = root / "examples/external-ovs-boundary.sh"
    require(boundary.is_file() and os.access(boundary, os.X_OK),
            "missing executable M5 external-boundary helper")
    sdr_compose = (root / "examples/sdr-node/external/compose.ovs.yaml").read_text()
    require("driver: macvlan" not in sdr_compose and "driver: ipvlan" not in sdr_compose
            and "external: true" not in sdr_compose,
            "active SDR OVS compose still declares a Docker data plane")

    source = (root / "src/ownership.cpp").read_text()
    for marker in ("linux_namespace", "create_namespace_endpoint", "create_mirror_endpoint",
                   "install_profile_flows", "net.ipv4.ip_forward=1", "nft", "ovs-ofctl"):
        require(marker in source, f"missing M5 realization marker: {marker}")
    print("GraphX M5 portable laboratory-migration contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
