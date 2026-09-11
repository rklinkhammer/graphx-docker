#!/usr/bin/env python3
"""S0 behavior snapshots protecting the simplification refactor."""

from __future__ import annotations

import difflib
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def clean_environment() -> dict[str, str]:
    return {key: value for key, value in os.environ.items()
            if not key.startswith("GRAPHX_")}


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(value) for value in args],
        check=False,
        text=True,
        capture_output=True,
        timeout=30,
        env=clean_environment(),
    )
    if check and result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(str(value) for value in args)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def fixture(root: Path, name: str) -> str:
    return (root / "tests" / "fixtures" / "baseline" / name).read_text(encoding="utf-8")


def assert_snapshot(root: Path, name: str, actual: str) -> None:
    expected = fixture(root, name)
    if actual != expected:
        difference = "".join(difflib.unified_diff(
            expected.splitlines(keepends=True),
            actual.splitlines(keepends=True),
            fromfile=f"expected/{name}",
            tofile=f"actual/{name}",
        ))
        raise AssertionError(
            f"S0 snapshot changed: {name}\n"
            "If the behavior change is intentional, review it and update the fixture explicitly.\n"
            f"{difference}"
        )


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_simplification_baseline.py GRAPHX SOURCE_ROOT")
    graphx = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    macvlan = root / "examples" / "macvlan" / "graphx.yaml"
    legacy_macvlan = root / "examples" / "compatibility" / "v1" / "macvlan.yaml"
    observability = root / "examples" / "network-observability" / "graphx.yaml"
    routes = root / "examples" / "static-route-policy" / "graphx.yaml"

    validated = run(graphx, "validate", macvlan).stdout.replace(str(root), "<SOURCE_ROOT>")
    assert_snapshot(root, "validate-macvlan.txt", validated)

    inspect = run(graphx, "inspect", macvlan).stdout
    assert_snapshot(root, "inspect-macvlan.txt", inspect)

    projected = run(
        graphx, "project", root / "graphx.yaml", "--check",
        "--output-dir", root / "config",
    ).stdout.replace(str(root), "<SOURCE_ROOT>")
    assert_snapshot(root, "project-root-check.txt", projected)

    migration = run(graphx, "config", "migrate", legacy_macvlan).stdout
    assert_snapshot(root, "migrate-macvlan-v1.yaml", migration)
    require(migration == run(graphx, "config", "migrate", legacy_macvlan).stdout,
            "migration output is not deterministic")

    with tempfile.TemporaryDirectory(prefix="graphx-s0-") as raw:
        temporary = Path(raw)
        state_root = temporary / "state"

        for action, snapshot in (
            ("create", "infra-create-network-observability.txt"),
            ("status", "infra-status-network-observability.txt"),
            ("destroy", "infra-destroy-network-observability.txt"),
            ("recover", "infra-recover-network-observability.txt"),
        ):
            result = run(
                graphx, "infra", action, observability, "--dry-run",
                "--state-dir", state_root,
            )
            normalized = result.stdout.replace(str(state_root), "<STATE_ROOT>")
            assert_snapshot(root, snapshot, normalized)
            require(not state_root.exists(), f"{action} dry-run created ownership state")

        refused_state = temporary / "version-1-state"
        refused = run(
            graphx, "infra", "create", legacy_macvlan, "--dry-run",
            "--state-dir", refused_state, check=False,
        )
        require(refused.returncode != 0, "version-1 infrastructure execution succeeded")
        assert_snapshot(root, "version1-infra-refusal.txt", refused.stderr)
        require(not refused_state.exists(),
                "version-1 infrastructure refusal created state before failing")

    route_apply = run(
        graphx, "infra", "route", "apply", routes,
        "--router", "route-router", "--destination", "10.64.30.10/32", "--dry-run",
    ).stdout
    route_clear = run(
        graphx, "infra", "route", "clear", routes,
        "--router", "route-router", "--destination", "10.64.30.10/32", "--dry-run",
    ).stdout
    assert_snapshot(root, "infra-route-apply.txt", route_apply)
    assert_snapshot(root, "infra-route-clear.txt", route_clear)

    invalid = run(
        graphx, "validate",
        root / "tests" / "fixtures" / "baseline" / "invalid-retired-profile.yaml",
        check=False,
    )
    require(invalid.returncode != 0, "retired network profile unexpectedly validated")
    normalized_error = invalid.stderr.replace(str(root), "<SOURCE_ROOT>")
    assert_snapshot(root, "invalid-retired-profile.txt", normalized_error)

    print("GraphX S0 simplification behavior snapshots passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
