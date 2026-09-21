# macOS: Docker and Open vSwitch with Lima

GraphX uses OrbStack for unprivileged portable Compose workloads and the dedicated
`graphx` Lima VM for system OVS, owned veth/TAP interfaces, namespaces, nftables,
netem and network capture. S07–S12 have [privileged Lima acceptance evidence](../../design/graph-generation/p7-verification.md).
Verified QEMU guest boot and explicit scenario execution are supported. Physical radio startup also
requires a separate uplink ownership contract; an external address is insufficient.

## Environment

Setup, prerequisites, automatic lifecycle, storage and VM migration are documented
in the [top-level build README](../../README.md#macos-lima-environment).
This page covers Lima-specific diagnostics and privileged verification.

Read-only preflight:

```sh
limactl list graphx
limactl shell --workdir /workspace/graphx-docker graphx -- docker info
limactl shell --workdir /workspace/graphx-docker graphx -- docker compose version
limactl shell --workdir /workspace/graphx-docker graphx -- sudo ovs-vsctl --timeout=5 show
```

## Compile and execute

Use the [CMake preparation and execution targets](../../README.md#prepare-and-run-an-example)
from the host checkout. The common CLI stages sources and verified artifacts on
the guest; no separate guest build procedure is required.

The guarded acceptance entry point remains `tests/test_ovs_execution_live.py`,
with explicit `--allow-privileged --target lima` and guest-local evidence paths.

The VM configuration fingerprint covers `graphx.yaml` and `provision.sh`, using
relative filenames. Host launchers and acceptance scripts are not provisioned
inputs and do not invalidate a running VM when edited. Repository location, VM
name, architecture and VM type remain independently identity-checked. Changes to
the VM definition or provisioning script still fail closed.

## Storage and recovery

Follow [environment and recovery guidance](../../README.md#macos-lima-environment).
Ownership mismatches fail closed; preserve the error and inspect the instance
identity before attempting cleanup.
