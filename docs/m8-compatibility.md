# M8 compatibility closure

M8 makes OVS the only GraphX network-infrastructure backend. `macvlan` and
`ipvlan-l2`/`ipvlan-l3` remain configuration semantics that control address and
flow behavior; they no longer select Docker network drivers. GraphX connects
containers with veth pairs and QEMU guests with TAP devices. Compose provides
management connectivity only.

## Version-1 support window

Version-1 files remain readable by `validate`, `inspect`, `project`, and
`config migrate` through the next major GraphX release. All `infra` lifecycle
operations reject version 1 before creating a state directory or changing the
host. Curated inputs live under `examples/compatibility/v1`; they are not active
examples.

```sh
graphx config migrate old.yaml --output graphx-v2.yaml
graphx validate graphx-v2.yaml
graphx inspect graphx-v2.yaml
graphx infra create graphx-v2.yaml --dry-run
```

Migration is deterministic and never overwrites an existing output. Keep the
original file in source control until the version-2 topology has passed review
and platform testing. Rollback means redeploying the previous GraphX release
with that saved configuration; the M8 binary deliberately cannot restore the
retired Docker data plane.

Imperative `infra fault` is also retired. Declare bounded `network.faults` in
version 2 so ownership, duration, recovery, and drift checks use one lifecycle.

## Runtime boundary

On macOS, the privileged environment is an ARM64 Linux Lima VM, not a macOS
container. Docker, system OVS, veth/TAP, QEMU, capture data, and ownership state
remain inside that VM. Docker Desktop simulation and the containerized
userspace-OVS image have been removed.

The default QEMU demonstration is `examples/qemu-node/scripts/demo.sh`, backed
by TAP and OVS. The external and container QEMU user-network profiles are
temporarily retained, explicitly deprecated compatibility demonstrations.

## Release evidence matrix

| Platform | Required evidence |
| --- | --- |
| macOS Apple Silicon + Lima ARM64 | portable suite, Lima verification, privileged M3–M8 suite |
| native Linux ARM64 | portable and privileged M3–M8 suite |
| native Linux x86_64 + TCG | portable, privileged M3–M8, QEMU TAP/TCG |
| native Linux x86_64 + KVM | portable, privileged M3–M8, QEMU TAP/KVM proof |

Results are reported per platform. Evidence from Lima or TCG must not be used to
claim an unexecuted native-Linux or KVM row.
