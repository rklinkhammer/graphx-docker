#!/usr/bin/env bash
set -euo pipefail

example_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo_dir="$(cd "$example_dir/../.." && pwd)"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/graphx-qemu-test.XXXXXX")"
cleanup() {
  find "$test_dir" -type f -delete 2>/dev/null || true
  rmdir "$test_dir" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

cc -std=c11 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
  "$example_dir/guest/src/qemu_network_node.c" -o "$test_dir/qemu-network-node"
python3 -m py_compile "$example_dir/host/peer.py" \
  "$example_dir/tools/capture_history.py" "$example_dir/tools/query_history.py"

python3 - "$test_dir/fixture.pcap" <<'PY'
import socket
import struct
import sys

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
        capture.write(struct.pack("<IIII", 1700000000 + number, 0, len(frame), len(frame)))
        capture.write(frame)
PY

python3 "$example_dir/tools/capture_history.py" "$test_dir/fixture.pcap" \
  "$test_dir/history.sqlite" --max-records 3 --preview-bytes 8
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

if [[ -x "$repo_dir/build/dev/graphx" ]]; then
  "$repo_dir/build/dev/graphx" validate "$example_dir/graphx.yaml"
else
  echo "SKIP GraphX config validation: build/dev/graphx is not built"
fi
echo "PASS QEMU node static and packet-history tests"
