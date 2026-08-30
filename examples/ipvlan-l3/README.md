# Standalone IPvlan L3 pipeline

Generator, transform, and sink each belong to a distinct IPvlan L3 subnet. The
three subnets are IPAM domains of one external Docker IPvlan network on one
parent interface. This is Docker's supported layout for routing between L3
subnets that share a parent; Docker does not permit multiple IPvlan network
objects to claim the same parent interface.

```text
generator 10.42.1.10/24  ─┐
transform 10.42.2.20/24  ─┼─ gx-ipvl3-domains → gx-l3-parent
sink      10.42.3.30/24  ─┘       IPvlan L3 routing, no L2 broadcast
```

This is Linux-only and requires Docker Engine and iproute2. It intentionally
omits `gateway` from all three network definitions: Docker ignores a gateway in
IPvlan L3 mode and installs a device default route in each container.

```sh
cmake --preset dev && cmake --build --preset dev
./build/dev/graphx validate examples/ipvlan-l3/graphx.yaml
./build/dev/graphx infra create examples/ipvlan-l3/graphx.yaml --dry-run
examples/ipvlan-l3/scripts/up.sh
docker logs -f gx-ipvl3-sink-sink-1
examples/ipvlan-l3/scripts/status.sh
examples/ipvlan-l3/scripts/down.sh
```

`up.sh` waits for an actual sample to traverse generator → transform → sink and
rolls the attempt back if no sink value appears within 60 seconds. A successful
start therefore verifies more than network creation. `status.sh` shows container
state and the latest doubled sink values.

The processing containers remain separate Compose projects; the infrastructure
layer independently owns their shared external network. Each node still has its
own L3 subnet and broadcast-free routing domain. Remote hosts
need explicit routes for `10.42.1.0/24`, `10.42.2.0/24`, and `10.42.3.0/24`
through the Docker host if this isolated parent is replaced by a physical one.

If an older run fails with `network ... is already using parent interface`, run
`examples/ipvlan-l3/scripts/down.sh` once. The current helper also removes the
three legacy partial-network names before retrying the supported multi-subnet
layout.
