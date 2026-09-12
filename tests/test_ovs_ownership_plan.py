#!/usr/bin/env python3
"""Portable OVS planning and ownership-boundary contracts."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(value) for value in args], text=True, capture_output=True,
        timeout=20, env={**os.environ, "GRAPHX_OVERRIDES": ""}, check=False,
    )
    if check and result.returncode != 0:
        raise AssertionError(f"command failed: {result.stderr}")
    return result


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_ovs_ownership_plan.py GRAPHX SOURCE_ROOT")
    graphx = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="graphx-ovs-") as raw:
        temporary = Path(raw)
        config = root / "examples" / "mixed-network" / "graphx.yaml"
        state = temporary / "state"

        create = run(
            graphx, "infra", "create", config, "--dry-run", "--state-dir", state,
        ).stdout
        require("# ownership-state" in create and
                str(state / "mixed-network.yaml") in create,
                "dry-run state path")
        require(create.count("ovs-vsctl -- add-br") == 2, "one OVS mutation per bridge")
        require("datapath_type=system" in create and
                "external_ids:graphx_owner=<generated-owner-token>" in create and
                "external_ids:graphx_config_hash=" in create and
                "external_ids:graphx_graph=mixed-network" in create,
                "dry-run ownership markers")
        require("docker network" not in create and "Mirror" not in create,
                "OVS ownership plan unexpectedly uses Docker networking or mirrors")
        digest = re.search(r"graphx_config_hash=([0-9a-f]{64})", create)
        require(digest is not None, "configuration SHA-256")
        require(not state.exists(), "dry-run does not create state")

        for action, marker in (
            ("status", "# inspect ownership-state"),
            ("destroy", "# identity-check-delete ownership-state"),
            ("recover", "# identity-check-delete ownership-state"),
        ):
            result = run(
                graphx, "infra", action, config, "--dry-run", "--state-dir", state,
            )
            require(marker in result.stdout and not state.exists(),
                    f"{action} dry-run boundary")

        plans = []
        resolved = []
        for instance in ("lab-a", "lab-b"):
            selected = temporary / f"{instance}.yaml"
            selected.write_text(config.read_text().replace(
                "deployment:", f"deployment:\n  instance_id: {instance}"))
            plans.append(run(graphx, "infra", "create", selected, "--dry-run",
                             "--state-dir", state).stdout)
            resolved.append(json.loads(run(graphx, "config", "normalize", selected,
                                           "--resources").stdout))
        names = [set(re.findall(r"add-br (gx[0-9a-f]+)", plan)) for plan in plans]
        require(len(names[0]) == 2 and names[0].isdisjoint(names[1]),
                "instances must have disjoint bridge names")
        require(all(len(name) <= 15 for group in names for name in group),
                "generated interface length")
        require(resolved[0]["deployment"]["project"] != resolved[1]["deployment"]["project"],
                "resolved Compose project isolation")
        require("br-gx-mac" not in names[0], "logical bridge name leaked")
        require(not state.exists(), "instance plans mutated state")

    print("GraphX portable OVS ownership contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
