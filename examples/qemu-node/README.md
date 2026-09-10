# Unified QEMU demonstrations

The primary OVS-backed Linux/Lima path is documented in
[`tap/README.md`](tap/README.md). The external and container profiles remain
user-mode-networking compatibility paths.

This suite models the same three-node application twice:

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

## Common user interface

Each profile supports:

```text
demo.sh start [--accel auto|kvm|tcg|hvf] [--no-capture] [--no-history]
demo.sh verify
demo.sh status
demo.sh logs
demo.sh token
demo.sh stop
```

Set `GRAPHX_QEMU_GUI_PORT` for `start` to choose another console port. The
validated value is saved in private state, so later commands do not depend on
the invoking shell environment.

Capture and packet history are enabled by default and bounded to 64 MiB,
100,000 capture packets, 50,000 history records, and one day unless overridden.
The source PCAP is also guarded at 64 MiB; reaching that safety limit stops QEMU
and leaves a clear diagnostic rather than allowing unbounded host storage.

The default console is <http://127.0.0.1:8080/>. Its Application view shows the
shared logical graph. Network view shows whether QEMU is host-managed or nested
inside a Docker service. History displays packet metadata separately from
GraphX message history. Pause/resume affects only `host-origin`; it does not
pause the VM.

QMP proves VM liveness and acceleration but leaves the node `booting`. An
independent bounded TCP-and-UDP probe promotes the guest application to
`ready`; continuous monitoring refreshes QMP state and demotes lost or stale
readiness. A paused VM is reported separately from its unavailable guest and
down TCP/UDP probes, and the monitor remains active so a resumed VM can recover
without restarting the demo. The GUI Network view displays those VM, guest,
and protocol states on their distinct deployment nodes.

## Compatibility entry points

The older `scripts/run.sh` remains a bounded, host-only QEMU smoke test. New
interactive use should select one of the two profile `demo.sh` scripts. The
root `graphx.yaml` remains a topology-only compatibility model and now marks
all four edges as external data-plane traffic with `framing: none`.

See [the complete QEMU demo guide](../../docs/qemu-demos.md) for architecture,
capture/history, GUI, control, and operator verification details.
