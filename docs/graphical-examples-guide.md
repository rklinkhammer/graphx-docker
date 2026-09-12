# Graphical examples guide

The portable pipeline and simulated SDR example expose the GraphX browser console. After a
launcher reports readiness, open its printed URL and verify topology, node and edge
status, rates, latency, SLO state, runtime evidence, capture entries, and history.

Both run with Docker Engine on Linux or OrbStack on macOS. The external SDR
example provides command-line traffic and control verification only; it does not
start telemetry, history, a capture observer, or a browser console.

After resizing the browser or rotating a mobile display, use the topology's
**Fit view** control to bring all nodes back into view. The canvas does not
automatically refit an existing layout when its container changes size.

The QEMU TAP and OVS network labs are primarily command-line demonstrations. Their
launchers print status and artifact locations; packet captures can be opened through
Wireshark extcap or exported with the GraphX CLI.

For commands and environment requirements, use [`demo-guide.md`](demo-guide.md) and
[`test-procedure.md`](test-procedure.md).
