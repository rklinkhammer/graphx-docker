#!/usr/bin/env bash
set -euo pipefail

if (($# != 3)); then
  echo "usage: test-wireshark.sh TSHARK FIXTURE_WRITER LUA_DISSECTOR" >&2
  exit 64
fi

tshark=$1
fixture_writer=$2
dissector=$3
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/graphx-wireshark.XXXXXX")
trap 'find "$test_dir" -type f -delete; rmdir "$test_dir"' EXIT INT TERM

chmod 0755 "$test_dir"
cp "$dissector" "$test_dir/graphx.lua"
"$fixture_writer" "$test_dir/graphx.pcapng"
chmod 0644 "$test_dir/graphx.lua" "$test_dir/graphx.pcapng"

command_line=(env HOME="$test_dir" XDG_CONFIG_HOME="$test_dir" "$tshark")
if ((EUID == 0)); then
  command -v runuser >/dev/null || exit 2
  command_line=(runuser -u nobody -- env HOME="$test_dir" XDG_CONFIG_HOME="$test_dir" "$tshark")
fi

"${command_line[@]}" -n -X "lua_script:$test_dir/graphx.lua" \
  -r "$test_dir/graphx.pcapng" -T fields -E separator=, \
  -e graphx.version -e graphx.sequence -e graphx.type -e graphx.attribute_count \
  >"$test_dir/fields.txt"
grep -q '^2,42,73616d706c65,2$' "$test_dir/fields.txt"

python3 - "$test_dir/graphx.pcapng" "$test_dir/unsupported.pcapng" <<'PY'
from pathlib import Path
import sys

data = bytearray(Path(sys.argv[1]).read_bytes())
at = data.find(b'GXE\x02')
if at < 0:
    raise SystemExit('fixture has no GraphX envelope')
data[at + 3] = 3
Path(sys.argv[2]).write_bytes(data)
PY
chmod 0644 "$test_dir/unsupported.pcapng"
"${command_line[@]}" -n -X "lua_script:$test_dir/graphx.lua" \
  -r "$test_dir/unsupported.pcapng" \
  -Y '_ws.expert.message contains "unsupported envelope wire version 3"' \
  -T fields -e frame.number >"$test_dir/unsupported.txt"
grep -q '^1$' "$test_dir/unsupported.txt"

echo "GraphX Wireshark dissector validation passed"
