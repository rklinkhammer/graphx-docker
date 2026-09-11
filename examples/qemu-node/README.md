# Unified QEMU demonstrations

The canonical M8 OVS-backed Linux/Lima path is launched by `scripts/demo.sh` and documented in
[`tap/README.md`](tap/README.md). The external and container profiles remain
explicitly deprecated user-mode-networking compatibility paths.

This suite models the same three-node application across the canonical TAP path
and two compatibility profiles:

```text
host-origin -- raw TCP/UDP --> qemu-node -- raw TCP/UDP --> host-receiver
```

The guest exchanges ordinary network messages. It does not link GraphX and its
application traffic is not a GraphX envelope. GraphX supplies configuration,
orchestration, passive packet observation, bounded PCAPNG and packet history,
the browser console, and control of the origin traffic generator.

## Shared implementation

Both profiles use the same:

- x86_64 Buildroot guest and `qemu-network-node` application;
- `host/peer.py` origin, receiver, probe, and out-of-band control adapter;
- `tools/packet_observer.py` passive observer and packet-history service;
- GraphX telemetry server and browser GUI;
- TCP/UDP ports, deterministic payloads, history schema, and capture limits.

Only the QEMU deployment boundary changes.

| Profile | QEMU location | Platforms | Acceleration |
|---|---|---|---|
| [tap](tap/README.md) | Unprivileged Lima/Linux process on a GraphX-owned OVS TAP | Linux (including Lima on macOS) | TCG for the checked-in x86_64 guest |
| [external](external/README.md) | Host process | macOS and Linux | HVF on Intel macOS, KVM on Linux, or TCG |
| [container](container/README.md) | Docker service | Linux x86_64 | KVM or TCG |

## Build the shared guest

From the repository root:

```sh
examples/qemu-node/scripts/build.sh
```

The build uses Buildroot 2025.02.17 and writes ignored artifacts beneath
`examples/qemu-node/output/images`. It also runs Buildroot `legal-info`, writing
target and host package manifests, source archives, and collected license texts
under `examples/qemu-node/output/legal-info`. Optional organizational trust uses
the project-wide `GRAPHX_CA_CERT` and `GRAPHX_CERT_INSTALL_SCRIPT` inputs.

## Canonical lifecycle

The root TAP/OVS launcher supports:

```text
examples/qemu-node/scripts/demo.sh start
demo.sh verify
demo.sh status
demo.sh stop
```

On Linux the launcher runs locally. On Apple Silicon macOS it verifies and uses
the GraphX Lima VM automatically. `start` checks the QEMU identity, TAP and OVS
state, guest TCP/UDP readiness, VLAN isolation, and SPAN capture growth.
`pause` and `resume` control the VM through QMP. This canonical infrastructure
profile has no browser console; use the deprecated external
compatibility profile when GUI behavior itself is under test.

QMP proves VM liveness and acceleration but leaves the node `booting`. An
independent bounded TCP-and-UDP probe promotes the guest application to
`ready`; continuous monitoring refreshes QMP state and demotes lost or stale
readiness. A paused VM is reported separately from its unavailable guest and
down TCP/UDP probes, and the monitor remains active so a resumed VM can recover
without restarting the demo. The GUI Network view displays those VM, guest,
and protocol states on their distinct deployment nodes.

## Compatibility entry points

The older `scripts/run.sh` remains a bounded, host-only QEMU smoke test. The
external and container launchers are deprecated compatibility entry points.
Their `qemu-usernet` configurations are not selected by the root launcher.

See [the complete QEMU demo guide](../../docs/qemu-demos.md) for architecture,
capture/history, GUI, control, and operator verification details.
