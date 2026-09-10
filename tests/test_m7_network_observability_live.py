#!/usr/bin/env python3
"""Privileged Linux M7 capture/fault ownership lifecycle regression."""

from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time


def run(*args: object, check: bool = True,
        extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    result = subprocess.run([str(value) for value in args], text=True, capture_output=True,
                            timeout=45,
                            env={**os.environ, "GRAPHX_OVERRIDES": "", **(extra_env or {})})
    if check and result.returncode:
        raise AssertionError(f"{' '.join(map(str, args))}\n{result.stdout}\n{result.stderr}")
    return result


def main() -> int:
    if len(sys.argv) != 2 or sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("M7 live test requires: test_m7_network_observability_live.py GRAPHX as Linux root")
    for command in ("ovs-vsctl", "ip", "tc", "dumpcap", "capinfos"):
        if shutil.which(command) is None:
            raise SystemExit(f"M7 live test requires {command}")
    graphx = Path(sys.argv[1]).resolve()
    bridge, namespace = "br-gxm7live", "gx-m7-live"
    capture_root = Path("/var/lib/graphx/captures/m7-live-test")

    def cleanup(state_file: Path | None = None) -> None:
        if state_file and state_file.exists():
            ledger = state_file.read_text(encoding="utf-8", errors="replace")
            for field in ("pid", "timer_pid"):
                for value in re.findall(rf"^\s*{field}: ([0-9]+)$", ledger, re.MULTILINE):
                    run("kill", value, check=False)
        run("tc", "qdisc", "del", "dev", "gxm7a0", "root", check=False)
        run("ovs-vsctl", "--if-exists", "del-br", bridge, check=False)
        run("ip", "netns", "delete", namespace, check=False)
        run("ip", "link", "delete", "gxm7cap0", check=False)
        if state_file:
            state_file.unlink(missing_ok=True)
        shutil.rmtree(capture_root, ignore_errors=True)

    cleanup()
    try:
        with tempfile.TemporaryDirectory(prefix="graphx-m7-live-", dir="/var/tmp") as raw:
            root = Path(raw)
            state, config = root / "state", root / "graphx.yaml"
            config.write_text(f"""\
version: 2
graph:
  id: m7-live
  nodes: [{{id: probe, kind: diagnostic-endpoint, runtime: process, execution: host,
           lifecycle: external, control: none, ports: []}}]
  edges: []
transport: {{}}
network:
  networks: [{{id: data, profile: ethernet, subnets: [10.77.0.0/24], gateway: 10.77.0.1, external: false}}]
  switches:
    - id: {bridge}
      kind: openvswitch
      ports:
        - {{id: a, interface: gxm7a0, peer: gxm7a1}}
        - {{id: b, interface: gxm7b0, peer: gxm7b1}}
        - {{id: capture, interface: gxm7cap0, peer: gxm7cap1}}
      mirror: {{id: m7-span, output_port: capture, select_all: true}}
  routers:
    - id: m7-peer
      kind: linux_namespace
      namespace: {namespace}
      forwarding: false
      interfaces:
        - {{id: a, network: data, address: 10.77.0.2/24, device: gxm7a1, peer: gxm7a0, switch: {bridge}}}
        - {{id: b, network: data, address: 10.77.0.3/24, device: gxm7b1, peer: gxm7b0, switch: {bridge}}}
  attachments:
    - {{id: a-data, kind: namespace_veth, owner: m7-peer, network: data,
       address: 10.77.0.2/24, interface: gxm7a1, peer: gxm7a0, switch: {bridge}}}
    - {{id: b-data, kind: namespace_veth, owner: m7-peer, network: data,
       address: 10.77.0.3/24, interface: gxm7b1, peer: gxm7b0, switch: {bridge}}}
    - {{id: m7-span, kind: mirror, owner: {bridge}, interface: gxm7cap0, switch: {bridge}}}
  captures:
    - {{id: live-span, attachment: m7-span, directory: {capture_root}, snaplen: 2048,
       max_file_bytes: 65536, max_files: 2, rotation_seconds: 1, retention_seconds: 60}}
  faults:
    - {{id: live-delay, attachment: a-data, delay_ms: 5, jitter_ms: 1,
       duration_seconds: 2}}
""", encoding="utf-8")
            state_file = state / "m7-live.yaml"

            failed = run(graphx, "infra", "create", config, "--state-dir", state,
                         check=False, extra_env={"GRAPHX_M7_FAIL_AFTER": "6"})
            if failed.returncode == 0 or state_file.exists() or run(
                    "ovs-vsctl", "br-exists", bridge, check=False).returncode == 0:
                raise AssertionError("M7 injected failure did not roll back")
            cleanup(state_file)

            crashed = run(graphx, "infra", "create", config, "--state-dir", state,
                          check=False, extra_env={"GRAPHX_M7_CRASH_AFTER": "6"})
            if crashed.returncode != 99 or not state_file.exists() or not list(
                    capture_root.glob("live-span-*/*.pcapng")):
                raise AssertionError("M7 crash did not leave recoverable capture identity: "
                                     f"rc={crashed.returncode}\n{crashed.stdout}\n{crashed.stderr}")
            run(graphx, "infra", "recover", config, "--state-dir", state)
            if state_file.exists() or run("ovs-vsctl", "br-exists", bridge,
                                          check=False).returncode == 0:
                raise AssertionError("M7 recovery left infrastructure or ledger residue")
            cleanup(state_file)

            # Permission drift and premature fault disappearance must fail closed.
            run(graphx, "infra", "create", config, "--state-dir", state)
            ledger = state_file.read_text(encoding="utf-8")
            session_match = re.search(r"^\s*session_directory: (.+)$", ledger, re.MULTILINE)
            timer_match = re.search(r"^\s*timer_pid: ([0-9]+)$", ledger, re.MULTILINE)
            if not session_match or not timer_match:
                raise AssertionError("M7 ledger lacks adversarial-test identities")
            session = Path(session_match.group(1).strip('"'))
            session.chmod(0o777)
            mode_status = run(graphx, "infra", "status", config, "--state-dir", state,
                              check=False)
            mode_export = run(graphx, "infra", "capture", "export", config,
                              "--capture", "live-span", "--output", root / "mode-drift.pcapng",
                              "--state-dir", state, check=False)
            mode_destroy = run(graphx, "infra", "destroy", config, "--state-dir", state,
                               check=False)
            if mode_status.returncode == 0 or mode_export.returncode == 0 or \
                    mode_destroy.returncode == 0:
                raise AssertionError("capture directory permission drift did not fail closed")
            session.chmod(0o700)
            if run(graphx, "infra", "status", config, "--state-dir", state).returncode != 0:
                raise AssertionError("restored capture directory identity remained unhealthy")

            timer_pid = timer_match.group(1)
            run("kill", timer_pid)
            for _ in range(50):
                if run("kill", "-0", timer_pid, check=False).returncode != 0:
                    break
                time.sleep(0.02)
            run("tc", "qdisc", "del", "dev", "gxm7a0", "root")
            early_status = run(graphx, "infra", "status", config, "--state-dir", state,
                               check=False)
            early_destroy = run(graphx, "infra", "destroy", config, "--state-dir", state,
                                check=False)
            if early_status.returncode == 0 or "state=missing-or-replaced" not in \
                    early_status.stdout or early_destroy.returncode == 0:
                raise AssertionError("premature timer/qdisc disappearance did not fail closed")
            cleanup(state_file)

            drifted_retained_file: Path | None = None
            drifted_retained_mode: int | None = None
            for cycle in range(3):
                run(graphx, "infra", "create", config, "--state-dir", state)
                sessions = list(capture_root.glob("live-span-*"))
                if cycle == 1:
                    if len(sessions) != 2 or drifted_retained_file is None or \
                            drifted_retained_mode is None or not drifted_retained_file.exists():
                        raise AssertionError("unsafe expired capture session was pruned")
                    drifted_retained_file.chmod(drifted_retained_mode)
                elif cycle == 2 and (len(sessions) != 1 or
                                     (drifted_retained_file is not None and
                                      drifted_retained_file.exists())):
                    raise AssertionError("restored expired capture session was not pruned")
                status = run(graphx, "infra", "status", config, "--state-dir", state).stdout
                ledger = state_file.read_text(encoding="utf-8")
                active_session_match = re.search(
                    r"^\s*session_directory: (.+)$", ledger, re.MULTILINE)
                if active_session_match is None:
                    raise AssertionError("M7 ledger lacks active capture directory")
                active_session = Path(active_session_match.group(1).strip('"'))
                if not all(marker in status for marker in
                           ("network_capture live-span", "state=capturing",
                            "netem_fault live-delay", "state=active")):
                    raise AssertionError(f"M7 status lacks active capture/fault evidence: {status}")
                if not all(marker in ledger for marker in
                           ("phase: M7", "kind: network_capture", "kind: netem_fault",
                            "process_start_time:", "directory_uid: 0", "directory_gid: 0",
                            "directory_mode: 448", "qdisc_identity:", "boot_id:",
                            "applied_monotonic_ns:", "expires_monotonic_ns:")):
                    raise AssertionError("M7 ledger lacks process/qdisc identities")
                if "netem" not in run("tc", "qdisc", "show", "dev", "gxm7a0").stdout:
                    raise AssertionError("declarative netem was not applied")
                frame_sender = ("import socket; s=socket.socket(socket.AF_PACKET, "
                                "socket.SOCK_RAW); s.bind(('gxm7a1', 0)); "
                                "s.send(bytes.fromhex('ffffffffffff02000000007788b5') + "
                                "b'graphx-m7' * 8)")
                for _ in range(6):
                    run("ip", "netns", "exec", namespace, "python3", "-c", frame_sender)
                    time.sleep(0.4)
                files = list(active_session.glob("*.pcapng"))
                if not 1 <= len(files) <= 2:
                    raise AssertionError(f"capture ring is not bounded: {files}")
                if cycle == 0:
                    source_mode = files[0].stat().st_mode & 0o777
                    files[0].chmod(0o666)
                    unsafe_status = run(graphx, "infra", "status", config,
                                        "--state-dir", state, check=False)
                    unsafe_export = run(graphx, "infra", "capture", "export", config,
                                        "--capture", "live-span", "--output",
                                        root / "unsafe-source.pcapng", "--state-dir", state,
                                        check=False)
                    if unsafe_status.returncode == 0 or unsafe_export.returncode == 0:
                        raise AssertionError("writable capture source did not fail closed")
                    files[0].chmod(source_mode)
                exported = root / f"export-{cycle}.pcapng"
                run(graphx, "infra", "capture", "export", config, "--capture", "live-span",
                    "--output", exported, "--state-dir", state)
                capinfos = run("capinfos", "-T", "-t", "-c", exported).stdout
                if "pcapng" not in capinfos.lower():
                    raise AssertionError(f"export is not PCAPNG: {capinfos}")
                duplicate = run(graphx, "infra", "capture", "export", config,
                                "--capture", "live-span", "--output", exported,
                                "--state-dir", state, check=False)
                if duplicate.returncode == 0:
                    raise AssertionError("capture export overwrote an existing destination")
                time.sleep(2.1)
                expired = run(graphx, "infra", "status", config, "--state-dir", state).stdout
                if "netem_fault live-delay" not in expired or "state=expired" not in expired:
                    raise AssertionError(f"timed fault did not expire observably: {expired}")
                if "netem" in run("tc", "qdisc", "show", "dev", "gxm7a0").stdout:
                    raise AssertionError("timed fault left a netem qdisc")
                run(graphx, "infra", "destroy", config, "--state-dir", state)
                if state_file.exists() or run("ovs-vsctl", "br-exists", bridge,
                                              check=False).returncode == 0 or run(
                        "ip", "netns", "list").stdout.find(namespace) >= 0:
                    raise AssertionError("M7 ordinary destroy left runtime residue")
                sessions = list(capture_root.glob("live-span-*"))
                expected_sessions = 2 if cycle == 1 else 1
                if len(sessions) != expected_sessions or any(
                        (session.stat().st_mode & 0o777) != 0o550 for session in sessions):
                    raise AssertionError("retained capture session was not finalized read-only")
                retained_files = [item for session in sessions for item in session.iterdir()]
                if not retained_files or not any(item.suffix == ".pcapng"
                                                 for item in retained_files):
                    raise AssertionError("retained capture session lacks PCAPNG evidence")
                for item in retained_files:
                    metadata = item.stat(follow_symlinks=False)
                    if not item.is_file() or metadata.st_uid != 0 or metadata.st_gid != 0 or \
                            metadata.st_mode & 0o222 or \
                            (item.name != "dumpcap.stderr" and item.suffix != ".pcapng"):
                        raise AssertionError(f"retained capture file is not sealed: {item}")
                    writer = ("import os; os.setgroups([]); os.setgid(65534); os.setuid(65534); "
                              f"open({str(item)!r}, 'ab').close()")
                    if run("python3", "-c", writer, check=False).returncode == 0:
                        raise AssertionError(f"unprivileged write reached retained evidence: {item}")
                if cycle == 0:
                    expired_time = time.time() - 61
                    os.utime(sessions[0], (expired_time, expired_time))
                    drifted_retained_file = next(
                        item for item in retained_files if item.suffix == ".pcapng")
                    drifted_retained_mode = drifted_retained_file.stat().st_mode & 0o777
                    drifted_retained_file.chmod(drifted_retained_mode | 0o200)
                elif cycle == 1:
                    expired_time = time.time() - 61
                    for session in sessions:
                        os.utime(session, (expired_time, expired_time))
                else:
                    cleanup(state_file)
    finally:
        cleanup()
    print("GraphX M7 live capture/fault lifecycle passed three times")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
