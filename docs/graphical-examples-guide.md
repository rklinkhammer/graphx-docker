# Graphical examples guide

The portable pipeline and simulated SDR example expose the GraphX browser console. After a
`graphx example up NAME` reports readiness, open its printed URL and verify topology, node and edge
status, rates, latency, SLO state, runtime evidence, capture entries, and history.

Container graphs run with Docker Engine on Linux or OrbStack on macOS. The explicit
SDR laboratory and QEMU/OVS graphs also have the default platform console, with
packet evidence governed by their declared observation contract. Physical SDR
startup remains gated until an uplink ownership contract is implemented.

After resizing the browser or rotating a mobile display, use the topology's
**Fit view** control to bring all nodes back into view. The canvas does not
automatically refit an existing layout when its container changes size.

The QEMU TAP and OVS network labs are primarily command-line demonstrations. Use `graphx example status NAME --allow-privileged` for resource status; packet captures can be opened through
Wireshark extcap or exported with the GraphX CLI.

For commands and environment requirements, use [`demo-guide.md`](demo-guide.md) and
[`test-procedure.md`](test-procedure.md).
