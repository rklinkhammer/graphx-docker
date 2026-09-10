#!/usr/bin/env python3
"""Portable static contract checks for the optional GraphX M1 Lima VM."""

from __future__ import annotations

from pathlib import Path
import re
import subprocess
import sys

def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_lima_environment.py SOURCE_ROOT")
    root = Path(sys.argv[1]).resolve()
    lima = root / "infrastructure" / "lima"
    expected = {"graphx.yaml", "provision.sh", "start.sh", "verify.sh", "stop.sh", "README.md"}
    require(expected <= {path.name for path in lima.iterdir()}, "required M1 files are missing")

    raw = (lima / "graphx.yaml").read_text(encoding="utf-8")
    for token in (
        "minimumLimaVersion: 2.2.0", "vmType: vz", "arch: aarch64",
        "digest: sha256:", "cpus: 4", "memory: 8GiB", "disk: 80GiB",
        "mountType: virtiofs", 'location: "{{.Param.repo}}"',
        "mountPoint: /workspace/graphx-docker", "system: false", "user: false",
        "hostIP: 127.0.0.1", "hostPort: 18080", "guestIP: 0.0.0.0",
        "guestIPMustBeZero: false", "proto: any", "ignore: true",
    ):
        require(token in raw, f"Lima definition omits {token}")
    require(raw.count("  - location:") == 2, "unexpected image or host mounts were added")
    require("guestSocket" not in raw and "hostSocket" not in raw, "privileged socket forwarding is forbidden")
    require("docker buildx version >/dev/null" in raw,
            "Lima readiness does not require the Buildx plugin")
    require('id -nG "{{.User}}" | grep -qw docker' in raw,
            "Lima readiness does not require login-user Docker access")

    scripts = [lima / name for name in ("common.sh", "provision.sh", "start.sh", "stop.sh", "verify.sh")]
    for script in scripts:
        subprocess.run(["bash", "-n", str(script)], check=True)
        text = script.read_text(encoding="utf-8")
        require("rm -rf" not in text and "rm -fr" not in text, f"unsafe recursive deletion in {script.name}")
    provision = (lima / "provision.sh").read_text(encoding="utf-8")
    for token in ("docker.io", "docker-buildx", "docker-compose-v2", "openvswitch-switch", "nftables", "qemu-system-ppc", "tcpdump", "tshark", "/var/lib/graphx"):
        require(token in provision, f"provisioning omits {token}")
    require("docker buildx version" in provision,
            "provisioning does not record the required Buildx plugin")
    require('usermod --append --groups docker "${GRAPHX_LIMA_USER}"' in provision,
            "provisioning does not grant the Lima user Docker access")
    require('runuser --user "${GRAPHX_LIMA_USER}" -- docker info' in provision,
            "provisioning does not prove Lima-user Docker access")
    require("install -d -m 0700 /var/lib/graphx/runs" in provision,
            "the GraphX ownership-ledger parent must enforce mode 0700")
    require(re.search(r"timeout [0-9]+", provision) is not None, "package/service operations are not bounded")

    start = (lima / "start.sh").read_text(encoding="utf-8")
    for token in ("Refreshing the Lima login session", 'limactl stop "${GRAPHX_M1_INSTANCE}"',
                  "docker info >/dev/null && docker buildx version >/dev/null"):
        require(token in start, f"Lima start lifecycle omits {token}")

    verify = (lima / "verify.sh").read_text(encoding="utf-8")
    for name in ("gx-m1-br", "gx-m1-ns", "gx-m1-vh", "gx-m1-vn", "gx-m1-tap", "gx-m1-int", "gx-m1-docker"):
        require(name in verify, f"verification omits fixed name {name}")
    for token in ("external_ids:graphx_m1_owner", "graphx-m1:", "datapath_type=system", "ip tuntap", "netem", "nft", "tcpdump", "docker buildx version", "Lima login user cannot access rootful Docker", "scripts/verify.sh quick", "GRAPHX_DEV_BUILD_DIR=/var/lib/graphx/m1/build/dev", "project graphx.yaml --check", "snapshot before", "snapshot after"):
        require(token in verify, f"verification omits {token}")
    require("/workspace/graphx-docker" in verify and "/var/lib/graphx" in verify, "storage boundary is not verified")
    require("GRAPHX_M1_TEST_FAIL_AFTER" in verify, "bounded failure injection hook is missing")
    require("set +e" not in verify, "cleanup must not disable fail-fast mode")
    for token in (
        '[[ ! -e ${state_dir} ]]', 'mkdir -- "${state_dir}"',
        "bridge.uuid", "internal.ifindex", "namespace.inode", "veth-host.ifindex",
        "veth-ns.ifindex", "tap.ifindex", "assert_clean", "cleanup-errors.txt",
        'rotate_evidence "$((evidence_count_limit - 1))"',
        'rotate_evidence "${evidence_count_limit}"',
    ):
        require(token in verify, f"transactional verification omits {token}")
    for stage in (
        "state", "docker", "bridge", "internal", "namespace-created",
        "namespace-owned", "veth-created", "veth-owned", "veth-attached",
        "tap-created", "tap-owned", "tap-attached", "netem", "topology",
        "capture-started",
    ):
        require(f"maybe_fail {stage}" in verify, f"failure injection omits {stage}")
    require('startsWith("gx-m1-")' not in verify and 'startswith("gx-m1-")' not in verify,
            "snapshots must not hide disposable-name replacements")
    require("grep -vE ' gx-m1-'" not in verify and 'grep -v "${bridge}"' not in verify,
            "snapshots must include exact disposable resources")

    docs = "\n".join((root / name).read_text(encoding="utf-8") for name in ("README.md", "SUPPORT.md", "docs/security.md", "docs/GraphX_Architecture.md"))
    for statement in ("configuration version 2", "veth", "TAP", "slirp"):
        require(statement.lower() in docs.lower(), f"documentation does not state M1 boundary: {statement}")
    print("Lima M1 static contract checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
