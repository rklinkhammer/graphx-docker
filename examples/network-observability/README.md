# Declarative network observation and faults (M7)

This Linux/Lima example makes an OVS SPAN capture and a timed netem fault part
of the same identity-safe GraphX infrastructure lifecycle as the bridge, TAP,
and mirror. It is intentionally a network primitive example: attach a QEMU
process to `gxm7tap0`, or generate Ethernet traffic on the bridge, to populate
the capture.

```sh
sudo ./build/dev/graphx infra create examples/network-observability/graphx.yaml
sudo ./build/dev/graphx infra status examples/network-observability/graphx.yaml
sudo ./build/dev/graphx infra capture export \
  examples/network-observability/graphx.yaml --capture ethernet-span \
  --output /var/tmp/m7-ethernet-span.pcapng
sudo ./build/dev/graphx infra destroy examples/network-observability/graphx.yaml
```

Capture uses `dumpcap` PCAPNG ring files. File size, file count, rotation, and
retention-window settings are all bounded. Each run receives a root-owned,
mode-0700 private session directory beneath `/var/lib/graphx/captures`; its
device, inode, ownership, and mode are fail-closed identities. Destroy stops
only the recorded process, removes all file write bits, and makes the retained
session read-only. Export uses
the verified directory identity and opens a root-owned, non-writable,
single-link regular file,
requires a complete final PCAPNG block, takes a bounded snapshot, and refuses an
existing or symlink destination.

The fault is applied to the declared attachment's owned host interface and is
automatically cleared by a recorded timer. Its boot-bound monotonic deadline
lets status distinguish genuine expiry from premature timer/qdisc loss. Destroy
verifies interface, qdisc, timer, deadline, and capture identities before
changing any M7 resource.

Ethernet PCAPNG is a separate trust domain from GraphX application capture,
which continues to use LINKTYPE_USER0 and the GraphX dissector.
