#!/usr/bin/env bash
set -euo pipefail

if (($# != 3)); then
  echo "usage: test-wireshark.sh TSHARK FIXTURE_WRITER LUA_DISSECTOR" >&2
  exit 64
fi

TSHARK=$1
FIXTURE_WRITER=$2
DISSECTOR=$3
TEST_DIR=$(mktemp -d "${TMPDIR:-/tmp}/graphx-wireshark.XXXXXX")
cleanup() { find "$TEST_DIR" -type f -delete; rmdir "$TEST_DIR"; }
trap cleanup EXIT INT TERM

TSHARK_COMMAND=("$TSHARK")
if ((EUID == 0)); then
  command -v runuser >/dev/null || {
    echo "tshark disables Lua as root and runuser is unavailable" >&2
    exit 2
  }
  chmod 0755 "$TEST_DIR"
  TSHARK_COMMAND=(runuser -u nobody -- env HOME=/tmp "$TSHARK")
fi

"$FIXTURE_WRITER" "$TEST_DIR/graphx.pcapng"
chmod 0644 "$TEST_DIR/graphx.pcapng"
"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/graphx.pcapng" \
  -T fields -E separator=, -e graphx.version -e graphx.sequence -e graphx.type \
  -e graphx.attribute_count >"$TEST_DIR/fields.txt"
grep -q '^1,7,6c65676163792e73616d706c65,1$' "$TEST_DIR/fields.txt"
grep -q '^2,42,73616d706c65,2$' "$TEST_DIR/fields.txt"

"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/graphx.pcapng" \
  -Y 'graphx.version == 2 && graphx.sequence == 42 && graphx.message_id == 00112233445566778899aabbccddeeff' \
  -T fields -e graphx.trace_id >"$TEST_DIR/filter.txt"
grep -q '^0123456789abcdef0123456789abcdef$' "$TEST_DIR/filter.txt"

python3 - "$TEST_DIR/graphx.pcapng" "$TEST_DIR/zero-message.pcapng" \
  "$TEST_DIR/zero-trace.pcapng" "$TEST_DIR/zero-parent.pcapng" \
  "$TEST_DIR/duplicate-attribute.pcapng" <<'PY'
from pathlib import Path
import sys

source = bytearray(Path(sys.argv[1]).read_bytes())
at = source.find(b'GXE\x02')
if at < 0:
    raise SystemExit('fixture has no v2 envelope')

for output, offset in ((sys.argv[2], 20), (sys.argv[3], 36), (sys.argv[4], 52)):
    data = bytearray(source)
    data[at + offset:at + offset + 16] = bytes(16)
    Path(output).write_bytes(data)

duplicate = bytearray(source)
position = at + 68
type_length = int.from_bytes(duplicate[position:position + 4], 'big')
position += 4 + type_length
attribute_count = int.from_bytes(duplicate[position:position + 4], 'big')
position += 4
keys = []
for _ in range(attribute_count):
    key_length = int.from_bytes(duplicate[position:position + 4], 'big')
    position += 4
    keys.append((position, key_length))
    position += key_length
    value_length = int.from_bytes(duplicate[position:position + 4], 'big')
    position += 4 + value_length
if len(keys) < 2 or keys[0][1] != keys[1][1]:
    raise SystemExit('fixture needs two equal-length attribute keys')
duplicate[keys[1][0]:keys[1][0] + keys[1][1]] = duplicate[keys[0][0]:keys[0][0] + keys[0][1]]
Path(sys.argv[5]).write_bytes(duplicate)
PY
chmod 0644 "$TEST_DIR"/zero-*.pcapng "$TEST_DIR/duplicate-attribute.pcapng"

"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/zero-message.pcapng" \
  -Y '_ws.expert.message contains "zero envelope message identity"' \
  -T fields -e frame.number >"$TEST_DIR/zero-message.txt"
grep -q '^2$' "$TEST_DIR/zero-message.txt"

"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/zero-trace.pcapng" \
  -Y '_ws.expert.message contains "zero envelope trace identity"' \
  -T fields -e frame.number >"$TEST_DIR/zero-trace.txt"
grep -q '^2$' "$TEST_DIR/zero-trace.txt"

"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/zero-parent.pcapng" \
  -Y 'frame.number == 2 && graphx.version == 2' -T fields -e graphx.parent_message_id \
  >"$TEST_DIR/zero-parent.txt"
grep -q '^00000000000000000000000000000000$' "$TEST_DIR/zero-parent.txt"
"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/zero-parent.pcapng" \
  -Y '_ws.expert.message contains "zero envelope"' -T fields -e frame.number \
  >"$TEST_DIR/zero-parent-expert.txt"
test ! -s "$TEST_DIR/zero-parent-expert.txt"

"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/duplicate-attribute.pcapng" \
  -Y '_ws.expert.message contains "duplicate envelope attribute key"' \
  -T fields -e frame.number >"$TEST_DIR/duplicate-attribute.txt"
grep -q '^2$' "$TEST_DIR/duplicate-attribute.txt"

cp "$TEST_DIR/graphx.pcapng" "$TEST_DIR/malformed.pcapng"
python3 - "$TEST_DIR/malformed.pcapng" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
at = data.find(b'GXE\x01')
if at < 0:
    raise SystemExit('fixture has no v1 envelope')
data[at] = ord('B')
path.write_bytes(data)
PY
chmod 0644 "$TEST_DIR/malformed.pcapng"
"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/malformed.pcapng" \
  -Y '_ws.expert.message contains "invalid GraphX magic"' -T fields -e frame.number \
  >"$TEST_DIR/malformed.txt"
grep -q '^1$' "$TEST_DIR/malformed.txt"

cp "$TEST_DIR/graphx.pcapng" "$TEST_DIR/unsupported.pcapng"
python3 - "$TEST_DIR/unsupported.pcapng" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
at = data.find(b'GXE\x01')
if at < 0:
    raise SystemExit('fixture has no v1 envelope')
data[at + 3] = 3
path.write_bytes(data)
PY
chmod 0644 "$TEST_DIR/unsupported.pcapng"
"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/unsupported.pcapng" \
  -Y '_ws.expert.message contains "unsupported envelope wire version 3"' \
  -T fields -e frame.number >"$TEST_DIR/unsupported.txt"
grep -q '^1$' "$TEST_DIR/unsupported.txt"

cp "$TEST_DIR/graphx.pcapng" "$TEST_DIR/bad-length.pcapng"
python3 - "$TEST_DIR/bad-length.pcapng" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
data = bytearray(path.read_bytes())
at = data.find(b'GXE\x01')
if at < 4:
    raise SystemExit('fixture has no framed v1 envelope')
data[at - 4:at] = b'\x00\x00\x00\x00'
path.write_bytes(data)
PY
chmod 0644 "$TEST_DIR/bad-length.pcapng"
"${TSHARK_COMMAND[@]}" -n -X "lua_script:$DISSECTOR" -r "$TEST_DIR/bad-length.pcapng" \
  -Y '_ws.expert.message contains "envelope length is outside"' \
  -T fields -e frame.number >"$TEST_DIR/bad-length.txt"
grep -q '^1$' "$TEST_DIR/bad-length.txt"

echo "GraphX Wireshark dissector validation passed"
