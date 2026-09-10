# GraphX Lima execution environment (Migration M1)

This directory defines the optional Linux execution environment used for
privileged GraphX development on an Apple Silicon Mac. Lima creates one ARM64
Ubuntu 24.04 VM named `graphx`; the VM, rather than a macOS container or Docker
Desktop, owns rootful Docker, Open vSwitch, Linux namespaces, veth/TAP devices,
nftables, netem, QEMU, and packet-capture tools.

M1 provides and verifies those prerequisites. It does **not** implement the
configuration-v2 OVS-only backend, attach GraphX application containers with
veth, change current MACVLAN/IPVLAN behavior, or replace QEMU user-mode (slirp)
networking with TAP. Those are later migration phases.

## Prerequisites and boundary

- Apple Silicon Mac with macOS 13 or newer and Virtualization.framework.
- Lima 2.2.0 or newer (`brew install lima`) and Python 3.
- At least 4 CPU cores, 8 GiB RAM, 80 GiB free disk, and internet access for the
  pinned Ubuntu image and Ubuntu packages.

The repository is the only writable host mount and appears at
`/workspace/graphx-docker` through virtiofs. Docker data, OVS databases, QEMU
disks, active captures, run ownership, logs, package evidence, and verification
evidence remain on the VM filesystem:

```text
/var/lib/docker
/var/lib/openvswitch
/var/lib/graphx/{qemu,captures,runs,m1/evidence}
/var/log/graphx
```

No Docker, OVS, QMP, or other privileged socket is forwarded to macOS. The only
application forwarding rule is guest loopback port 8080 to macOS loopback port
18080; an explicit match-any deny rule suppresses Lima's general loopback fallback. Lima's
own SSH transport remains implementation-managed.

## Lifecycle

Run these commands from the repository on macOS:

```bash
infrastructure/lima/start.sh
infrastructure/lima/verify.sh
infrastructure/lima/stop.sh
```

`start.sh` validates the template and creates or starts only the fixed `graphx`
instance. It refuses an existing instance whose architecture, virtualization
type, source path, or configuration digest differs. Provisioning is idempotent,
bounded, records installed package versions, installs Docker Compose and
Buildx for the repository's BuildKit Dockerfiles, and leaves Docker and OVS as
systemd-managed rootful services. The Lima login user is added to the `docker`
group because the checked-in build and example scripts invoke Docker directly;
that group is root-equivalent inside this dedicated development VM. On initial
creation, `start.sh` performs one bounded VM restart if needed to replace Lima's
pre-provisioning SSH session and activate the new supplementary group.

`verify.sh` rejects occupied fixed names, then creates a disposable
`gx-m1-*` OVS/namespace/veth/TAP topology. It verifies system-datapath packet
exchange, nftables, netem, capture, Docker, the GraphX quick profile, and
projection consistency. Each run exclusively owns `/run/graphx-m1`; any stale
or foreign state is rejected without alteration. Resource creation records the
exact OVS UUID, network-namespace inode, and internal/veth/TAP kernel interface
indexes inside signal-deferred critical sections. Cleanup validates those
identities and the complete link inventory inside its namespace, treats a
mismatch or leftover as a verification failure, compares complete before/after
guest snapshots, and preserves foreign replacements for inspection.
Evidence rotation runs on success and failure, retaining at most ten run
directories and 256 MiB under `/var/lib/graphx/m1/evidence`.
The guest quick build uses `/var/lib/graphx/m1/build`, so macOS and Linux CMake
caches never collide on the shared source mount.

`stop.sh` stops only the matching instance. Source and VM-local evidence remain.
It is safe to repeat start and stop.

## Inspect and troubleshoot

```bash
limactl list graphx
limactl shell graphx -- systemctl status docker openvswitch-switch
limactl shell graphx -- sudo cat /var/lib/graphx/m1/provisioned
limactl shell graphx -- sudo ls -1 /var/lib/graphx/m1/evidence
limactl shell graphx -- sudo tail -n 200 /var/log/cloud-init-output.log
```

An image/package download, service wait, test, packet operation, and lifecycle
command has a deadline. A timeout is a failure, not permission to adopt or
delete unknown state. Resolve occupied `gx-m1-*` objects manually only after
inspecting their recorded kernel identity, OVS UUID/external IDs, aliases, or
Docker label. A retained `/run/graphx-m1` is deliberately not repaired or
overwritten automatically; inspect it together with the named resources before
performing a deliberate recovery.

TAP creation requires `/dev/net/tun`; system OVS requires the guest kernel's
Open vSwitch facilities. Packet capture runs as guest root and writes only to
the bounded VM-local evidence directory. On Apple Silicon, QEMU's available
accelerators are recorded, but x86_64 and PowerPC guests are expected to use TCG
emulation. M1 does not require or claim KVM.

## Deliberate reset or removal

Stopping is the normal operation. To replace a stale definition or perform a
clean recreation, first confirm the exact target and source path:

```bash
limactl list graphx --format '{{.Name}} {{.Status}} {{.Arch}} {{.VMType}} {{index .Param "repo"}}'
limactl stop graphx
limactl delete graphx
```

`limactl delete graphx` permanently removes the VM disk, including retained
evidence, Docker data, and QEMU disks. It does not delete the mounted source
checkout. The lifecycle scripts deliberately never delete an instance.
