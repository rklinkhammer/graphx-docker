# macOS: Docker and Open vSwitch with Lima

GraphX uses OrbStack for unprivileged portable Compose workloads and the dedicated
`graphx` Lima VM for system OVS, owned veth/TAP interfaces, namespaces, nftables,
netem and network capture. S07–S12 have [privileged Lima acceptance evidence](../../design/graph-generation/p7-verification.md).
Verified QEMU guest boot and explicit scenario execution are supported. Physical radio startup also
requires a separate uplink ownership contract; an external address is insufficient.

## Environment

The checked-in definition uses Apple Silicon Virtualization.framework, ARM64
Ubuntu, four CPUs, 8 GiB RAM and an 80 GiB disk. It mounts this repository at
`/workspace/graphx-docker` and installs rootful Docker, Compose, Buildx, system OVS,
Node.js 24 and Linux packet tools. Docker and other privileged sockets are never
forwarded to macOS. The host Docker context remains OrbStack.

`graphx example prepare` and `graphx example up` create or start the dedicated
VM automatically, using the checkout selected by the command. Install Lima first;
the first preparation provisions Linux and builds the required artifacts.
`example prepare` and `example down` stop the VM after success when no containers,
application processes or laboratory networks remain. Its disk and prepared images
are retained for the next launch. A failed operation or uncertain idle check leaves
the VM running for inspection and recovery.

`graphx env up` and `graphx env down` remain manual overrides. Use `env up` to inspect
retained guest state after an automatic stop. Status, logs and browser commands do
not start a stopped VM. The example CLI serializes Lima operations across checkouts;
complete a following-log command before another lifecycle command. Direct guest
commands and low-level laboratory scripts require manual environment management;
do not run them concurrently with automatic demo shutdown.

VM identity includes the absolute host checkout path. A VM belonging to a different
checkout is refused; it is never replaced automatically. The guest mount path
`/workspace/graphx-docker` does not require that directory name on the host.

Read-only preflight:

```sh
limactl list graphx
limactl shell --workdir /workspace/graphx-docker graphx -- docker info
limactl shell --workdir /workspace/graphx-docker graphx -- docker compose version
limactl shell --workdir /workspace/graphx-docker graphx -- sudo ovs-vsctl --timeout=5 show
```

## Compile and execute

The [example CLI](../../docs/user-guide.md#cli-reference) transfers a source snapshot, builds
the Linux CLI and prepares verified artifacts on the guest disk. From the macOS
repository root:

```sh
graphx example plan sample-pipeline/ovs
graphx example up sample-pipeline/ovs --allow-privileged --control generator:pause,resume --control collector:reset
graphx example open sample-pipeline/ovs --allow-privileged
graphx example status sample-pipeline/ovs --allow-privileged
graphx example down sample-pipeline/ovs --allow-privileged
```

Interactive startup opens an authenticated host browser. The console URL uses
the host loopback forward, normally port 18080.
Use `--images DIR` and `--release DIR` to reuse existing verified **guest-local**
artifacts. Runtime state and build output remain under `/var/lib/graphx/examples`.
The same interface supports network profiles, route diagnostics, laboratory SDR
and QEMU; see the [quick start](../../examples/quick-start.md). Source changes
require `--restart`. VM identity mismatches fail closed before copying or running.

The guarded acceptance entry point remains `tests/test_ovs_execution_live.py`,
with explicit `--allow-privileged --target lima` and guest-local evidence paths.

The VM configuration fingerprint covers `graphx.yaml` and `provision.sh`, using
relative filenames. Host launchers and acceptance scripts are not provisioned
inputs and do not invalidate a running VM when edited. Repository location, VM
name, architecture and VM type remain independently identity-checked. Changes to
the VM definition or provisioning script still fail closed.

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

`graphx env down` stops the VM while preserving its disk. Deleting or
replacing the VM destroys retained guest evidence and is not part of graph cleanup.
