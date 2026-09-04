# UDP unicast example

This bounded example sends five GraphX framed envelopes over IPv4 UDP loopback.
It requires no privileges and does not contact an external network.

```sh
cmake --preset dev
cmake --build --preset dev
examples/udp-unicast/run.sh
```

Success ends with `PASS received=5`. UDP has no acknowledgement, retransmission,
ordering, peer authentication, or encryption. TCP with TLS remains the preferred
control transport.
