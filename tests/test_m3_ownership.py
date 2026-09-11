#!/usr/bin/env python3
"""Portable M3 planning and ownership-boundary contracts."""

from __future__ import annotations

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
        raise SystemExit("usage: test_m3_ownership.py GRAPHX SOURCE_ROOT")
    graphx = Path(sys.argv[1]).resolve()
    root = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="graphx-m3-") as raw:
        temporary = Path(raw)
        config = temporary / "mixed-v2.yaml"
        migrated = run(
            graphx, "config", "migrate",
            root / "examples" / "compatibility" / "v1" / "mixed-network.yaml",
        ).stdout
        config.write_text(migrated, encoding="utf-8")
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
                "M3 bridge ownership remains free of legacy Docker networks and later mirrors")
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

        transactional = run(
            graphx, "infra", "create", config, "--transactional", "--dry-run",
            "--state-dir", state, check=False,
        )
        require(transactional.returncode != 0 and "always transactional" in transactional.stderr,
                "v2 rejects redundant legacy transaction mode")

    source = "\n".join(
        (root / path).read_text(encoding="utf-8")
        for path in (
            "src/ownership.cpp",
            "src/infra/ownership_lock.cpp",
            "src/infra/ownership_state.cpp",
        )
    )
    for contract in (
        "LOCK_EX | LOCK_NB", "LOCK_SH | LOCK_NB", "O_NOFOLLOW", "config_sha256", "owner_token",
        "expected_bridges", "external_ids:graphx_owner", "_uuid",
        "wait-until", "destroy", "refusing unowned", "refusing to delete replaced",
        "GRAPHX_M3_CRASH_AFTER", "path_entry_exists(state_path)",
        "save_state(state_path, state, false)",
    ):
        require(contract in source, f"missing ownership contract {contract}")
    require(source.count("external_ids:graphx_graph") >= 6,
            "graph identity must guard status, discovery, and deletion")
    require("std::filesystem::exists(state_path)" not in source,
            "state entry checks must not follow dangling symlinks")
    print("GraphX M3 portable ownership contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
