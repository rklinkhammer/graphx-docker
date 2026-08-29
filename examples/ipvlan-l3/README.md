# Standalone IPvlan L3 pipeline

Generator, transform, and sink each belong to a distinct Docker IPvlan L3
network and subnet. All three domains share one parent interface; the Linux
IPvlan L3 data path routes unicast traffic between their subnets without an L2
broadcast domain or a next-hop gateway.

```text
generator 10.42.1.10/24  ─┐
transform 10.42.2.20/24  ─┼─ gx-l3-parent / host IPvlan L3 routing
sink      10.42.3.30/24  ─┘
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

The three processing containers are separate Compose projects. Remote hosts
need explicit routes for `10.42.1.0/24`, `10.42.2.0/24`, and `10.42.3.0/24`
through the Docker host if this isolated parent is replaced by a physical one.
