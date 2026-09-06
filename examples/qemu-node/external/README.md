# External-QEMU demo

This portable profile runs QEMU and the shared packet observer on the host
while the origin, receiver, telemetry service, and GUI run in Docker. Keeping
the observer beside the host-written PCAP avoids Docker Desktop bind-cache
latency; the container profile runs the same observer code in Docker. GraphX
describes QEMU as an external, host-executed VM.

## Prerequisites

- Docker with Compose
- Python 3 and OpenSSL
- `qemu-system-x86_64` 8.2 or newer
- shared guest artifacts built by `../scripts/build.sh`
- macOS or Linux with ports 18001, 18002, 19001, 9000/UDP, 9100, and 8080 available

## Run

From the repository root:

```sh
examples/qemu-node/external/scripts/demo.sh start --accel auto
examples/qemu-node/external/scripts/demo.sh status
examples/qemu-node/external/scripts/demo.sh verify
examples/qemu-node/external/scripts/demo.sh token
examples/qemu-node/external/scripts/demo.sh logs
examples/qemu-node/external/scripts/demo.sh stop
```

On Apple Silicon, the x86_64 guest uses TCG. HVF is accepted only for an Intel
Mac. Linux uses KVM when `/dev/kvm` is accessible or TCG otherwise. An explicit
`--accel kvm` or `--accel hvf` fails rather than silently falling back.

The origin reaches QEMU through `host.docker.internal:18001`. A separate private
TCP/UDP forward on port 18002 probes the same guest service without competing
with data-plane UDP associations. The guest reaches the receiver through the
conventional slirp gateway `10.0.2.2:19001`, which is
published by Docker only on host loopback. QMP is a private Unix socket beneath
the mode-0700 state directory. It supplies accelerator evidence and graceful
shutdown and is not mounted into telemetry. It does not establish application
readiness: a separate host monitor requires TCP and UDP guest echoes and
refreshes bounded evidence continuously. The evidence JSON is retained with
run artifacts.

On native Linux, `host.docker.internal` is pinned to the private QEMU demo
bridge gateway `172.30.12.1`; both the QEMU forwards and observer history API
bind to that same private address. This is intentionally not the Docker default
bridge gateway and is not exposed on a physical interface. macOS retains the
Docker Desktop hostname mapping and loopback-only host listeners.

Startup verifies all four edges independently. If startup verification fails,
the launcher tears down its containers and owned host processes while retaining
the diagnostic run directory. Run `stop` before retrying an interrupted run.

Output for each run is retained beneath `outputs/qemu-node/external/TIMESTAMP`.
The state file containing generated local credentials and the verified QEMU
PID is stored with mode 0600 beneath `examples/qemu-node/.state`.
Start verifies `output/images/manifest.json` and prints hashes for the kernel,
rootfs, and manifest so both profiles can be compared exactly.
If `GRAPHX_QEMU_GUI_PORT` is set for `start`, it is saved for all later commands.
