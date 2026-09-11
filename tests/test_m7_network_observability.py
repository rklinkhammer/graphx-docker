#!/usr/bin/env python3
"""Portable M7 declarative network capture/fault contracts."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile


def run(*args: object, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(value) for value in args], text=True, capture_output=True,
                            timeout=30, env={**os.environ, "GRAPHX_OVERRIDES": ""})
    if check and result.returncode:
        raise AssertionError(f"{' '.join(map(str, args))}\n{result.stdout}\n{result.stderr}")
    return result


def require(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_m7_network_observability.py GRAPHX SOURCE_ROOT")
    graphx, root = Path(sys.argv[1]), Path(sys.argv[2])
    config = root / "examples/network-observability/graphx.yaml"
    run(graphx, "validate", config)
    plan = run(graphx, "infra", "create", config, "--dry-run",
               "--state-dir", "/var/lib/graphx/runs").stdout
    for marker in ("format=ethernet-pcapng", "max-file-bytes=1048576", "max-files=4",
                   "retention-seconds=3600", "root netem", "duration-seconds=30",
                   "auto-clear=identity-checked"):
        require(marker in plan, f"missing M7 dry-run marker: {marker}")
    require("LINKTYPE_USER0" not in plan and "docker network" not in plan,
            "M7 Ethernet lifecycle must not merge USER0 or Docker data-plane ownership")

    text = config.read_text(encoding="utf-8")
    invalid = (
        (text.replace("/var/lib/graphx/captures/m7-demo", "captures/m7-demo"),
         "must be beneath /var/lib/graphx/captures"),
        (text.replace("attachment: observed-span", "attachment: source-data", 1),
         "must reference a mirror attachment"),
        (text.replace("attachment: source-data", "attachment: observed-span", 1),
         "must reference a realized data attachment"),
        (text.replace("max_files: 4", "max_files: 0"), "must be between 1 and 1024"),
        (text.replace("jitter_ms: 3", "jitter_ms: 21"), "must not exceed delay_ms"),
        (text.replace("duration_seconds: 30", "duration_seconds: 0"),
         "must be between 1 and 86400"),
    )
    for candidate_text, expected in invalid:
        with tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False) as candidate:
            candidate.write(candidate_text)
            path = Path(candidate.name)
        result = run(graphx, "validate", path, check=False)
        path.unlink()
        require(result.returncode != 0 and expected in result.stderr,
                f"invalid M7 configuration was not rejected with {expected}: {result.stderr}")

    ownership = "\n".join(
        (root / path).read_text(encoding="utf-8")
        for path in (
            "src/infra/lifecycle_coordinator.cpp",
            "src/infra/capture_resources.cpp",
            "src/infra/fault_resources.cpp",
            "src/infra/ownership_state.cpp",
        )
    )
    for marker in ("network_capture", "netem_fault", "GRAPHX_M7_CRASH_AFTER",
                   "GRAPHX_M7_FAIL_AFTER", "complete_pcapng_snapshot",
                   "prune_expired_capture_sessions",
                   "directory_uid", "directory_gid", "directory_mode",
                   "applied_monotonic_ns", "expires_monotonic_ns", "boot_id",
                   "capture_file_metadata_is_safe", "fault_deadline_elapsed",
                   "seal_capture_session_files", "inspect_capture_session_files",
                   "retained_capture_file_metadata_is_safe",
                   "refusing replaced capture", "refusing replaced fault"):
        require(marker in ownership, f"missing M7 ownership contract: {marker}")
    schema = (root / "config/schema/graphx.schema.json").read_text(encoding="utf-8")
    require('"networkCapture"' in schema and '"networkFault"' in schema,
            "M7 schema definitions are missing")
    readme = (root / "examples/network-observability/README.md").read_text(encoding="utf-8")
    require("LINKTYPE_USER0" in readme and "Ethernet PCAPNG" in readme,
            "capture trust domains are not documented")
    print("GraphX M7 portable network observation contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
