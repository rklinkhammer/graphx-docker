#!/usr/bin/env python3
"""Behavioral checks for deterministic, non-mutating configuration projections."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


FILENAMES = (
    "graph-topology.yaml",
    "transport-topology.yaml",
    "network-topology.yaml",
    "deployment-topology.yaml",
)


def run(*args: object, expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(arg) for arg in args], text=True, capture_output=True, check=False,
        env={**os.environ, "GRAPHX_OVERRIDES": ""}
    )
    if result.returncode != expected:
        raise AssertionError(
            f"expected exit {expected}, got {result.returncode}: {' '.join(map(str, args))}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def digest(directory: Path) -> str:
    value = hashlib.sha256()
    for filename in FILENAMES:
        value.update((directory / filename).read_bytes())
    return value.hexdigest()


def assert_contains(path: Path, *values: str) -> None:
    content = path.read_text(encoding="utf-8")
    for value in values:
        if value not in content:
            raise AssertionError(f"{path} does not contain {value!r}")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_projections.py GRAPHX SOURCE_ROOT")
    cli = Path(sys.argv[1]).resolve()
    source_root = Path(sys.argv[2]).resolve()

    with tempfile.TemporaryDirectory(prefix="graphx-projections-") as temporary_name:
        temporary = Path(temporary_name)
        source = temporary / "graphx.yaml"
        output = temporary / "config"
        shutil.copyfile(source_root / "graphx.yaml", source)

        run(cli, "project", source, "--output-dir", output)
        first_digest = digest(output)
        run(cli, "project", source, "--output-dir", output)
        if digest(output) != first_digest:
            raise AssertionError("repeated projection generation was not deterministic")
        run(cli, "project", source, "--check", "--output-dir", output)

        graph = output / "graph-topology.yaml"
        transport = output / "transport-topology.yaml"
        network = output / "network-topology.yaml"
        deployment = output / "deployment-topology.yaml"
        assert_contains(graph, "graph_id: sample-pipeline", "runtime: process", "data_plane: graphx")
        assert_contains(transport, "type: tcp", "verify_peer: true", "max_attempts: 60")
        assert_contains(network, "owner: generator", "edge: samples", "external: false")
        assert_contains(deployment, "node: generator", "service: telemetry", "port: 8080")

        transport.write_text(transport.read_text(encoding="utf-8") + "# stale\n", encoding="utf-8")
        stale = transport.read_bytes()
        result = run(cli, "project", source, "--check", "--output-dir", output, expected=1)
        if b"# stale" not in transport.read_bytes() or transport.read_bytes() != stale:
            raise AssertionError("check mode rewrote a stale projection")
        if "Regenerate with: graphx project" not in result.stderr:
            raise AssertionError("stale diagnostic omits the regeneration command")

        transport.unlink()
        run(cli, "project", source, "--check", "--output-dir", output, expected=1)
        if transport.exists():
            raise AssertionError("check mode created a missing projection")
        run(cli, "project", source, "--output-dir", output)

        before_invalid = digest(output)
        invalid_source = temporary / "invalid.yaml"
        invalid_source.write_text("version: [\n", encoding="utf-8")
        run(cli, "project", invalid_source, "--output-dir", output, expected=2)
        if digest(output) != before_invalid:
            raise AssertionError("invalid source configuration mutated projections")

        fifo = output / "network-topology.yaml"
        fifo.unlink()
        os.mkfifo(fifo)
        run(cli, "project", source, "--output-dir", output, expected=1)
        fifo.unlink()
        run(cli, "project", source, "--output-dir", output)

        referent = temporary / "referent.yaml"
        referent.write_text("sentinel\n", encoding="utf-8")
        graph.unlink()
        graph.symlink_to(referent)
        run(cli, "project", source, "--output-dir", output, expected=1)
        if referent.read_text(encoding="utf-8") != "sentinel\n":
            raise AssertionError("symlink referent was modified")

        cases = {
            "../tests/fixtures/projection-transports": (
                "type: in_process", "type: unix", "type: shared_memory", "type: tcp",
                "type: udp", "require_client_certificate: true", "channel: quoted-channel"
            ),
            "shared-memory": ("type: shared_memory", "max_message_bytes:"),
            "udp-unicast": ("type: udp", "mode: unicast", "max_datagram_bytes:"),
            "udp-broadcast": ("mode: broadcast", "loopback:"),
            "udp-multicast": ("mode: multicast", "ttl:"),
            "qemu-node/external": ("runtime: qemu", "architecture: x86_64", "data_plane: external"),
            "mixed-network": ("kind: openvswitch", "mirror:", "policies:", "hops:"),
            "ipvlan-l3": ("subnets:", "10.42.1.0/24", "mode: l3"),
        }
        for example, expected_values in cases.items():
            case_output = temporary / example.replace("/", "-")
            if example.startswith("../tests/"):
                case_source = source_root / (example.removeprefix("../") + ".yaml")
            else:
                case_source = source_root / "examples" / example / "graphx.yaml"
            run(cli, "project", case_source, "--output-dir", case_output)
            combined = "\n".join(
                (case_output / filename).read_text(encoding="utf-8") for filename in FILENAMES
            )
            for value in expected_values:
                if value not in combined:
                    raise AssertionError(f"{example} projections omit {value!r}")
            if example == "qemu-node/external":
                qemu_deployment = (case_output / "deployment-topology.yaml").read_text(encoding="utf-8")
                if "node: qemu-node" in qemu_deployment:
                    raise AssertionError("external QEMU node gained a fabricated managed service")

        network_source = temporary / "network-fixture.yaml"
        network_text = (source_root / "examples" / "mixed-network" / "graphx.yaml").read_text(
            encoding="utf-8"
        )
        network_text = network_text.replace(
            "{ id: docker-parent, interface: mv-ovs, peer: mv-parent }",
            "{ id: docker-parent, interface: mv-ovs, peer: mv-parent, vlan: { access_tag: 42, trunks: [43, 44] } }",
        ).replace(
            "      policies:\n",
            "      routes:\n"
            "        - { destination: 10.30.0.0/24, via: 10.20.0.254, device: r-ipv }\n"
            "      policies:\n",
        )
        network_source.write_text(network_text, encoding="utf-8")
        network_output = temporary / "network-rich"
        run(cli, "project", network_source, "--output-dir", network_output)
        assert_contains(
            network_output / "network-topology.yaml",
            "access_tag: 42", "- 43", "routes:", "destination: 10.30.0.0/24",
        )

    print("projection behavior checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
