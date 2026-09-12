# QEMU node

The supported QEMU example is the `tap` profile. It connects an x86_64 guest to a
system Open vSwitch bridge, captures mirrored Ethernet traffic, records packet
history, and collects QMP runtime evidence.

```sh
examples/qemu-node/scripts/build.sh
examples/qemu-node/scripts/demo.sh start
examples/qemu-node/scripts/demo.sh status
examples/qemu-node/scripts/demo.sh verify
examples/qemu-node/scripts/demo.sh stop
```

Run it on native Linux or through the dispatcher into the GraphX Lima guest. Runtime
artifacts stay under `/var/lib/graphx/qemu`. GraphX owns the only Ethernet capture
process and its root-only PCAPNG ring under `/var/lib/graphx/captures`. A bounded,
read-only snapshot is handed to the unprivileged packet observer, which records
telemetry and SQLite packet history without access to the live capture directory.
On macOS, build and run it using the [`Docker and OVS with Lima`](../../infrastructure/lima/README.md)
procedure.

Native Linux prerequisites include `qemu-system-x86_64`, system OVS, Docker
with Buildx for image builds, `iproute2`, `nft`, `dumpcap`, `capinfos`, Python 3,
curl, and `setpriv`. UID and GID 65532 must both belong to the dedicated
`graphx-qemu` account/group; the launcher refuses any other identity. Provision
that account deliberately on native Linux, checking for numeric ID conflicts
first. Lima provisioning creates it automatically. The launcher drops QEMU and
observer privileges to that identity and always selects TCG.
