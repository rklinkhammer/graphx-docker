# QEMU demonstrations: user and operator guide

## Purpose

GraphX provides two views of one x86_64 QEMU application. The portable profile
treats QEMU as an external host node. The Linux profile runs QEMU in Docker.
The guest and all application payloads are identical.

```text
host-origin -- raw TCP and UDP --> qemu-node
qemu-node   -- raw TCP and UDP --> host-receiver
```

GraphX observes packets passively. Raw edges use `data_plane: external` and
`framing: none`, are shown in the GUI, and cannot be created through
`TransportFactory`.

The portable profile runs the shared observer beside host QEMU so live PCAP
tailing does not depend on Docker Desktop bind-mount cache timing. Telemetry
and the GUI remain containerized. On native Linux the observer binds its
history API and QEMU's TCP/UDP forwards only to the private `172.30.12.1` demo
bridge gateway. The containers' `host.docker.internal` name is pinned to that
same gateway instead of Docker's unrelated default-bridge gateway. On macOS,
Docker Desktop supplies the hostname and the host services bind to loopback.
The Linux QEMU profile runs the same observer code as a container.

## Build once

```sh
cd ~/workspace/graphx-docker
examples/qemu-node/scripts/build.sh
```

Expected artifacts are `examples/qemu-node/output/images/bzImage`,
`rootfs.cpio.gz`, and `manifest.json`. Reproducible Buildroot mode and a stable
`SOURCE_DATE_EPOCH` are enabled. Each start verifies the manifest before it
creates resources, retains a copy with the run, and prints the kernel, rootfs,
and manifest SHA-256 values.

## Demo 1: external QEMU

```sh
examples/qemu-node/external/scripts/demo.sh start --accel auto
```

Open <http://127.0.0.1:8080/>. Retrieve the mutation token with:

```sh
examples/qemu-node/external/scripts/demo.sh token
```

Paste it into the Control token field. Pause and Resume stop and restart only
the origin generator. Reset clears collector counters; it does not erase the
retained packet-history database or pretend to reset the guest.

Inspect and stop:

```sh
examples/qemu-node/external/scripts/demo.sh verify
examples/qemu-node/external/scripts/demo.sh status
examples/qemu-node/external/scripts/demo.sh logs
examples/qemu-node/external/scripts/demo.sh stop
```

`start` checks each of the four TCP/UDP edges independently, plus capture and
history. A failed startup check automatically removes its containers and owned
QEMU/observer processes, but preserves the run directory for diagnosis. It is
still safe to run `stop` before retrying.

To use another console port, set it for `start`; the validated value is retained
in private run state for every later command:

```sh
GRAPHX_QEMU_GUI_PORT=18080 examples/qemu-node/external/scripts/demo.sh start --accel auto
examples/qemu-node/external/scripts/demo.sh status
```

## Demo 2: QEMU in Docker on Linux

```sh
examples/qemu-node/container/scripts/demo.sh start --accel kvm
```

The browser and controls operate exactly as in the external profile. Network
view differs intentionally: it identifies a Docker-managed QEMU runtime with a
nested guest rather than a host-managed VM.

For an unaccelerated smoke test:

```sh
examples/qemu-node/container/scripts/demo.sh start --accel tcg
```

Stop before changing accelerators:

```sh
examples/qemu-node/container/scripts/demo.sh stop
```

## GUI behavior

- **Application:** three logical nodes and four raw TCP/UDP edges.
- **Network:** Docker and QEMU user-network boundaries for the active profile.
- **History:** bounded packet metadata from a separate SQLite store.
- **Capture:** Ethernet PCAPNG downloads readable by Wireshark/tshark.

Counters arrive over WebSocket and must advance without refreshing. Returning
from History to Application or Network must not blank the page. QEMU is shown
as not application-controllable in this phase.

The QEMU card reports requested, launcher-selected, and runtime-proven
accelerators separately. The evidence source is QMP `query-status` plus
`query-kvm`. QMP proves VM liveness and acceleration, not guest-application
readiness. A separate bounded monitor requires successful TCP and UDP echoes
before `ready`; repeated failure or evidence older than ten seconds becomes
`degraded`. The network view renders the runtime boundary, virtual machine,
and guest application as separate nodes: host process → VM → guest application
for the external profile, and Docker container → VM → guest application for
the Linux profile. Runtime states are `not-started`, `booting`, `ready`,
`degraded`, `stopped`, and `unavailable`.

The monitor refreshes QMP status on every probe cycle. A QMP pause therefore
shows the VM as `paused`, marks the guest application unavailable, and marks
both TCP and UDP down without terminating the monitor. After QMP resume, the
monitor requires fresh successful TCP and UDP probes before the guest returns
to `ready`. These layer-specific states are shown in both the topology API and
the GUI Network view; they do not imply that the guest is GraphX-controlled.

## Capture and history

Defaults are:

| Resource | Default bound |
|---|---:|
| PCAPNG bytes | 64 MiB |
| PCAPNG packets | 100,000 |
| Source PCAP bytes | 64 MiB; QEMU stops at the bound |
| Packet-history records | 50,000 |
| Packet-history age | 1 day |
| Packet-history database | 64 MiB |
| Payload preview | 64 bytes |

Each run has a private timestamped output directory. The observer tolerates an
incomplete final PCAP record while QEMU is writing. It rejects impossible
captured/original/snaplen lengths and timestamps, skips a bounded malformed
record, and continues with the next valid record. History includes original
length, captured length, and a truncation flag. Downloads are exposed only
when every PCAPNG block in the descriptor snapshot is complete and consistent.

Use `--no-capture` to disable cataloged PCAPNG output and `--no-history` to
disable durable GraphX and packet history. Live telemetry remains active. The transient QEMU source capture may
still exist while the demo runs because it is the passive live-observation
input; it remains guarded by the source byte limit.

## Manual Linux acceptance procedure

Run on a native Linux x86_64 host from a clean stopped state:

```sh
cd ~/workspace/graphx-docker
examples/qemu-node/scripts/build.sh
examples/qemu-node/container/scripts/demo.sh start --accel tcg
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh status
examples/qemu-node/container/scripts/demo.sh stop

examples/qemu-node/container/scripts/demo.sh start --accel kvm
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh status
```

Confirm the containers are unprivileged and QEMU has only the intended device:

```sh
docker inspect graphx-qemu-container-qemu-node-1 \
  --format '{{.HostConfig.Privileged}} {{json .HostConfig.Devices}} {{json .HostConfig.CapAdd}}'
```

Expected: `false`, `/dev/kvm` only, and no added capabilities. The launcher
fails closed unless QMP reports both `present=true` and `enabled=true`. Inspect
the retained proof without accessing the private QMP socket:

```sh
run_dir=$(sed -n 's/^GRAPHX_QEMU_RUN_DIR=//p' examples/qemu-node/.state/container.env)
python3 -m json.tool "$run_dir/accelerator-evidence.json"
```

For KVM require `actualAccelerator: "kvm"`, `state: "ready"`, and both KVM
booleans true. For TCG require `actualAccelerator: "tcg"`, a running QMP
status, and KVM not enabled. Confirm both raw protocols in the capture:

```sh
tshark -r "$run_dir/qemu-node.pcapng" -Y 'tcp.port == 18001 || udp.port == 18001 || tcp.port == 19001 || udp.port == 19001'
```

After the explicit runs, exercise automatic selection with `start --accel
auto`, record all three accelerator fields, and stop it. Verify denial without
changing host device permissions by omitting the KVM device from a one-shot
runtime container; this must exit nonzero with the inaccessible-KVM message:

```sh
docker run --rm -e GRAPHX_QEMU_ACCEL=kvm \
  -v "$PWD/examples/qemu-node/output/images:/artifacts:ro" \
  graphx-qemu-runtime:latest
```

In the GUI, verify counters advance without refresh, inspect all four edges,
open History, return to Network, download the Ethernet capture, then test
Pause, Resume, and Reset with the token.

Finally:

```sh
examples/qemu-node/container/scripts/demo.sh stop
docker ps --filter name=graphx-qemu-container
```

No matching running container should remain. Retained run artifacts are
intentional. Record Linux distribution, kernel, CPU, Docker/Compose, QEMU,
`/dev/kvm` permissions, accelerator evidence, commands, and results in the
Phase 12 verification report.

Use this result template:

| Gate | Evidence | Result |
|---|---|---|
| Host/tool versions | distribution, kernel, CPU, Docker, Compose | Pass/Fail |
| Explicit TCG | evidence JSON and four advancing edges | Pass/Fail |
| Explicit KVM | evidence JSON with KVM present+enabled | Pass/Fail |
| Automatic selection | requested/selected/actual values | Pass/Fail |
| KVM denial | nonzero exit and bounded diagnostic | Pass/Fail |
| Capture/history | TShark summary and sample history record | Pass/Fail |
| GUI/control | all tabs; live counters; pause/resume/reset | Pass/Fail |
| Restart/signals | telemetry and QEMU restart/stop behavior | Pass/Fail |
| Least privilege | live device/group/capability inspection | Pass/Fail |
| Cleanup | repeated stop and empty process/container checks | Pass/Fail |

## Troubleshooting

- **KVM requested but inaccessible:** enable virtualization, load `kvm` and the
  CPU-specific module, and grant the operator access to `/dev/kvm`.
- **Port already allocated:** stop the standard demo or set
  `GRAPHX_QEMU_GUI_PORT` for `start`; it is saved for later commands. Ports 18001
  and 19001 must also be free in external mode.
- **Guest not healthy:** inspect QEMU/guest logs and confirm both guest artifacts
  match `output/images/manifest.json`; startup reports the exact mismatch.
- **No live counters:** inspect the packet observer, confirm the PCAP is growing,
  and verify the shared telemetry secret is identical in the services.
- **History unavailable:** check the packet-observer health and the timestamped
  run directory permissions.

TAP/bridge networking, multicast or broadcast through the guest, physical SDR
integration, and direct guest application control are deferred.
