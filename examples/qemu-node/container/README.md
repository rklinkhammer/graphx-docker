# Linux containerized-QEMU demo

This profile runs QEMU, the x86_64 guest, origin, receiver, observer, telemetry,
and GUI as Docker Compose services. It is supported only on native Linux.

## Prerequisites

- Linux x86_64
- Docker Engine with Compose
- Python 3 and OpenSSL on the host
- shared guest artifacts built by `../scripts/build.sh`
- `/dev/kvm` readable and writable by the operator for KVM mode

## Run

```sh
examples/qemu-node/container/scripts/demo.sh start --accel auto
examples/qemu-node/container/scripts/demo.sh status
examples/qemu-node/container/scripts/demo.sh verify
examples/qemu-node/container/scripts/demo.sh token
examples/qemu-node/container/scripts/demo.sh logs
examples/qemu-node/container/scripts/demo.sh stop
```

Use `--accel kvm` for the Linux acceptance run. It fails clearly unless QMP
`query-kvm` proves both `present=true` and `enabled=true`. Use `--accel tcg`
for an unaccelerated smoke test. `auto` reports requested, selected, and actual
accelerators separately.

QMP proof leaves the node `booting`. The entrypoint promotes it to `ready` only
after the guest application answers both TCP and UDP, then refreshes that
evidence continuously. Repeated failure or stale evidence is degraded.

The default profile does not use `privileged`, the Docker socket, `NET_ADMIN`,
or `/dev/net/tun`. KVM mode adds only `/dev/kvm` and its host group. QEMU
user-mode networking forwards origin traffic to the guest. A bounded relay in
the QEMU container namespace forwards guest TCP and UDP to `host-receiver`, so
the same guest image and `10.0.2.2:19001` endpoint are used by both profiles.

Only the GUI is published to host loopback. Data ports remain private. QMP is
kept in the QEMU container's private `/run/graphx-qemu` tmpfs and is not shared
with telemetry or the packet observer. Bounded proof is retained as
`accelerator-evidence.json`.
Artifacts are retained beneath `outputs/qemu-node/container/TIMESTAMP`.
An optional `GRAPHX_QEMU_GUI_PORT` supplied to `start` is saved for later CLI
commands.

## Required manual Linux acceptance

The implementation environment may validate configuration, images, scripts,
and TCG design without a Linux KVM host. Release acceptance requires the
operator procedure in `docs/qemu-demos.md`, including generated QMP proof that
KVM is active. Merely seeing `/dev/kvm` in the container is not sufficient.
