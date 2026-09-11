#!/usr/bin/env python3
"""Portable static contract checks for the optional GraphX M1 Lima VM."""

from __future__ import annotations

from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

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
        "node --version | grep -Eq '^v24\\.'", 'import("node:sqlite")',
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
    dispatcher = root / "scripts" / "network-lab.sh"
    require(dispatcher.is_file() and os.access(dispatcher, os.X_OK),
            "cross-platform network-lab dispatcher is missing or not executable")
    subprocess.run(["bash", "-n", str(dispatcher)], check=True)
    dispatcher_text = dispatcher.read_text(encoding="utf-8")
    for token in (
        "macvlan|ipvlan-l2|ipvlan-l3|mixed-network",
        "plan|up|status|down", "graphx_m1_assert_identity",
        "/var/lib/graphx/m1/build/dev/graphx", "GRAPHX_LIMA_GRAPHX_BIN",
        'limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}"',
        'scripts/network-lab.sh "${lab}" "${action}"',
    ):
        require(token in dispatcher_text, f"network-lab dispatcher omits {token}")
    rejected = subprocess.run(
        [str(dispatcher), "not-a-lab", "up"], text=True, capture_output=True
    )
    require(rejected.returncode == 64 and "unsupported network laboratory" in rejected.stderr,
            "network-lab dispatcher does not reject an unknown lab before execution")

    with tempfile.TemporaryDirectory(prefix="graphx-network-lab-test-") as temporary:
        temporary_path = Path(temporary)
        invocation_log = temporary_path / "invocations"
        fake_uname = temporary_path / "uname"
        fake_graphx = temporary_path / "graphx"
        fake_uname.write_text("#!/bin/sh\nprintf 'Linux\\n'\n", encoding="utf-8")
        fake_graphx.write_text(
            f"#!/bin/sh\nprintf '%s\\n' \"$*\" >'{invocation_log}'\n", encoding="utf-8"
        )
        fake_uname.chmod(0o755)
        fake_graphx.chmod(0o755)
        environment = {**os.environ, "PATH": f"{temporary}:{os.environ['PATH']}",
                       "GRAPHX_BIN": str(fake_graphx)}
        subprocess.run([str(dispatcher), "ipvlan-l2", "plan"], env=environment, check=True)
        invocation = invocation_log.read_text(encoding="utf-8")
        require("infra create" in invocation and
                str(root / "examples/ipvlan-l2/graphx.yaml") in invocation and
                "--dry-run" in invocation,
                "Linux network-lab plan does not select the requested version-2 topology")

    config_digest = subprocess.run(
        ["bash", "-c", f'source "{lima / "common.sh"}"; graphx_m1_digest'],
        text=True, capture_output=True, check=True
    ).stdout.strip()
    with tempfile.TemporaryDirectory(prefix="graphx-network-lab-lima-test-") as temporary:
        temporary_path = Path(temporary)
        invocation_log = temporary_path / "invocations"
        fake_uname = temporary_path / "uname"
        fake_limactl = temporary_path / "limactl"
        fake_uname.write_text(
            "#!/bin/sh\ncase \"${1:-}\" in -m) printf 'arm64\\n' ;; *) printf 'Darwin\\n' ;; esac\n",
            encoding="utf-8",
        )
        fake_limactl.write_text(
            "#!/bin/sh\n"
            "if [ \"$1\" = list ]; then printf '%s\\n' \"$GRAPHX_TEST_INSTANCE\"; exit 0; fi\n"
            "if [ \"$1\" = shell ]; then printf '%s\\n' \"$*\" >>\"$GRAPHX_TEST_LOG\"; exit 0; fi\n"
            "exit 1\n",
            encoding="utf-8",
        )
        fake_uname.chmod(0o755)
        fake_limactl.chmod(0o755)
        environment = {
            **os.environ,
            "PATH": f"{temporary}:{os.environ['PATH']}",
            "GRAPHX_TEST_LOG": str(invocation_log),
            "GRAPHX_TEST_INSTANCE":
                f"graphx|Running|aarch64|vz|{root}|{config_digest}",
        }
        subprocess.run([str(dispatcher), "macvlan", "status"], env=environment, check=True)
        invocations = invocation_log.read_text(encoding="utf-8").splitlines()
        require(len(invocations) == 2, "macOS dispatcher did not preflight and invoke Lima")
        require("test -x" in invocations[0] and "docker info" in invocations[0]
                and "openvswitch-switch.service" in invocations[0],
                "macOS dispatcher does not preflight its guest dependencies")
        require("--workdir /workspace/graphx-docker graphx -- env" in invocations[1]
                and "GRAPHX_BIN=/var/lib/graphx/m1/build/dev/graphx" in invocations[1]
                and "scripts/network-lab.sh macvlan status" in invocations[1],
                "macOS dispatcher does not route the canonical action through Lima")
    provision = (lima / "provision.sh").read_text(encoding="utf-8")
    for token in ("docker.io", "docker-buildx", "docker-compose-v2", "openvswitch-switch", "nftables", "qemu-system-ppc", "tcpdump", "tshark", "/var/lib/graphx"):
        require(token in provision, f"provisioning omits {token}")
    require("node:24-bookworm-slim@sha256:" in provision,
            "provisioning does not use the digest-pinned Node.js 24 runtime")
    package_block = re.search(r"packages=\(\n(.*?)\n\)", provision, re.DOTALL)
    require(package_block is not None, "provisioning package list is malformed")
    require(not ({"nodejs", "npm"} & set(package_block.group(1).split())),
            "provisioning must not rely on Ubuntu's unsupported Node.js packages")
    verifier_dockerfile = (root / "docker/linux-verifier.Dockerfile").read_text(
        encoding="utf-8"
    )
    node_image = re.search(r"node:24-bookworm-slim@sha256:[0-9a-f]{64}", provision)
    require(node_image is not None and node_image.group(0) in verifier_dockerfile,
            "Lima and the Linux verifier must use the same pinned Node image")
    require('import("node:sqlite")' in provision,
            "provisioning does not prove node:sqlite availability")
    require("docker buildx version" in provision,
            "provisioning does not record the required Buildx plugin")
    require('usermod --append --groups docker "${GRAPHX_LIMA_USER}"' in provision,
            "provisioning does not grant the Lima user Docker access")
    require('runuser --user "${GRAPHX_LIMA_USER}" -- docker info' in provision,
            "provisioning does not prove Lima-user Docker access")
    require("install -d -m 0700 /var/lib/graphx/runs" in provision,
            "the GraphX ownership-ledger parent must enforce mode 0700")
    require("chown root:root /var/lib/graphx/captures" in provision,
            "dumpcap storage root is not provisioned with root ownership")
    require(re.search(r"timeout [0-9]+", provision) is not None, "package/service operations are not bounded")

    start = (lima / "start.sh").read_text(encoding="utf-8")
    for token in ("Refreshing the Lima login session", 'limactl stop "${GRAPHX_M1_INSTANCE}"',
                  "docker info >/dev/null && docker buildx version >/dev/null",
                  'node --version | grep -Eq "^v24\\\\."', "node:sqlite"):
        require(token in start, f"Lima start lifecycle omits {token}")

    verify = (lima / "verify.sh").read_text(encoding="utf-8")
    require("Node.js 24 is required" in verify and 'import("node:sqlite")' in verify,
            "Lima verification does not enforce the supported Node.js runtime")
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
