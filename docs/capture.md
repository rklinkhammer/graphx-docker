# PCAPNG capture and Wireshark

GraphX can record canonical application frames independently of network packet
capture. Set the capture provider to `pcapng`; the existing generator,
transform, and sink code does not change.

## Capture format

Each node writes its own PCAPNG file. The file contains:

- one Section Header Block describing the GraphX representation;
- one Interface Description Block using `LINKTYPE_USER0` (147), nanosecond
  timestamp resolution, and a configurable snap length (16 MiB + 4 bytes by
  default, enough for the largest canonical frame);
- one Enhanced Packet Block for every observed send or receive callback;
- the exact canonical `u32` big-endian length prefix followed by the serialized
  `GXE` envelope as packet data;
- a standard UTF-8 packet comment containing JSON fields for edge, direction,
  envelope wire version, sequence, message ID, parent-message ID, trace ID, and
  type.

The timestamp is the system-clock time at which GraphX records the callback.
The telemetry event for the same record includes the capture filename, packet
index, and PCAPNG block byte offset. The browser joins v2 records by edge and
message ID. Version-1 records fall back to edge, trace ID, and sequence.

The packet bytes retain their original envelope wire version and can be decoded
according to [`protocol.md`](protocol.md). GraphX deliberately does not label
these records as Ethernet, IP, or TCP.
`LINKTYPE_USER0` is a private-use libpcap value. It must not be treated as a
globally registered interchange format or enabled in a Wireshark profile where
another protocol owns USER0. The Phase 9 Lua dissector binds explicitly to that
encapsulation without changing GraphX's application wire format.

Metadata uses the standard PCAPNG `opt_comment` option. GraphX does not invent a
Private Enterprise Number for a custom option or block.

GraphX also provides `EthernetPcapngCaptureSink` for actual frames obtained from
an L2 interface. It uses `LINKTYPE_ETHERNET` (1), requires at least the complete
14-byte Ethernet header, and never wraps an application envelope in a fabricated
Ethernet header. OVS SPAN interfaces and the network-lab capture scripts are the
normal source for these frames.

Primary format references:

- [IETF PCAPNG draft](https://datatracker.ietf.org/doc/draft-ietf-opsawg-pcapng/)
- [libpcap link-type registry source](https://github.com/the-tcpdump-group/libpcap/blob/master/pcap-common.c)
- [Wireshark extcap developer guide](https://www.wireshark.org/docs/wsdg_html_chunked/ChCaptureExtcap.html)
- [Wireshark Lua dissector example](https://www.wireshark.org/docs/wsdg_html_chunked/wslua_dissector_example.html)

## Standalone demo

```sh
cmake --preset dev
cmake --build --preset dev
examples/capture/run.sh
```

The command runs ten messages and prints the timestamped directory containing
`generator.pcapng`, `transform.pcapng`, and `sink.pcapng`. See
[`examples/capture/README.md`](../examples/capture/README.md).

For a manual local run, set:

```sh
export GRAPHX_CAPTURE_ENABLED=true
export GRAPHX_CAPTURE_PROVIDER=pcapng
export GRAPHX_CAPTURE_DIR="$PWD/captures/manual"
export GRAPHX_CAPTURE_MAX_FILE_BYTES=268435456
export GRAPHX_CAPTURE_MAX_PACKETS=1000000
```

Then run the nodes normally. Configuration can instead enable the same behavior:

```yaml
observability:
  capture:
    enabled: true
    provider: pcapng
    directory: captures
    snaplen: 16777220
    max_file_bytes: 268435456
    max_packets: 1000000
```

Files with the same node name are replaced at node startup only after the open
descriptor has been validated as a single-link regular file. The writer opens
nonblocking and with no-follow semantics, so symlinks, hard links, FIFOs,
sockets, and devices are rejected without blocking or changing their contents.
New files are created owner/group-readable and writable. An Enhanced Packet
Block is committed only when the complete block fits. Optional correlation
comments are bounded by PCAPNG's 16-bit option length; if metadata is too large,
the writer emits a small valid JSON truncation marker and still records the
exact canonical frame. Capture stops for that process, without stopping graph
traffic, when either configured limit is reached. Automatic rotation is
intentionally not performed: use a new directory or archive the stopped file
before restarting a node. This makes retention and deletion an explicit
operator decision while bounding disk consumption to one file per node.

The defaults are 256 MiB and 1,000,000 packets per node. `snaplen` is 256–
16,777,220 bytes, `max_file_bytes` is 64 KiB–4 GiB, and `max_packets` is
1–100,000,000. A snap length below 16,777,220 intentionally produces truncated
packet records; the original packet length remains in the Enhanced Packet Block
and the dissector reports the frame as truncated rather than inventing data.

Telemetry examines at most `GRAPHX_CAPTURE_CATALOG_MAX_ENTRIES` directory
entries per one-second catalog refresh and returns at most
`GRAPHX_CAPTURE_CATALOG_MAX_FILES` validated files, sorted by name. Defaults are
512 examined entries and 128 files; limits are 1–1,024 files and the configured
file limit–4,096 entries. `catalogTruncated: true` means more directory entries
or matching files exist than the bounded view can represent.
`catalogScannedEntries` reports the work performed. Direct authenticated
downloads validate the requested descriptor independently, so a valid file
omitted from a truncated catalog remains downloadable by its known name.

## Docker demo

The standard Compose topology has a shared named capture volume. Enable capture
for all three nodes and the telemetry service with:

```sh
GRAPHX_CAPTURE_ENABLED=true scripts/demo.sh start
```

Open the console and select an edge. Once a file exists, **Download PCAPNG**
downloads the source-side capture. The same listing is available at:

```sh
curl http://localhost:8080/api/captures
curl -o generator.pcapng http://localhost:8080/captures/generator.pcapng
```

`scripts/demo.sh stop` leaves the named volume intact. To deliberately delete
the captures together with the Compose resources, use `docker compose down -v`.

## Wireshark dissector

Copy `wireshark/graphx.lua` to the personal Lua plugin directory printed by
`tshark -G folders` or shown under **Help → About Wireshark → Folders**, then
restart Wireshark. The `graphx.*` display fields cover the stream length,
version, sequence, timestamp, v2 message/trace/parent identities, v1 legacy
trace, type, attribute count and values, and payload. The dissector validates
lengths before every read, caps attributes at 4,096, rejects unsupported
versions, mandatory all-zero v2 message/trace identities, duplicate attribute
keys, and trailing data, and adds expert diagnostics for malformed packets. An
all-zero optional parent identity remains the valid absent-parent encoding.
See [`../wireshark/README.md`](../wireshark/README.md) for filters.

## Wireshark extcap

`tools/graphx-extcap` implements the production Phase 9 extcap control surface. It lists a
GraphX application interface (DLT 147) and a standard Ethernet interface (DLT
1), presents capture-file and live-follow options, and writes the selected
PCAPNG stream to the FIFO supplied by Wireshark. It requires Python 3, opens
sources nonblocking, rejects symlink/non-regular inputs, checks the section
header and interface DLT, limits
each buffered block to 17 MiB, and accepts only the GraphX PCAPNG 1.0 profile:
indefinite section length, one initial interface, interface-zero Enhanced Packet
Blocks, and no later sections or interfaces. It verifies block lengths/trailers
and emits only complete blocks. A nonempty capture filter is rejected
explicitly; use a Wireshark display filter after capture.

Check it directly:

```sh
tools/graphx-extcap --extcap-interfaces
tools/graphx-extcap --extcap-interface graphx --extcap-dlts
tools/graphx-extcap --extcap-interface graphx-ethernet --extcap-dlts
tools/graphx-extcap --extcap-interface graphx --extcap-config
```

Copy or symlink the executable into the personal extcap directory reported by
your Wireshark installation, keep it executable, and restart Wireshark. Select
**GraphX framed envelopes** for application files or **GraphX Ethernet mirror
capture** for OVS/dumpcap files, choose the matching PCAPNG file, and start
capture. The follow option streams records appended while capture runs.

The adapter follows one node file. It does not merge or rotate files and does
not control the node lifecycle. Stop capture through Wireshark; termination and
FIFO closure are treated as normal cancellation.

## Security and retention

PCAPNG packet bytes include the complete GraphX envelope payload and attributes.
They are operational evidence, not sanitized telemetry, and may contain
application-sensitive data. Protect the capture directory, limit observation
download access with `GRAPHX_OBSERVATION_TOKEN`, export files only to approved
locations, and apply an external retention/deletion policy. Capture filenames
are fixed to bounded node identifiers. The collector opens each download once
with nonblocking/no-follow semantics, validates regular-file type, single-link
ownership, size, PCAPNG header, and DLT on that exact descriptor, then streams
the same descriptor. A writable shared volume therefore cannot use a symlink,
hard link, special file, or pathname replacement race to serve unchecked bytes.
Only the supported USER0 and Ethernet DLTs are downloadable.
Catalog scans and observation snapshots are bounded and cached for one second;
the catalog reports truncation instead of allocating or broadcasting every file
in a hostile directory.

## Application capture versus OVS capture

The PCAPNG writer records GraphX application framing and correlation metadata.
The OVS SPAN helpers record real link-layer packets. Live display uses `tcpdump`;
supplying an output filename uses `dumpcap` and writes PCAPNG with standard
Ethernet records. These are complementary artifacts: application
captures explain envelopes; OVS captures explain traffic on the emulated
network path. Automated cross-file packet matching remains a later hardening
step.

```sh
examples/mixed-network/scripts/capture.sh mac captures/mixed-mac.pcapng
examples/ipvlan-l2/scripts/capture.sh transform captures/ipvlan-transform.pcapng
```

Stop capture with `Ctrl-C`. On macOS the mixed-network helper runs `dumpcap`
inside the privileged OVS container; the host does not need native OVS.
