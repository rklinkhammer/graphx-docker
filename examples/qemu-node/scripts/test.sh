#!/usr/bin/env bash
set -euo pipefail

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
graphx_cli=${1:-}
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-qemu-test.XXXXXX")"
cleanup() {
  find "$test_dir" -type f -delete 2>/dev/null || true
  rmdir "$test_dir" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
  "$example_dir/guest/src/qemu_network_node.c" -o "$test_dir/qemu-network-node"
python3 -m py_compile "$example_dir/host/peer.py" \
  "$example_dir/host/relay.py" "$example_dir/tools/capture_history.py" \
  "$example_dir/tools/packet_observer.py" "$example_dir/tools/query_history.py" \
  "$example_dir/tools/qmp_control.py" "$example_dir/tools/artifact_manifest.py"
bash -n "$example_dir/scripts/demo-profile.sh" \
  "$example_dir/scripts/inspect-capture.sh" \
  "$example_dir/external/scripts/demo.sh" "$example_dir/container/scripts/demo.sh" \
  "$example_dir/container/qemu-entrypoint.sh" "$example_dir/container/qemu-healthcheck.sh"

python3 - "$test_dir/fixture.pcap" <<'PY'
import socket
import struct
import sys
import time

output = sys.argv[1]
ethernet = bytes.fromhex("5254001234565254006543210800")

def packet(protocol, source_port, destination_port, payload):
    if protocol == 17:
        transport = struct.pack("!HHHH", source_port, destination_port, 8 + len(payload), 0) + payload
    else:
        transport = struct.pack("!HHIIHHHH", source_port, destination_port, 1, 0, 0x5018, 8192, 0, 0) + payload
    ip = bytearray(20)
    ip[0] = 0x45
    struct.pack_into("!H", ip, 2, 20 + len(transport))
    ip[8] = 64
    ip[9] = protocol
    ip[12:16] = socket.inet_aton("10.0.2.15")
    ip[16:20] = socket.inet_aton("10.0.2.2")
    return ethernet + ip + transport

with open(output, "wb") as capture:
    capture.write(struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))
    for number, protocol in enumerate((6, 17, 6, 17), 1):
        frame = packet(protocol, 18001, 19001, f"message-{number}".encode())
        capture.write(struct.pack("<IIII", int(time.time()) + number, 0, len(frame), len(frame)))
        capture.write(frame)
PY

python3 "$example_dir/tools/capture_history.py" "$test_dir/fixture.pcap" \
  "$test_dir/history.sqlite" --max-records 3 --preview-bytes 8 \
  --pcapng "$test_dir/qemu-node.pcapng"
[[ -s "$test_dir/qemu-node.pcapng" ]]
cat >"$test_dir/fake-tshark" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
test "$1" = -r && test "$2" = - && test "$3" = -Y && test -n "$4"
printf 'stdin-bytes=%s\n' "$(wc -c | tr -d '[:space:]')"
SH
chmod 0700 "$test_dir/fake-tshark"
TSHARK="$test_dir/fake-tshark" \
  "$example_dir/scripts/inspect-capture.sh" "$test_dir" \
  | grep -Eq '^stdin-bytes=[1-9][0-9]*$'
count="$(python3 - "$test_dir/history.sqlite" <<'PY'
import sqlite3
import sys
connection = sqlite3.connect(sys.argv[1])
print(connection.execute("SELECT COUNT(*) FROM packet_history").fetchone()[0])
PY
)"
[[ "$count" == 3 ]]
python3 "$example_dir/tools/query_history.py" "$test_dir/history.sqlite" --limit 3 \
  | grep -q "UDP 10.0.2.15:18001 -> 10.0.2.2:19001"

GRAPHX_QEMU_RUN_ID=static-test \
GRAPHX_TELEMETRY_HOST=127.0.0.1 \
GRAPHX_TELEMETRY_PORT=9 \
python3 "$example_dir/tools/packet_observer.py" \
  --capture "$test_dir/fixture.pcap" --pcapng "$test_dir/live.pcapng" \
  --database "$test_dir/packet-history.sqlite" --once >/dev/null
[[ -s "$test_dir/live.pcapng" ]]
observer_count="$(python3 - "$test_dir/packet-history.sqlite" <<'PY'
import sqlite3
import sys
connection = sqlite3.connect(sys.argv[1])
print(connection.execute("SELECT COUNT(*) FROM packet_history").fetchone()[0])
PY
)"
[[ "$observer_count" == 4 ]]

python3 - "$example_dir/tools/packet_observer.py" "$test_dir" <<'PY'
import importlib.util
import os
import shutil
import struct
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location("packet_observer", sys.argv[1])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
root = Path(sys.argv[2])
source = root / "replace.pcap"
shutil.copyfile(root / "fixture.pcap", source)
observer = module.Observer(source, root / "replace.pcapng", root / "replace.sqlite")
assert observer.read_available() and observer.observed == 4
replacement = root / "replacement.pcap"
shutil.copyfile(root / "fixture.pcap", replacement)
os.replace(replacement, source)
assert observer.read_available() and observer.observed == 8 and observer.capture_generation == 1
observer.close()

# Impossible records are dropped without poisoning the stream; a following valid
# packet is still observed and retained with explicit capture semantics.
fixture = (root / "fixture.pcap").read_bytes()
included = struct.unpack_from("<I", fixture, 32)[0]
frame = fixture[40:40 + included]
header = struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, len(frame), 1)
valid = struct.pack("<IIII", struct.unpack_from("<I", fixture, 24)[0], 0,
                    len(frame), len(frame)) + frame
bad_records = {
    "zero": struct.pack("<IIII", 1, 0, 0, 0),
    "larger-than-original": struct.pack("<IIII", 1, 0, len(frame), 1) + frame,
    "larger-than-snaplen": struct.pack("<IIII", 1, 0, len(frame) + 1, len(frame) + 1) + frame + b"x",
    "unbounded-original": struct.pack("<IIII", 1, 0, len(frame), 0xffffffff) + frame,
    "bad-fraction": struct.pack("<IIII", 1, 1_000_000, len(frame), len(frame)) + frame,
}
for name, bad in bad_records.items():
    capture = root / f"recover-{name}.pcap"
    capture.write_bytes(header + bad + valid)
    candidate = module.Observer(capture, root / f"recover-{name}.pcapng",
                                root / f"recover-{name}.sqlite")
    candidate.telemetry_host = "127.0.0.1"
    candidate.telemetry_port = 9
    assert candidate.read_available()
    assert candidate.dropped == 1 and candidate.observed == 1
    record = candidate.query(1, None, None)["records"][0]
    assert record["capturedLength"] == len(frame)
    assert record["originalLength"] == len(frame)
    assert record["truncated"] is False
    candidate.close()

# An older history database is migrated in place.
legacy = root / "legacy.sqlite"
connection = __import__("sqlite3").connect(legacy)
connection.execute("CREATE TABLE packet_history (id INTEGER PRIMARY KEY, capture_session TEXT NOT NULL, capture_offset INTEGER NOT NULL, captured_at REAL NOT NULL, direction TEXT NOT NULL, edge_id TEXT NOT NULL, protocol TEXT NOT NULL, source_address TEXT NOT NULL, destination_address TEXT NOT NULL, source_port INTEGER NOT NULL, destination_port INTEGER NOT NULL, wire_length INTEGER NOT NULL, payload_length INTEGER NOT NULL, payload_preview_hex TEXT NOT NULL, UNIQUE(capture_session, capture_offset))")
connection.close()
migrated = module.Observer(root / "missing.pcap", root / "legacy.pcapng", legacy)
columns = {row[1] for row in migrated.connection.execute("PRAGMA table_info(packet_history)")}
assert {"captured_length", "original_length", "truncated"} <= columns
migrated.close()

for name, data in (
    ("partial", b"\xd4\xc3\xb2\xa1"),
    ("invalid", b"not-pcap" + bytes(17)),
    ("oversized", struct.pack("<IHHIIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1) +
     struct.pack("<IIII", 1, 0, module.MAX_PACKET_BYTES + 1, module.MAX_PACKET_BYTES + 1)),
):
    capture = root / f"{name}.pcap"
    capture.write_bytes(data)
    candidate = module.Observer(capture, root / f"{name}.pcapng", root / f"{name}.sqlite")
    assert not candidate.read_available()
    candidate.close()
PY

python3 - "$example_dir/tools/qmp_control.py" "$test_dir" <<'PY'
import importlib.util
import json
import socket
import sys
import threading
from pathlib import Path

spec = importlib.util.spec_from_file_location("qmp_control", sys.argv[1])
qmp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qmp)
root = Path(sys.argv[2])
path = root / "mock.qmp"

def server():
    with socket.socket(socket.AF_UNIX) as listener:
        listener.bind(str(path)); listener.listen(1)
        connection, _ = listener.accept()
        with connection:
            connection.sendall(b'{"QMP":{"version":{}}}\n')
            for response in ({}, {"running": True, "status": "running"},
                             {"present": False, "enabled": False}):
                request = json.loads(connection.makefile("rb").readline())
                assert request["execute"] in ("qmp_capabilities", "query-status", "query-kvm")
                connection.sendall((json.dumps({"return": response}) + "\n").encode())

thread = threading.Thread(target=server); thread.start()
arguments = type("Arguments", (), {"socket": path, "output": root / "evidence.json",
    "requested": "auto", "selected": "tcg", "timeout": 1.0, "wait": 2.0})()
qmp.probe(arguments); thread.join()
evidence = json.loads(arguments.output.read_text())
assert evidence["actualAccelerator"] == "tcg" and evidence["state"] == "booting"
assert evidence["vmState"] == "running" and evidence["guestState"] == "probing"

def status_server(status):
    if path.exists():
        path.unlink()
    started = threading.Event()
    def serve():
        with socket.socket(socket.AF_UNIX) as listener:
            listener.bind(str(path)); listener.listen(1)
            started.set()
            connection, _ = listener.accept()
            with connection:
                connection.sendall(b'{"QMP":{"version":{}}}\n')
                stream = connection.makefile("rb")
                for response in ({}, status):
                    json.loads(stream.readline())
                    connection.sendall((json.dumps({"return": response}) + "\n").encode())
    candidate = threading.Thread(target=serve); candidate.start()
    assert started.wait(1)
    return candidate

# A fresh QMP status, not the startup snapshot, drives the independent VM layer.
thread = status_server({"running": False, "status": "paused"})
assert not qmp.refresh_vm_evidence(arguments.output, path, 1.0)
thread.join()
paused = json.loads(arguments.output.read_text())
assert paused["vmState"] == "paused" and paused["state"] == "degraded"
assert paused["guestState"] == "unavailable"
thread = status_server({"running": True, "status": "running"})
assert qmp.refresh_vm_evidence(arguments.output, path, 1.0)
thread.join()
refreshed = json.loads(arguments.output.read_text())
assert refreshed["vmState"] == "running" and refreshed["qmpStatus"]["status"] == "running"

def free_port():
    with socket.socket() as candidate:
        candidate.bind(("127.0.0.1", 0))
        return candidate.getsockname()[1]

def tcp_echo(port, started):
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", port)); listener.listen(1)
        started.set()
        connection, _ = listener.accept()
        with connection:
            data = connection.recv(1024); connection.sendall(data)

def udp_echo(port, started):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
        listener.bind(("127.0.0.1", port))
        started.set()
        data, peer = listener.recvfrom(1024); listener.sendto(data, peer)

def readiness(port):
    return type("Readiness", (), {"target": "127.0.0.1", "port": port,
        "output": arguments.output, "timeout": 0.1, "wait": 0.0,
        "interval": 0.1, "failure_threshold": 1, "monitor": False})()

# QMP liveness alone and either protocol alone cannot promote guest readiness.
try:
    qmp.guest_readiness(readiness(free_port()))
    raise AssertionError("QMP-only guest was accepted as ready")
except qmp.QmpError:
    pass
port = free_port()
tcp_started = threading.Event()
tcp_thread = threading.Thread(target=tcp_echo, args=(port, tcp_started)); tcp_thread.start()
assert tcp_started.wait(1)
try:
    qmp.guest_readiness(readiness(port))
    raise AssertionError("TCP-only guest was accepted as ready")
except qmp.QmpError:
    pass
tcp_thread.join()
protocols = json.loads(arguments.output.read_text())["guestProtocols"]
assert protocols == {"tcp": True, "udp": False}

# Both protocols promote readiness; subsequent total loss demotes it.
port = free_port()
tcp_started = threading.Event(); udp_started = threading.Event()
tcp_thread = threading.Thread(target=tcp_echo, args=(port, tcp_started))
udp_thread = threading.Thread(target=udp_echo, args=(port, udp_started))
tcp_thread.start(); udp_thread.start()
assert tcp_started.wait(1) and udp_started.wait(1)
qmp.guest_readiness(readiness(port)); tcp_thread.join(); udp_thread.join()
ready = json.loads(arguments.output.read_text())
assert ready["state"] == "ready" and ready["guestState"] == "ready"
assert ready["guestProtocols"] == {"tcp": True, "udp": True}
try:
    qmp.guest_readiness(readiness(port))
    raise AssertionError("guest readiness loss was not detected")
except qmp.QmpError:
    pass
assert json.loads(arguments.output.read_text())["state"] == "degraded"

# A paused or temporarily unavailable VM must not terminate the continuous
# monitor; a later running state and dual-protocol probes restore readiness.
original_refresh = qmp.refresh_vm_evidence
original_tcp = qmp.tcp_guest_probe
original_udp = qmp.udp_guest_probe
original_sleep = qmp.time.sleep
states = iter((True, False, True))
def simulated_refresh(output, _socket, _timeout):
    running = next(states)
    value = qmp.existing_evidence(output)
    value.update({"qmpStatus": {"running": running,
                                "status": "running" if running else "paused"},
                  "vmState": "running" if running else "paused"})
    if not running:
        value.update({"state": "degraded", "guestState": "unavailable",
                      "guestProtocols": {"tcp": False, "udp": False}})
    qmp.atomic_json(output, value)
    return running
qmp.refresh_vm_evidence = simulated_refresh
qmp.tcp_guest_probe = lambda *_: True
qmp.udp_guest_probe = lambda *_: True
qmp.time.sleep = lambda *_: None
monitor = type("Monitor", (), {"target": "127.0.0.1", "port": 1,
    "socket": root / "simulated.qmp", "output": arguments.output,
    "timeout": 0.1, "wait": 0.0, "interval": 0.1,
    "failure_threshold": 1, "monitor": True})()
try:
    qmp.guest_readiness(monitor)
    raise AssertionError("continuous monitor did not execute its recovery cycle")
except StopIteration:
    pass
finally:
    qmp.refresh_vm_evidence = original_refresh
    qmp.tcp_guest_probe = original_tcp
    qmp.udp_guest_probe = original_udp
    qmp.time.sleep = original_sleep
recovered = json.loads(arguments.output.read_text())
assert recovered["state"] == "ready" and recovered["vmState"] == "running"
assert recovered["guestProtocols"] == {"tcp": True, "udp": True}

# Loss of the QMP control socket must fail the VM, guest, and protocol layers
# closed without discarding the previously established accelerator evidence.
lost = root / "lost-evidence.json"
lost.write_text(json.dumps(recovered))
assert not qmp.refresh_vm_evidence(lost, root / "lost.qmp", 0.05)
lost_value = json.loads(lost.read_text())
assert lost_value["state"] == "unavailable"
assert lost_value["vmState"] == "unavailable"
assert lost_value["guestState"] == "unavailable"
assert lost_value["guestProtocols"] == {"tcp": False, "udp": False}
assert lost_value["actualAccelerator"] == "tcg"

missing = type("Arguments", (), {"socket": root / "missing.qmp",
    "output": root / "missing-evidence.json", "requested": "auto", "selected": "tcg",
    "timeout": 0.05, "wait": 0.1})()
try:
    qmp.probe(missing)
    raise AssertionError("missing QMP socket was accepted")
except qmp.QmpError:
    assert json.loads(missing.output.read_text())["state"] == "unavailable"

path.unlink()
def kvm_server():
    with socket.socket(socket.AF_UNIX) as listener:
        listener.bind(str(path)); listener.listen(1)
        connection, _ = listener.accept()
        with connection:
            connection.sendall(b'{"QMP":{"version":{}}}\n')
            stream = connection.makefile("rb")
            for response in ({}, {"running": True}, {"present": True, "enabled": False}):
                json.loads(stream.readline())
                connection.sendall((json.dumps({"return": response}) + "\n").encode())
thread = threading.Thread(target=kvm_server); thread.start()
kvm = type("Arguments", (), {"socket": path, "output": root / "kvm-evidence.json",
    "requested": "kvm", "selected": "kvm", "timeout": 1.0, "wait": 2.0})()
try:
    qmp.probe(kvm)
    raise AssertionError("unverified KVM was accepted")
except qmp.QmpError:
    pass
thread.join()
assert json.loads(kvm.output.read_text())["state"] == "degraded"
PY

mkdir -p "$test_dir/images" "$test_dir/guest"
printf kernel >"$test_dir/images/bzImage"
printf rootfs >"$test_dir/images/rootfs.cpio.gz"
printf source >"$test_dir/guest/node.c"
python3 "$example_dir/tools/artifact_manifest.py" create --images "$test_dir/images" \
  --guest "$test_dir/guest" --buildroot-version test --source-date-epoch 1 >/dev/null
python3 "$example_dir/tools/artifact_manifest.py" verify --images "$test_dir/images" >/dev/null
printf tampered >>"$test_dir/images/bzImage"
! python3 "$example_dir/tools/artifact_manifest.py" verify --images "$test_dir/images" >/dev/null 2>&1

for profile in external container; do
  for edge in origin-qemu-tcp qemu-receiver-tcp origin-qemu-udp qemu-receiver-udp; do
    grep -q "id: $edge" "$example_dir/$profile/graphx.yaml"
  done
  grep -q 'framing: none' "$example_dir/$profile/graphx.yaml"
done
! grep -q 'privileged:' "$example_dir/container/compose.yaml"
! grep -q '/captures/qemu.qmp' "$example_dir/container/qemu-entrypoint.sh"
grep -q 'qmp_socket=/run/graphx-qemu/qemu.qmp' "$example_dir/container/qemu-entrypoint.sh"
grep -q '/run/graphx-qemu:uid=${GRAPHX_HOST_UID:-65532},gid=${GRAPHX_HOST_GID:-65532},mode=0700' \
    "$example_dir/container/compose.yaml"

# The only permitted graph-profile differences are deployment ownership and
# profile-specific transport destinations. Logical nodes, ports, and edges are identical.
python3 - "$example_dir/external/graphx.yaml" "$example_dir/container/graphx.yaml" <<'PY'
import re
import sys

def contract(path):
    source = open(path, encoding="utf-8").read()
    nodes = {}
    graph = source.split("  edges:\n", 1)[0]
    for match in re.finditer(r"    - id: ([^\n]+)\n(.*?)(?=    - id: |\Z)", graph, re.S):
        node_id, block = match.groups()
        ports = tuple(re.findall(r"name: ([^,]+), direction: ([^,]+), schema: ([^ }]+)", block))
        nodes[node_id] = ports
    edges = set(re.findall(
        r"\{ id: ([^,]+), from: ([^,]+), to: ([^,]+), transport: ([^,]+), data_plane: ([^ }]+) \}",
        source))
    return nodes, edges

external = contract(sys.argv[1])
container = contract(sys.argv[2])
assert external == container, "logical topology differs outside the deployment allowlist"
expected = {"origin-qemu-udp", "origin-qemu-tcp", "qemu-receiver-udp", "qemu-receiver-tcp"}
assert {edge[0] for edge in external[1]} == expected
PY

if docker compose version >/dev/null 2>&1; then
  export GRAPHX_QEMU_RUN_DIR="$test_dir/run"
  export GRAPHX_QEMU_RUN_ID=static-test
  export GRAPHX_TELEMETRY_SHARED_SECRET=0123456789abcdef0123456789abcdef
  export GRAPHX_CONTROL_TOKEN=abcdef0123456789abcdef0123456789
  export GRAPHX_QEMU_ACCEL=tcg
  export GRAPHX_QEMU_HOST_GATEWAY=172.30.12.1
  mkdir -p "$GRAPHX_QEMU_RUN_DIR"
  docker compose -f "$example_dir/external/compose.yaml" \
    -f "$example_dir/external/compose.history.yaml" config --format json \
    >"$test_dir/external-compose.json"
  python3 - "$test_dir/external-compose.json" <<'PY'
import json
import sys

services = json.load(open(sys.argv[1], encoding="utf-8"))["services"]
for name in ("host-origin", "telemetry"):
    aliases = services[name]["extra_hosts"]
    if isinstance(aliases, list):
        assert any(value in aliases for value in (
            "host.docker.internal:172.30.12.1",
            "host.docker.internal=172.30.12.1",
        )), (name, aliases)
    else:
        assert aliases["host.docker.internal"] == "172.30.12.1", (name, aliases)
PY
  docker compose -f "$example_dir/container/compose.yaml" \
    -f "$example_dir/container/compose.history.yaml" config --quiet
else
  echo "SKIP QEMU Compose validation: docker compose is unavailable"
fi

grep -q 'hostfwd=tcp:\$bind_address:18001-:18001' "$example_dir/scripts/demo-profile.sh"
grep -q 'hostfwd=tcp:\$bind_address:18002-:18001' "$example_dir/scripts/demo-profile.sh"
grep -q -- '--target "$readiness_target" --port 18002' "$example_dir/scripts/demo-profile.sh"
grep -q -- '--target 127.0.0.1 --port 18002' "$example_dir/container/qemu-entrypoint.sh"
grep -q 'Demo verification failed; cleaning up owned runtime resources' \
  "$example_dir/scripts/demo-profile.sh"
grep -q 'all four raw TCP/UDP edge counters advanced' "$example_dir/scripts/demo-profile.sh"

if test "$(uname -s)" != Linux; then
  set +e
  platform_output=$(GRAPHX_QEMU_GUI_PORT=not-a-port \
    "$example_dir/container/scripts/demo.sh" start --accel tcg 2>&1)
  platform_status=$?
  set -e
  test "$platform_status" -eq 2
  grep -q 'supported only on native Linux' <<<"$platform_output"
fi

if [[ -z "$graphx_cli" && -n "${GRAPHX_BUILD_DIR:-}" ]]; then
  graphx_cli="$GRAPHX_BUILD_DIR/graphx"
fi
if [[ -z "$graphx_cli" && -x "$repo_dir/build/dev/graphx" ]]; then
  graphx_cli="$repo_dir/build/dev/graphx"
fi
if [[ -n "$graphx_cli" && -x "$graphx_cli" ]]; then
  "$graphx_cli" validate "$example_dir/graphx.yaml"
  "$graphx_cli" validate "$example_dir/external/graphx.yaml"
  "$graphx_cli" validate "$example_dir/container/graphx.yaml"
else
  echo "SKIP GraphX config validation: pass the active GraphX CLI as argument 1"
fi
echo "PASS QEMU node static and packet-history tests"
