#!/usr/bin/env python3
"""Portable CLI and schema contracts for GraphX configuration version 2."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=check, text=True, capture_output=True, timeout=30)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_config_v2.py GRAPHX SOURCE_ROOT")
    graphx = str(Path(sys.argv[1]).resolve())
    root = Path(sys.argv[2]).resolve()
    sources = [
        root / "graphx.yaml",
        *(root / "examples" / name / "graphx.yaml" for name in (
            "macvlan", "ipvlan-l2", "ipvlan-l3", "mixed-network",
            "static-route-policy", "qemu-node",
        )),
    ]
    semantics = {
        "ethernet": "mac=endpoint learning=dynamic filtering=ovs arp=endpoint broadcast=flood multicast=flood routing=l2 isolation=none management=separate",
        "macvlan": "mac=endpoint learning=dynamic filtering=ovs arp=endpoint broadcast=flood multicast=flood routing=l2 isolation=host management=separate",
        "ipvlan-l2": "mac=shared-uplink learning=suppressed filtering=ovs arp=endpoint-shared-mac broadcast=flood multicast=flood routing=l2 isolation=host management=separate",
        "ipvlan-l3": "mac=shared-uplink learning=none filtering=route arp=suppressed broadcast=suppressed multicast=suppressed routing=l3 isolation=endpoint management=separate",
        "ipvlan-l3s": "mac=shared-uplink learning=none filtering=source-validated arp=suppressed broadcast=suppressed multicast=suppressed routing=l3-source-validated isolation=endpoint management=separate",
    }

    with tempfile.TemporaryDirectory(prefix="graphx-m2-") as temporary:
        temporary_root = Path(temporary)
        for index, source in enumerate(sources):
            first = run(graphx, "config", "migrate", str(source)).stdout
            second = run(graphx, "config", "migrate", str(source)).stdout
            require(first == second, f"migration is not deterministic for {source}")
            require("version: 2" in first and "driver:" not in first,
                    f"migration retained version-1 driver semantics for {source}")
            target = temporary_root / f"migrated-{index}.yaml"
            target.write_text(first, encoding="utf-8")
            validated = run(graphx, "validate", str(target)).stdout
            require("valid GraphX configuration version 2" in validated,
                    f"migrated configuration did not validate: {source}")
            inspected = run(graphx, "inspect", str(target)).stdout
            for profile, behavior in semantics.items():
                if f"profile: {profile}" in first:
                    require(f"profile={profile} {behavior}" in inspected,
                            f"inspect behavior drifted for {profile}")

        l3_source = (root / "examples" / "ipvlan-l3" / "graphx.yaml").read_text(
            encoding="utf-8"
        ).replace("mode: l3", "mode: l3s")
        l3_source_path = temporary_root / "ipvlan-l3s-v1.yaml"
        l3_source_path.write_text(l3_source, encoding="utf-8")
        l3_migrated = run(graphx, "config", "migrate", str(l3_source_path)).stdout
        l3_target = temporary_root / "ipvlan-l3s-v2.yaml"
        l3_target.write_text(l3_migrated, encoding="utf-8")
        require("profile: ipvlan-l3s" in l3_migrated,
                "ipvlan-l3s migration mapping is absent")
        require(f"profile=ipvlan-l3s {semantics['ipvlan-l3s']}" in
                run(graphx, "inspect", str(l3_target)).stdout,
                "inspect behavior drifted for ipvlan-l3s")

        hostile_environment = dict(os.environ)
        hostile_environment["GRAPHX_OVERRIDES"] = "version=2"
        environment_independent = subprocess.run(
            [graphx, "config", "migrate", str(root / "graphx.yaml")],
            check=True, text=True, capture_output=True, timeout=30,
            env=hostile_environment,
        ).stdout
        require(environment_independent == run(
            graphx, "config", "migrate", str(root / "graphx.yaml")
        ).stdout, "migration consumed ambient GRAPHX_OVERRIDES")

        output = temporary_root / "explicit.yaml"
        run(graphx, "config", "migrate", str(root / "graphx.yaml"), "--output", str(output))
        require(output.is_file(), "explicit migration output was not created")
        require(output.stat().st_mode & 0o777 == 0o600,
                "migration output permissions are not owner-only")
        refused = run(graphx, "config", "migrate", str(root / "graphx.yaml"),
                      "--output", str(output), check=False)
        require(refused.returncode != 0 and "refusing to overwrite" in refused.stderr,
                "migration overwrote an existing output")
        source_refused = run(graphx, "config", "migrate", str(root / "graphx.yaml"),
                             "--output", str(root / "graphx.yaml"), check=False)
        require(source_refused.returncode != 0 and "refusing to overwrite" in source_refused.stderr,
                "migration overwrote its source")
        dangling = temporary_root / "dangling.yaml"
        dangling.symlink_to(temporary_root / "unexpected-target.yaml")
        symlink_refused = run(graphx, "config", "migrate", str(root / "graphx.yaml"),
                              "--output", str(dangling), check=False)
        require(symlink_refused.returncode != 0 and "refusing to overwrite" in symlink_refused.stderr and
                not (temporary_root / "unexpected-target.yaml").exists(),
                "migration followed a dangling output symlink")
        duplicate_output = run(
            graphx, "config", "migrate", str(root / "graphx.yaml"),
            "--output", str(temporary_root / "one.yaml"),
            "--output", str(temporary_root / "two.yaml"), check=False,
        )
        require(duplicate_output.returncode != 0 and "only once" in duplicate_output.stderr,
                "migration accepted multiple output paths")
        unknown_option = run(graphx, "config", "migrate", str(root / "graphx.yaml"),
                             "--replace", check=False)
        require(unknown_option.returncode != 0 and "unknown option" in unknown_option.stderr,
                "migration accepted an unknown option")
        realization = run(graphx, "infra", "create", str(output), "--dry-run",
                          "--state-dir", str(temporary_root / "state"))
        require("ownership-state" in realization.stdout and
                "docker network create" not in realization.stdout + realization.stderr,
                "version 2 did not enter the M3 OVS lifecycle boundary")

    schema = json.loads((root / "config/schema/graphx.schema.json").read_text(encoding="utf-8"))
    require(schema["properties"]["version"]["enum"] == [1, 2], "schema version boundary")
    definitions = schema["$defs"]
    require("networkInfrastructureV1" in definitions and "networkInfrastructureV2" in definitions,
            "schema does not separate versioned network surfaces")
    attachment_kinds = definitions["attachmentDefinition"]["properties"]["kind"]["enum"]
    require(attachment_kinds == ["container_veth", "namespace_veth", "qemu_tap", "external", "mirror"],
            "schema attachment kinds drifted")
    print("GraphX configuration v2 migration and CLI checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
