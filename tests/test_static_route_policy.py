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
    config = example / "graphx.yml"
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

    from config_plan_support import check
    value = check(graphx, root, "static-route-policy")
    routes = [r for router in value["network"]["routers"] for r in router["routes"]]
    assert any(route.get("install") == "manual" for route in routes)
    assert all("device" in route for route in routes)

    demo = (example / "scripts/demo.sh").read_text(encoding="utf-8")
    for marker in ("scripts/lib/demo-runtime.sh", "graphx_demo_run", "apply-route|clear-route)"):
        assert marker in demo, f"compiled route launcher omits {marker}"
    assert 'infra create' not in demo and 'external-ovs-boundary.sh' not in demo
    for action in ('apply-route', 'clear-route'):
        result = subprocess.run(['bash', example / 'scripts/demo.sh', action], capture_output=True, text=True)
        assert result.returncode == 2 and 'E_PHASE_UNAVAILABLE' in result.stderr
    assert "--transactional" not in demo
    assert "pkill" not in demo and "killall" not in demo and "rm -rf" not in demo
    assert not (example / "compose.yaml").exists(), "retired route Compose data plane remains"
    subprocess.run(["bash", "-n", example / "scripts/demo.sh",
                    example / "scripts/inspect.sh"], check=True)
    print("route-policy portable route-policy contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
