# Portable simulated-SDR profile

## Prerequisites

Docker with Compose, OpenSSL, curl, and Python 3 are required. On Linux, use Docker Engine; on macOS, run OrbStack and select
its `orbstack` Docker context. Configure optional organizational build trust once
using `GRAPHX_CA_CERT` and `GRAPHX_CERT_INSTALL_SCRIPT` (absolute paths); the SDR and telemetry images use the
same global certificate/install-script inputs as the rest of GraphX.

## Run it

From the repository root:

```bash
examples/sdr-node/simulated/scripts/demo.sh start
examples/sdr-node/simulated/scripts/demo.sh status
examples/sdr-node/simulated/scripts/demo.sh verify
```

Open <http://127.0.0.1:8080>. The Network tab shows the SDR, bridge, processor,
sink, and three raw edges. Counts should update without refreshing. Capture and
History show Ethernet captures and packet-derived records separately from
GraphX message history. Exercise every tab transition before calling a GUI run
accepted.

The control token printed by `start` (and by `demo.sh token`) enables GUI
pause/resume/reset. Those actions target the processor controller and change the
SDR transmit state over mutual TLS. Direct device commands are also available:

```bash
examples/sdr-node/simulated/scripts/demo.sh control status
examples/sdr-node/simulated/scripts/demo.sh control tune 433920000
examples/sdr-node/simulated/scripts/demo.sh control stop
examples/sdr-node/simulated/scripts/demo.sh control start
```

Inspect evidence with Wireshark or TShark using the `sdr-node.pcapng` path shown
under `outputs/sdr-node/simulated`. Useful display filters are
`udp.port == 18400`, `tcp.port == 18401`, and `tcp.port == 18402`. TLS control
payloads are encrypted; packet metadata remains observable.

Stop and retain evidence:

```bash
examples/sdr-node/simulated/scripts/demo.sh stop
```

Add `--no-capture` or `--no-history` only to `start`. If startup fails, use
the bounded command output and retained run directory for diagnosis. Failed or
interrupted starts automatically remove the profile's Compose resources, and
`stop` is safe to repeat. Confirm the selected GUI port is free before retrying.
An explicit `GRAPHX_SDR_GUI_PORT` on a new `start` takes precedence over the
port retained in the preceding state file.
This profile represents the Ethernet switch with Docker's private bridge; it is
not evidence for OVS, SPAN, macvlan, or physical-link behavior.
