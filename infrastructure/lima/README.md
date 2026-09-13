# macOS: Docker and Open vSwitch with Lima

GraphX uses OrbStack for unprivileged portable Compose workloads and the dedicated
`graphx` Lima VM for system OVS, owned veth/TAP interfaces, namespaces, nftables,
netem and network capture. S07–S12 have [privileged Lima acceptance evidence](../../design/graph-generation/p7-verification.md).
QEMU guest boot and scenario execution remain gated. Physical radio startup also
requires a separate uplink ownership contract; an external address is insufficient.

## Environment

The checked-in definition uses Apple Silicon Virtualization.framework, ARM64
Ubuntu, four CPUs, 8 GiB RAM and an 80 GiB disk. It mounts this repository at
`/workspace/graphx-docker` and installs rootful Docker, Compose, Buildx, system OVS,
Node.js 24 and Linux packet tools. Docker and other privileged sockets are never
forwarded to macOS. The host Docker context remains OrbStack.

Use `infrastructure/lima/start.sh` when explicitly setting up the environment. It
validates the fixed VM identity before creating or starting it. Runtime launchers
only reuse a running, identity-matched VM; they do not provision or replace one.

Read-only preflight:

```sh
limactl list graphx
limactl shell --workdir /workspace/graphx-docker graphx -- docker info
limactl shell --workdir /workspace/graphx-docker graphx -- docker compose version
limactl shell --workdir /workspace/graphx-docker graphx -- sudo ovs-vsctl --timeout=5 show
```

## Compile and execute

Prepare a guest-local source/build under `/var/lib/graphx`; never reuse the mounted
macOS CMake build directory. Build and verify the shared image release using the
[release process](../../docs/release-process.md), then compile with that release's
catalog. Static-route diagnostics also require the verified native installation.
See [execution](../../docs/execution.md) for compiled artifact and credential inputs.

For a separately authorized run, set these variables at the macOS prompt to
existing **guest paths**:

```sh
export GRAPHX_LIMA_GRAPHX_BIN=/var/lib/graphx/releases/native/bin/graphx
export GX_OUTPUT=/var/lib/graphx/runs/example/compiled
export GX_STATE=/var/lib/graphx/runs/example/state
export GRAPHX_IMAGE_RELEASE=/var/lib/graphx/releases/images
export GRAPHX_ALLOW_PRIVILEGED=1
scripts/network-lab.sh macvlan up
scripts/network-lab.sh macvlan status
scripts/network-lab.sh macvlan down
```

The same dispatcher supports `ipvlan-l2`, `ipvlan-l3` and `mixed-network`.
It forwards explicit artifact paths to the compiled runner inside Lima. `plan`
requires existing compiled artifacts but no privilege flag. On Linux, invoke the
wrapper as the authorized root runner. Static-route and network-observation cases
use an explicit guest shell; see the [example matrix](../../examples/README.md).

The guarded acceptance entry point is `tests/test_ovs_execution_live.py`, with
explicit `--allow-privileged --target lima` and guest-local release/evidence paths.
Legacy infrastructure probes are not substitutes for this compiled-runner acceptance.

## Storage and recovery

Builds, release candidates, ledgers, live capture rings and high-I/O evidence remain
on the guest disk under `/var/lib/graphx`. Only small verification summaries need
copying to the host. The standard application port forward maps guest port 8080 to
host loopback port 18080; other test ports remain guest-local.

Each graph owns a common ledger and lock. `down` validates identities before
stopping processes or removing infrastructure, and retains history, sealed captures
and logs. An interrupted operation uses the same compilation and state root for
recovery. Investigate ownership mismatches; do not rewrite identities or broadly
prune Docker/network resources.

`infrastructure/lima/stop.sh` stops the VM while preserving its disk. Deleting or
replacing the VM destroys retained guest evidence and is not part of graph cleanup.
