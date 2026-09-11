#!/usr/bin/env python3
"""Portable contracts for GraphX normalized configuration JSON."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def clean_environment(**updates: str) -> dict[str, str]:
    environment = {
        key: value for key, value in os.environ.items()
        if not key.startswith("GRAPHX_")
    }
    environment.update(updates)
    return environment


def run(
    *args: object,
    check: bool = True,
    environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(value) for value in args],
        check=False,
        text=True,
        capture_output=True,
        timeout=30,
        env=environment or clean_environment(),
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


def normalize(graphx: Path, config: Path, *options: str,
              environment: dict[str, str] | None = None) -> tuple[str, dict]:
    output = run(
        graphx, "config", "normalize", config, *options,
        environment=environment,
    ).stdout
    return output, json.loads(output)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_normalized_config.py GRAPHX SOURCE_ROOT")
    graphx = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    fixture_root = root / "tests" / "fixtures" / "normalized"
    representative = root / "examples" / "network-observability" / "graphx.yaml"

    first, document = normalize(graphx, representative, "--format", "json")
    second, _ = normalize(graphx, representative)
    require(first == second, "explicit and default JSON normalization differ")
    require(first == normalize(graphx, representative)[0],
            "normalization is not byte deterministic")
    require(first == (fixture_root / "network-observability.json").read_text(encoding="utf-8"),
            "representative normalized contract changed")
    require(document["contract_version"] == 1, "normalized contract version drifted")
    require(document["source_version"] == 2 and document["infrastructure_mutable"] is True,
            "version-2 mutability boundary drifted")
    require(document["network"]["backend"] == "ovs", "version-2 backend is not OVS")
    require(document["network"]["captures"][0]["id"] == "ethernet-span",
            "capture intent is absent from normalized output")
    require(document["network"]["faults"][0]["duration_seconds"] == 30,
            "bounded fault intent is absent from normalized output")

    legacy_output, legacy = normalize(graphx, root / "graphx.yaml")
    require(legacy["source_version"] == 1 and legacy["infrastructure_mutable"] is False,
            "version-1 normalization incorrectly permits mutation")
    require(legacy["network"]["backend"] == "compatibility-only",
            "version-1 normalization advertises an active backend")
    require(legacy["network"]["networks"][0]["legacy_driver"] == "bridge",
            "version-1 compatibility semantics were discarded")
    require("docker" not in legacy["network"]["backend"],
            "legacy Docker networking was advertised as a backend")
    require(legacy_output == normalize(graphx, root / "graphx.yaml")[0],
            "version-1 normalization is not deterministic")

    override_environment = clean_environment(
        GRAPHX_OVERRIDES="observability.telemetry.port=9100",
        GRAPHX_CONTROL_TOKEN="normalization-secret-must-not-appear",
        GRAPHX_TELEMETRY_SHARED_SECRET="second-secret-must-not-appear",
    )
    overridden_output, overridden = normalize(
        graphx, root / "graphx.yaml",
        "--set", "observability.telemetry.port=9200",
        environment=override_environment,
    )
    require(overridden["observability"]["telemetry"]["port"] == 9200,
            "explicit override did not take precedence over GRAPHX_OVERRIDES")
    require("normalization-secret-must-not-appear" not in overridden_output and
            "second-secret-must-not-appear" not in overridden_output,
            "environment secret leaked into normalized JSON")

    for options, environment in (
        (("--set", "version=2"), clean_environment()),
        ((), clean_environment(GRAPHX_OVERRIDES="version=2")),
    ):
        refused = run(
            graphx, "config", "normalize", root / "examples" / "shared-memory" / "graphx.yaml",
            *options, check=False, environment=environment,
        )
        require(refused.returncode == 2 and
                "configuration version is immutable" in refused.stderr,
                "version-1 source boundary was overrideable during normalization")

    with tempfile.TemporaryDirectory() as temporary:
        state = Path(temporary) / "state"
        refused = run(
            graphx, "infra", "create",
            root / "examples" / "shared-memory" / "graphx.yaml",
            "--dry-run", "--state-dir", state,
            check=False,
            environment=clean_environment(GRAPHX_OVERRIDES="version=2"),
        )
        require(refused.returncode == 2 and
                "configuration version is immutable" in refused.stderr and
                not state.exists(),
                "version override reached infrastructure planning or created state")

    escaped_value = 'captures/quote"-backslash\\-unicode-λ'
    _, escaped = normalize(
        graphx, root / "graphx.yaml",
        "--set", f"observability.capture.directory={escaped_value}",
    )
    require(escaped["observability"]["capture"]["directory"] == escaped_value,
            "normalized JSON did not safely preserve escaped UTF-8 configuration text")

    checked_configs = [root / "graphx.yaml", *sorted((root / "examples").rglob("graphx.yaml"))]
    for config in checked_configs:
        output, candidate = normalize(graphx, config)
        require(output.endswith("\n") and not output.endswith("\n\n"),
                f"non-canonical final newline: {config}")
        require(candidate["contract_version"] == 1, f"contract version drifted: {config}")
        require(candidate["graph"]["id"], f"graph identity absent: {config}")
        require(candidate["observability"]["telemetry"]["port"] > 0,
                f"resolved observability defaults absent: {config}")

    invalid = run(
        graphx, "config", "normalize",
        root / "tests" / "fixtures" / "baseline" / "invalid-retired-profile.yaml",
        check=False,
    )
    require(invalid.returncode == 2 and "network.networks[0].profile" in invalid.stderr,
            "normalization did not preserve authoritative configuration diagnostics")
    for options, diagnostic in (
        (("--format", "yaml"), "format must be json"),
        (("--format", "json", "--format", "json"), "only once"),
        (("--set", "missing=value"), "override path does not exist"),
        (("--unknown",), "unknown option"),
    ):
        refused = run(graphx, "config", "normalize", representative, *options, check=False)
        require(refused.returncode != 0 and diagnostic in refused.stderr,
                f"invalid normalize options were accepted: {options}")

    print("GraphX normalized configuration contract checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
