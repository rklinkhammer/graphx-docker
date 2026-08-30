# Standalone PCAPNG capture demo

This portable demo runs the local three-process TCP graph for ten messages and
records one PCAPNG file per node without changing application code.

```sh
cmake --preset dev
cmake --build --preset dev
examples/capture/run.sh
```

The script prints its timestamped output directory under `captures/`. Open
`generator.pcapng`, `transform.pcapng`, or `sink.pcapng` in Wireshark. Each
Enhanced Packet Block contains the exact GraphX `u32be + GXE envelope` frame.
The packet comment is JSON containing edge, direction, sequence, trace ID, and
type. The interface uses `LINKTYPE_USER0` (147), so Wireshark does not mistake
the application bytes for Ethernet or IP packets.

Override `GRAPHX_CAPTURE_DIR`, `GRAPHX_MAX_MESSAGES`, or
`GRAPHX_INTERVAL_MS` when needed. Capture files are truncated when a node starts
again with the same output path.
