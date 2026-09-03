# GraphX Wireshark integration

`graphx.lua` is the supported Phase 9 dissector for GraphX application capture.
It binds only to Wireshark's USER0 encapsulation table, decodes the canonical
four-byte stream prefix and envelope wire versions 1 and 2, exposes filterable
identity/correlation fields, and reports truncated, oversized, malformed,
unknown-version, and trailing data without reading past captured bytes.

Install the file into the personal Lua plugin directory printed by
`tshark -G folders` or Wireshark's **Help → About Wireshark → Folders** page.
Install `tools/graphx-extcap` into the personal extcap directory shown on the
same page and keep it executable. Restart Wireshark after either change.

Useful display filters include:

```text
graphx
graphx.version == 2
graphx.sequence == 42
graphx.message_id == 00112233445566778899aabbccddeeff
graphx.trace_id == 0123456789abcdef0123456789abcdef
graphx.attribute_count > 0
```

USER0 is a local/private-use link type and can conflict with another local
USER0 protocol. Install this dissector only in profiles where GraphX owns USER0.
Ethernet PCAPNG files from OVS or dumpcap remain ordinary Ethernet and do not
use this dissector.
