# Standalone IPvlan L2 routed pipeline

Each GraphX node has its own Docker IPvlan L2 network and subnet. The domains
are genuinely independent: three Compose projects attach to three external
networks, and traffic can cross domains only through `gx-ipvl2-rtr`.

```text
generator / 10.41.1.0/24
  → br-l2-gen → Linux router namespace
  → br-l2-xform → transform / 10.41.2.0/24
  → Linux router namespace → br-l2-sink → sink / 10.41.3.0/24
```

Linux prerequisites are Docker Engine, Open vSwitch, iproute2, nftables, and
optionally tcpdump/dumpcap.

```sh


```

The OVS bridge in every domain has a SPAN output. `tc netem` can be attached to
any router interface with `graphx infra fault apply`, using router
`ipvlan-l2-router` and interface `generator`, `transform`, or `sink`.
