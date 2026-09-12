# macOS: Docker and Open vSwitch with Lima

GraphX uses two Linux environments on Apple Silicon macOS, depending on the
workload:

| Workload | Runtime | Why |
|---|---|---|
| Portable Compose demo | OrbStack | It needs Docker and Compose, but not Linux host networking. |
| OVS network and QEMU/TAP labs | GraphX Lima VM | They need rootful Docker, system Open vSwitch, namespaces, veth/TAP devices, nftables, netem, QEMU, and packet capture. |

OrbStack does not provide the Linux host data plane required by the OVS labs.
The macOS launchers therefore dispatch those labs to the fixed Lima instance
named `graphx`; do not change the host Docker context to run them.

## Prerequisites

- Apple Silicon Mac with macOS 13 or newer and Virtualization.framework.
- Lima 2.2.0 or newer and Python 3.
- At least 4 CPU cores, 8 GiB RAM, 80 GiB free disk, and internet access for
  the pinned Ubuntu image and packages.

Install Lima with Homebrew if needed:

```sh
brew install lima
limactl --version
```

The separate portable demo requires OrbStack with its Docker context selected:

```sh
docker context use orbstack
docker info
docker compose version
scripts/demo.sh start
```

OrbStack is not required by the OVS launchers once the Lima VM has been
provisioned.

## Create and verify the Linux environment

Run from the repository root on macOS:

```sh
infrastructure/lima/start.sh
infrastructure/lima/verify.sh
```

`start.sh` validates the checked-in Lima definition, then creates or starts the
`graphx` ARM64 Ubuntu VM. It installs rootful Docker with Compose and Buildx,
system Open vSwitch, QEMU, nftables, packet tools, and Node.js 24. It refuses to
reuse a VM whose architecture, virtualization type, source checkout, or GraphX
Lima configuration differs from the current definition.

`verify.sh` checks Docker and OVS, builds GraphX in the VM, and exercises a
disposable system-OVS topology containing a namespace, veth pair, TAP device,
nftables policy, netem fault, and capture. Run it after creating the VM and
after changing the Lima definition or provisioning scripts.

## Run OVS network labs from macOS

Use the same commands at the macOS prompt that you would use on Linux. The
dispatcher detects macOS and runs the lab inside Lima:

```sh
scripts/network-lab.sh mixed-network plan
scripts/network-lab.sh mixed-network up
scripts/network-lab.sh mixed-network status
scripts/network-lab.sh mixed-network down
```

Replace `mixed-network` with `macvlan`, `ipvlan-l2`, or `ipvlan-l3` to run a
focused topology. `up` starts the lab containers with Docker inside the VM and
then creates the OVS data plane. Always use the matching `down` command when
finished; it verifies resource ownership before removing anything.

The guest GraphX executable is
`/var/lib/graphx/runtime/build/dev/graphx`. If it is missing or the dispatcher
reports failed prerequisites, rerun `infrastructure/lima/verify.sh`.

## Run the QEMU TAP/OVS lab

Build the guest image with Docker inside Lima so the entire privileged workflow
uses the documented VM environment:

```sh
limactl shell --workdir /workspace/graphx-docker graphx -- \
  examples/qemu-node/scripts/build.sh
```

Then control the lab from the macOS prompt. Its launcher dispatches to Lima:

```sh
examples/qemu-node/scripts/demo.sh start --accel auto
examples/qemu-node/scripts/demo.sh status
examples/qemu-node/scripts/demo.sh verify
examples/qemu-node/scripts/demo.sh stop
```

The VM is ARM64. The x86_64 QEMU guest normally uses TCG emulation; this setup
does not claim KVM acceleration.

## Work directly in the VM

For diagnosis or development, open a shell in the mounted checkout:

```sh
limactl shell --workdir /workspace/graphx-docker graphx
```

Useful checks are:

```sh
docker info
docker compose version
sudo systemctl status docker openvswitch-switch
sudo ovs-vsctl show
/var/lib/graphx/runtime/build/dev/graphx inspect graphx.yaml
```

Docker in this VM is a rootful system service. The Lima login user belongs to
the `docker` group so checked-in scripts can call Docker without `sudo`; that
group is root-equivalent inside this dedicated development VM. Docker, OVS,
and QMP sockets are not forwarded to macOS.

## Files, ports, and evidence

The repository is the only writable host mount and appears in the VM at
`/workspace/graphx-docker`. Linux build state and privileged runtime data remain
on the VM disk:

```text
/var/lib/docker
/var/lib/openvswitch
/var/lib/graphx/qemu
/var/lib/graphx/captures
/var/lib/graphx/runs
/var/lib/graphx/runtime/build
/var/lib/graphx/runtime/evidence
/var/log/graphx
```

The only application port forwarded to macOS is guest loopback port 8080 at
`127.0.0.1:18080`. Lima's own SSH transport remains implementation-managed.

Verification evidence is retained under
`/var/lib/graphx/runtime/evidence`, with at most ten runs and 256 MiB. The VM
build directory is separate from macOS CMake output, so the two platforms do
not share incompatible build caches.

## Stop, restart, and troubleshoot

Stopping preserves the VM disk and mounted source checkout:

```sh
infrastructure/lima/stop.sh
infrastructure/lima/start.sh
```

Inspect a failed environment with:

```sh
limactl list graphx
limactl shell graphx -- systemctl status docker openvswitch-switch
limactl shell graphx -- sudo cat /var/lib/graphx/runtime/provisioned
limactl shell graphx -- sudo ls -1 /var/lib/graphx/runtime/evidence
limactl shell graphx -- sudo tail -n 200 /var/log/cloud-init-output.log
```

If an image build reports a registry timeout, test both the guest network and
the guest Docker engine:

```sh
limactl shell graphx -- curl -fsSI --connect-timeout 10 https://registry-1.docker.io/v2/
limactl shell graphx -- docker pull hello-world:linux
```

An HTTP `401 Unauthorized` response from the first command is the expected
anonymous registry challenge and proves the HTTPS path is working. If either
command times out, retry after the network path recovers; restarting the OVS
lab is unnecessary. Then rerun the failed build command. GraphX Dockerfiles use
the Docker/BuildKit frontend bundled in the provisioned engine, so builds do
not need a separate `docker/dockerfile` frontend image download.

Lifecycle commands have deadlines and fail closed. If a lab reports an
ownership or identity mismatch, inspect the named resource and retained
evidence before changing it manually. The scripts do not adopt, overwrite, or
delete an object they cannot prove they own.

To replace the VM definition, first confirm that the fixed name refers to this
checkout:

```sh
limactl list graphx --format '{{.Name}} {{.Status}} {{.Arch}} {{.VMType}} {{index .Param "repo"}}'
limactl stop graphx
limactl delete graphx
```

`limactl delete graphx` permanently removes the VM disk, including Docker data,
QEMU disks, and retained evidence. It does not delete the mounted repository.
The GraphX lifecycle scripts never delete the VM.
