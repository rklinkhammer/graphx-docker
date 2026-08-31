# PCAPNG capture and Wireshark

GraphX can record canonical application frames independently of network packet
capture. Set the capture provider to `pcapng`; the existing generator,
transform, and sink code does not change.

## Capture format

Each node writes its own PCAPNG file. The file contains:

- one Section Header Block describing the GraphX representation;
- one Interface Description Block using `LINKTYPE_USER0` (147), nanosecond
  timestamp resolution, and a 16 MiB + 4-byte snap length;
- one Enhanced Packet Block for every observed send or receive callback;
- the exact canonical `u32` big-endian length prefix followed by the serialized
  `GXE` envelope as packet data;
- a standard UTF-8 packet comment containing JSON fields for edge, direction,
  envelope sequence, trace ID, and type.

The timestamp is the system-clock time at which GraphX records the callback.
The telemetry event for the same record includes the capture filename, packet
index, and PCAPNG block byte offset. The browser joins it to recent messages by
edge and trace ID.

GraphX deliberately does not label these records as Ethernet, IP, or TCP.
`LINKTYPE_USER0` is a private-use libpcap value, so this representation is meant
for local educational captures. It must not be treated as a stable interchange
format for unrelated products. A future registered link type or Wireshark
dissector can replace USER0 without changing the `CaptureSink` boundary.

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
```

Then run the nodes normally. Configuration can instead enable the same behavior:

```yaml
observability:
  capture: { enabled: true, provider: pcapng, directory: captures }
```

Files with the same node name are truncated at node startup. Capture is not
rotated and can grow without a configured limit.

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

## Wireshark extcap

`tools/graphx-extcap` implements the initial extcap control surface. It lists a
GraphX application interface (DLT 147) and a standard Ethernet interface (DLT
1), presents capture-file and live-follow options, and writes the selected
PCAPNG stream to the FIFO supplied by Wireshark.

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

This first extcap adapter follows one node file. It does not merge files, rotate
captures, add a native GraphX dissector, or control the node lifecycle.

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
