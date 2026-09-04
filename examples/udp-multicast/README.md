# UDP multicast example

One publisher sends five datagrams to the administratively scoped group
`239.255.42.1`. The formal GraphX subscriber and a diagnostic subscriber both
join the group on loopback and receive the same transmission.

```sh
cmake --preset dev
cmake --build --preset dev
examples/udp-multicast/run.sh
```

Two `PASS received=5` lines prove local fan-out. The graph model still describes
one logical producer-to-consumer edge; native multi-destination graph edges are
not part of Phase 11. Routed multicast may additionally require IGMP and network
configuration.
