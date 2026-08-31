# Mixed network example

The reference topology routes a GraphX TCP edge across two Docker domains:

```text
generator 10.10.0.10 (explicit MAC)
  → macvlan L2 → br-gx-mac (OVS + SPAN)
  → gx-router 10.10.0.1 / 10.20.0.1 (forwarding, nftables, netem)
  → br-gx-ipv (OVS + SPAN) → ipvlan L2
  → transform 10.20.0.20 → sink 10.20.0.30
```

The macvlan and ipvlan Compose files use different project names and external
networks. Neither project owns the infrastructure.

## Native Linux

Prerequisites: Docker Engine + Compose, Open vSwitch, iproute2, nftables, and
optionally tcpdump/dumpcap. Build GraphX, review the plan, then start:

```sh
cmake --preset dev && cmake --build --preset dev
./build/dev/graphx validate examples/mixed-network/graphx.yaml
./build/dev/graphx infra create examples/mixed-network/graphx.yaml --dry-run
examples/mixed-network/scripts/linux-up.sh
examples/mixed-network/scripts/status.sh
```

Stop containers before deleting their external networks:

```sh
examples/mixed-network/scripts/linux-down.sh
```

`linux-up.sh` refuses to overwrite existing interfaces, OVS bridges, namespaces,
or Docker networks. If a previous run was interrupted or startup reports that an
interface already exists, recover and retry with:

```sh
examples/mixed-network/scripts/linux-down.sh
examples/mixed-network/scripts/linux-up.sh
```

Startup is transactional after its preflight check: if infrastructure or either
Compose project fails during that attempt, the helper tears down what that
attempt created. The preflight deliberately does not delete existing resources,
because they may belong to a still-running lab that should be inspected first.

Macvlan isolates its parent host from macvlan children by design. Routed traffic
between the two container domains works, but host-to-container access needs a
separate host macvlan shim and route, which this example intentionally omits.

## Docker Desktop for macOS

The macOS profile substitutes bridge networks for unsupported macvlan/ipvlan and
runs userspace OVS in a privileged router container:

```sh
examples/mixed-network/scripts/macos-up.sh
examples/mixed-network/scripts/status.sh
docker logs gx-ovs-ovs-router-1
examples/mixed-network/scripts/macos-down.sh
```

This is an OVS/routing/inspection lab, not an exact macvlan behavior emulator.
See `docs/network-infrastructure.md` for the precise boundary.

## Faults and capture

The helpers automatically target the native namespace or the macOS OVS container:

```sh
examples/mixed-network/scripts/fault.sh apply
examples/mixed-network/scripts/fault.sh clear
examples/mixed-network/scripts/capture.sh mac
examples/mixed-network/scripts/capture.sh ipv
examples/mixed-network/scripts/capture.sh mac captures/mixed-mac.pcapng
examples/mixed-network/scripts/capture.sh ipv captures/mixed-ipv.pcapng
```

Set `DELAY`, `JITTER`, and `LOSS` to override the default 20 ms, 3 ms, and 1%.
The one-argument form displays live standard Ethernet packets with `tcpdump`.
The two-argument form saves standard Ethernet PCAPNG with `dumpcap`. In the
macOS profile both tools and OVS run inside the provided privileged container.
