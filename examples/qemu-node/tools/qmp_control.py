#!/usr/bin/env python3
"""Bounded QMP control and runtime-evidence helper for the QEMU demonstrations."""

from __future__ import annotations

import argparse
import json
import os
import socket
import tempfile
import time
from pathlib import Path

MAX_QMP_MESSAGE = 256 * 1024
GUEST_PROBE_PAYLOAD = b"graphx guest readiness"
QMP_PAUSED_STATES = {"paused", "suspended", "inmigrate", "postmigrate", "prelaunch"}
QMP_STOPPED_STATES = {"shutdown"}


class QmpError(RuntimeError):
    pass


def daemonize(pid_file: Path, log_file: Path) -> bool:
    if not hasattr(os, "fork"):
        raise QmpError("guest readiness daemon mode requires a POSIX host")
    child = os.fork()
    if child:
        os.waitpid(child, 0)
        return False
    os.setsid()
    grandchild = os.fork()
    if grandchild:
        os._exit(0)
    log_file.parent.mkdir(parents=True, exist_ok=True)
    null_fd = os.open(os.devnull, os.O_RDONLY)
    log_fd = os.open(log_file, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    os.dup2(null_fd, 0)
    os.dup2(log_fd, 1)
    os.dup2(log_fd, 2)
    os.close(null_fd)
    os.close(log_fd)
    pid_file.write_text(f"{os.getpid()}\n", encoding="ascii")
    os.chmod(pid_file, 0o600)
    return True


class QmpClient:
    def __init__(self, path: Path, timeout: float) -> None:
        self.path = path
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(timeout)
        self.buffer = b""

    def __enter__(self) -> "QmpClient":
        self.socket.connect(str(self.path))
        greeting = self.receive()
        if "QMP" not in greeting:
            raise QmpError("QMP greeting was not received")
        self.command("qmp_capabilities")
        return self

    def __exit__(self, *_: object) -> None:
        self.socket.close()

    def receive(self) -> dict[str, object]:
        while b"\n" not in self.buffer:
            chunk = self.socket.recv(8192)
            if not chunk:
                raise QmpError("QMP connection closed before a complete response")
            self.buffer += chunk
            if len(self.buffer) > MAX_QMP_MESSAGE:
                raise QmpError("QMP response exceeded the bounded message size")
        line, self.buffer = self.buffer.split(b"\n", 1)
        try:
            value = json.loads(line)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise QmpError("QMP returned malformed JSON") from error
        if not isinstance(value, dict):
            raise QmpError("QMP response must be an object")
        return value

    def command(self, name: str) -> object:
        request = json.dumps({"execute": name}, separators=(",", ":")).encode() + b"\n"
        self.socket.sendall(request)
        while True:
            response = self.receive()
            if "event" in response:
                continue
            if "error" in response:
                raise QmpError(f"QMP {name} failed: {response['error']}")
            if "return" not in response:
                raise QmpError(f"QMP {name} returned no result")
            return response["return"]


def atomic_json(path: Path, value: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(value, output, sort_keys=True, separators=(",", ":"))
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except BaseException:
        try:
            os.close(descriptor)
        except OSError:
            pass
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def existing_evidence(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else {}
    except (OSError, json.JSONDecodeError):
        return {}


def qmp_vm_state(status: dict[str, object]) -> str:
    if status.get("running") is True:
        return "running"
    native = status.get("status")
    if native in QMP_PAUSED_STATES:
        return "paused"
    if native in QMP_STOPPED_STATES:
        return "stopped"
    return "degraded"


def refresh_vm_evidence(path: Path, socket_path: Path, timeout: float) -> bool:
    value = existing_evidence(path)
    try:
        with QmpClient(socket_path, timeout) as client:
            status = client.command("query-status")
        if not isinstance(status, dict) or not isinstance(status.get("running"), bool):
            raise QmpError("QMP query-status returned an unexpected result")
    except (OSError, QmpError) as error:
        now = int(time.time() * 1000)
        value.update({
            "state": "unavailable", "vmState": "unavailable",
            "guestState": "unavailable", "guestProtocols": {"tcp": False, "udp": False},
            "updatedAt": now, "reason": f"QMP status refresh failed: {error}"[:512],
        })
        atomic_json(path, value)
        return False

    vm_state = qmp_vm_state(status)
    now = int(time.time() * 1000)
    value.update({"qmpStatus": status, "vmState": vm_state, "updatedAt": now})
    if vm_state != "running":
        value.update({
            "state": "stopped" if vm_state == "stopped" else "degraded",
            "guestState": "stopped" if vm_state == "stopped" else "unavailable",
            "guestProtocols": {"tcp": False, "udp": False},
            "reason": f"QMP reports VM state {vm_state}",
        })
    else:
        value.pop("reason", None)
    atomic_json(path, value)
    return vm_state == "running"


def set_state(path: Path, state: str, reason: str | None = None) -> None:
    value = existing_evidence(path)
    value.update({"state": state, "updatedAt": int(time.time() * 1000)})
    if state == "booting":
        value.update({"vmState": "booting", "guestState": "probing",
                      "guestProtocols": {"tcp": False, "udp": False}})
    elif state == "stopped":
        value.update({"vmState": "stopped", "guestState": "stopped",
                      "guestProtocols": {"tcp": False, "udp": False}})
    elif state in ("degraded", "unavailable"):
        value.update({"vmState": state, "guestState": "unavailable",
                      "guestProtocols": {"tcp": False, "udp": False}})
    if reason:
        value["reason"] = reason[:512]
    else:
        value.pop("reason", None)
    atomic_json(path, value)


def tcp_guest_probe(target: str, port: int, timeout: float) -> bool:
    try:
        with socket.create_connection((target, port), timeout=timeout) as client:
            client.settimeout(timeout)
            client.sendall(GUEST_PROBE_PAYLOAD)
            return client.recv(len(GUEST_PROBE_PAYLOAD) + 1) == GUEST_PROBE_PAYLOAD
    except OSError:
        return False


def udp_guest_probe(target: str, port: int, timeout: float) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
        client.settimeout(timeout)
        try:
            client.sendto(GUEST_PROBE_PAYLOAD, (target, port))
            reply, _ = client.recvfrom(len(GUEST_PROBE_PAYLOAD) + 1)
            return reply == GUEST_PROBE_PAYLOAD
        except OSError:
            return False


def record_guest_readiness(path: Path, tcp: bool, udp: bool,
                           state: str, reason: str | None = None) -> None:
    value = existing_evidence(path)
    qmp_status = value.get("qmpStatus")
    if not isinstance(qmp_status, dict) or qmp_vm_state(qmp_status) != "running":
        raise QmpError("guest readiness cannot be recorded without a running QMP guest")
    now = int(time.time() * 1000)
    value.update({
        "state": state,
        "vmState": qmp_vm_state(qmp_status),
        "guestState": "ready" if tcp and udp else "unavailable",
        "guestProtocols": {"tcp": tcp, "udp": udp},
        "guestReadinessAt": now,
        "updatedAt": now,
    })
    if reason:
        value["reason"] = reason[:512]
    else:
        value.pop("reason", None)
    atomic_json(path, value)


def guest_readiness(arguments: argparse.Namespace) -> None:
    deadline = time.monotonic() + arguments.wait
    while True:
        socket_path = getattr(arguments, "socket", None)
        vm_running = socket_path is None or refresh_vm_evidence(
            arguments.output, socket_path, arguments.timeout)
        tcp = vm_running and tcp_guest_probe(arguments.target, arguments.port, arguments.timeout)
        udp = vm_running and udp_guest_probe(arguments.target, arguments.port, arguments.timeout)
        if tcp and udp:
            record_guest_readiness(arguments.output, tcp, udp, "ready")
            break
        if time.monotonic() >= deadline:
            record_guest_readiness(
                arguments.output, tcp, udp, "degraded",
                "guest application did not answer both TCP and UDP probes",
            )
            raise QmpError("guest application did not answer both TCP and UDP probes")
        time.sleep(min(arguments.interval, max(0.0, deadline - time.monotonic())))

    if not arguments.monitor:
        return

    failures = 0
    while True:
        time.sleep(arguments.interval)
        vm_running = socket_path is None or refresh_vm_evidence(
            arguments.output, socket_path, arguments.timeout)
        tcp = vm_running and tcp_guest_probe(arguments.target, arguments.port, arguments.timeout)
        udp = vm_running and udp_guest_probe(arguments.target, arguments.port, arguments.timeout)
        if not vm_running:
            failures = min(failures + 1, arguments.failure_threshold)
            continue
        if tcp and udp:
            failures = 0
            record_guest_readiness(arguments.output, tcp, udp, "ready")
            continue
        failures += 1
        if failures >= arguments.failure_threshold:
            record_guest_readiness(
                arguments.output, tcp, udp, "degraded",
                "guest application readiness probe failed",
            )


def probe(arguments: argparse.Namespace) -> None:
    deadline = time.monotonic() + arguments.wait
    error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with QmpClient(arguments.socket, arguments.timeout) as client:
                status = client.command("query-status")
                kvm = client.command("query-kvm")
            break
        except (OSError, QmpError) as current:
            error = current
            time.sleep(0.1)
    else:
        set_state(arguments.output, "unavailable", f"QMP probe failed: {error}")
        raise QmpError(f"QMP did not become ready: {error}")

    if not isinstance(status, dict) or not isinstance(kvm, dict):
        raise QmpError("QMP runtime queries returned unexpected results")
    present = kvm.get("present") is True
    enabled = kvm.get("enabled") is True
    if arguments.selected == "kvm" and not (present and enabled):
        set_state(arguments.output, "degraded", "KVM was selected but QMP did not confirm it")
        raise QmpError("KVM selected but query-kvm did not report present=true and enabled=true")
    actual = "kvm" if present and enabled else arguments.selected
    evidence = {
        "schemaVersion": 1,
        "state": "booting",
        "vmState": "running" if status.get("running") is True else "booting",
        "guestState": "probing",
        "guestProtocols": {"tcp": False, "udp": False},
        "requestedAccelerator": arguments.requested,
        "selectedAccelerator": arguments.selected,
        "actualAccelerator": actual,
        "evidenceSource": "QMP query-status + query-kvm",
        "qmpStatus": status,
        "kvm": {"present": present, "enabled": enabled},
        "updatedAt": int(time.time() * 1000),
    }
    atomic_json(arguments.output, evidence)
    print(json.dumps(evidence, sort_keys=True))


def shutdown(arguments: argparse.Namespace) -> None:
    try:
        with QmpClient(arguments.socket, arguments.timeout) as client:
            try:
                client.command("system_powerdown")
            except QmpError:
                pass
    except (OSError, QmpError) as error:
        raise QmpError(f"could not request QMP shutdown: {error}") from error

    deadline = time.monotonic() + arguments.wait
    while time.monotonic() < deadline:
        if not arguments.socket.exists():
            return
        time.sleep(0.2)
    try:
        with QmpClient(arguments.socket, arguments.timeout) as client:
            client.command("quit")
    except (OSError, QmpError):
        pass


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    commands = result.add_subparsers(dest="command", required=True)
    probe_parser = commands.add_parser("probe")
    probe_parser.add_argument("--socket", type=Path, required=True)
    probe_parser.add_argument("--output", type=Path, required=True)
    probe_parser.add_argument("--requested", choices=("auto", "kvm", "tcg", "hvf"), required=True)
    probe_parser.add_argument("--selected", choices=("kvm", "tcg", "hvf"), required=True)
    probe_parser.add_argument("--timeout", type=float, default=2.0)
    probe_parser.add_argument("--wait", type=float, default=15.0)
    shutdown_parser = commands.add_parser("shutdown")
    shutdown_parser.add_argument("--socket", type=Path, required=True)
    shutdown_parser.add_argument("--timeout", type=float, default=2.0)
    shutdown_parser.add_argument("--wait", type=float, default=5.0)
    state_parser = commands.add_parser("state")
    state_parser.add_argument("--output", type=Path, required=True)
    state_parser.add_argument("--state", choices=("not-started", "booting", "ready", "degraded", "stopped", "unavailable"), required=True)
    state_parser.add_argument("--reason")
    readiness_parser = commands.add_parser("guest-readiness")
    readiness_parser.add_argument("--target", required=True)
    readiness_parser.add_argument("--port", type=int, required=True)
    readiness_parser.add_argument("--output", type=Path, required=True)
    readiness_parser.add_argument("--socket", type=Path, required=True)
    readiness_parser.add_argument("--timeout", type=float, default=0.5)
    readiness_parser.add_argument("--wait", type=float, default=60.0)
    readiness_parser.add_argument("--interval", type=float, default=2.0)
    readiness_parser.add_argument("--failure-threshold", type=int, default=3)
    readiness_parser.add_argument("--monitor", action="store_true")
    readiness_parser.add_argument("--daemonize", action="store_true")
    readiness_parser.add_argument("--pid-file", type=Path)
    readiness_parser.add_argument("--log-file", type=Path)
    return result


def main() -> int:
    arguments = parser().parse_args()
    try:
        if arguments.command == "probe":
            probe(arguments)
        elif arguments.command == "shutdown":
            shutdown(arguments)
        elif arguments.command == "guest-readiness":
            if not 1 <= arguments.port <= 65535:
                raise ValueError("guest readiness port must be from 1 through 65535")
            if not 0.05 <= arguments.timeout <= 10 or not 0 <= arguments.wait <= 300:
                raise ValueError("guest readiness timeout or wait is outside its bound")
            if not 0.1 <= arguments.interval <= 60 or not 1 <= arguments.failure_threshold <= 10:
                raise ValueError("guest readiness interval or failure threshold is outside its bound")
            if arguments.daemonize:
                if not arguments.monitor or arguments.pid_file is None or arguments.log_file is None:
                    raise ValueError("daemonized readiness requires monitor, pid-file, and log-file")
                if not daemonize(arguments.pid_file, arguments.log_file):
                    return 0
            guest_readiness(arguments)
        else:
            set_state(arguments.output, arguments.state, arguments.reason)
        return 0
    except (OSError, QmpError, ValueError) as error:
        print(f"qmp-control: {error}", file=os.sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
