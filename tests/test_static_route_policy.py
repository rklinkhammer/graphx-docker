#!/usr/bin/env python3
"""Portable contract tests for the route-policy native route-policy laboratory."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def load_module(path: Path):
    spec = importlib.util.spec_from_file_location("route_diagnostic", path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"cannot import {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(*arguments: object, expect: int = 0) -> str:
    result = subprocess.run([str(value) for value in arguments], check=False,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env={**os.environ, "GRAPHX_OVERRIDES": ""})
    if result.returncode != expect:
        raise AssertionError(f"command returned {result.returncode}, expected {expect}:\n{result.stdout}")
    return result.stdout


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_static_route_policy.py SOURCE_ROOT GRAPHX_CLI")
    root, graphx = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    example = root / "examples/static-route-policy"
    config = example / "graphx.yaml"
    diagnostic = load_module(example / "tools/diagnostic.py")

    for size in (1, diagnostic.MAX_TOKEN_BYTES):
        token = "x" * size
        assert diagnostic.parse_packet(diagnostic.payload(token)) == token
    invalid_packets = (b"", b"GXR1\x00\x01", b"BAD!\x00\x01x",
                       diagnostic.payload("ok") + b"x")
    for value in invalid_packets:
        try:
            diagnostic.parse_packet(value)
            raise AssertionError("malformed diagnostic datagram was accepted")
        except (UnicodeDecodeError, ValueError):
            pass

    assert "valid graphx configuration" in run(graphx, "validate", config).lower()
    inspected = run(graphx, "inspect", config)
    assert "static-route-policy-lab" in inspected
    created = run(graphx, "infra", "create", config, "--dry-run")
    assert all(name in created for name in ("br-route-left", "br-route-middle", "br-route-right"))
    assert all(name in created for name in ("mirror-route-left", "mirror-route-middle",
                                             "mirror-route-right"))
    assert "ovs-vsctl -- add-br br-route-left" in created
    assert "--may-exist add-br br-route-left" not in created
    source = config.read_text(encoding="utf-8")
    assert source.index("allow-left-middle") < source.index("deny-middle-left") < source.index("allow-left-right")
    assert "ip route replace 10.64.30.10/32" not in created, "manual route leaked into create"
    applied = run(graphx, "infra", "route", "apply", config, "--router", "route-router",
                  "--destination", "10.64.30.10/32", "--dry-run")
    cleared = run(graphx, "infra", "route", "clear", config, "--router", "route-router",
                  "--destination", "10.64.30.10/32", "--dry-run")
    assert "ip route replace 10.64.30.10/32 via 10.64.3.10 dev rt-right" in applied
    assert "ip route delete 10.64.30.10/32" in cleared
    run(graphx, "infra", "status", config, "--dry-run")
    run(graphx, "infra", "destroy", config, "--dry-run")

    with tempfile.TemporaryDirectory(prefix="graphx-route-policy-") as temporary:
        invalid = Path(temporary) / "invalid.yaml"
        invalid.write_text(source.replace("install: manual", "install: automatic"), encoding="utf-8")
        assert "must be 'create' or 'manual'" in run(graphx, "validate", invalid, expect=2)
        invalid.write_text(source.replace("device: rt-right, install: manual",
                                          "device: unknown, install: manual"), encoding="utf-8")
        assert "unknown router interface device" in run(graphx, "validate", invalid, expect=2)
        invalid.write_text(source.replace("      policies:\n",
            "        - { destination: 10.64.30.10/32, device: rt-right, install: manual }\n      policies:\n"),
            encoding="utf-8")
        assert "must be unique within the router" in run(graphx, "validate", invalid, expect=2)
        invalid.write_text(source.replace("id: allow-left-middle", "id: deny-middle-left"),
                           encoding="utf-8")
        assert "policies[0].id: must be unique within the router" in run(
            graphx, "validate", invalid, expect=2)

    demo = (example / "scripts/demo.sh").read_text(encoding="utf-8")
    for marker in ("GRAPHX_EXTERNAL_OWNER", "external-ovs-boundary.sh",
                   "graphx_external_namespace_create", "graphx_external_namespace_delete",
                   "trap rollback_up ERR", 'infra create "$config"', "apply-route)",
                   "clear-route)"):
        assert marker in demo, f"canonical route launcher omits {marker}"
    assert "--transactional" not in demo
    assert "pkill" not in demo and "killall" not in demo and "rm -rf" not in demo
    assert not (example / "compose.yaml").exists(), "retired route Compose data plane remains"
    subprocess.run(["bash", "-n", example / "scripts/demo.sh",
                    example / "scripts/inspect.sh"], check=True)
    print("route-policy portable route-policy contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
