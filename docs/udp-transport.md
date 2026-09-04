# UDP transport

GraphX UDP edges carry exactly one `u32be` framed GraphX envelope per IPv4
datagram. The four-byte length prefix is retained even though UDP preserves
datagram boundaries so the existing protocol validator, application capture,
and Wireshark dissector use the same bytes as TCP.

## Delivery modes

- `unicast` sends to one IPv4 destination.
- `broadcast` enables `SO_BROADCAST` and sends to a limited or directed IPv4
  broadcast address. Directed-broadcast validity depends on the deployed subnet.
- `multicast` joins or transmits to an address in `224.0.0.0/4`. `interface`
  selects an IPv4 interface name or address, `ttl` limits scope, and `loopback`
  controls local delivery.

The configuration uses `destination`, `bind`, and `port` for both roles.
`ConnectionMode::connect` creates a sender bound to `bind` on an ephemeral local
port; `ConnectionMode::listen` creates a receiver bound to `bind:port`.
`reuse_address` also enables the platform reuse-port facility where available,
which permits multiple local multicast listeners.

## Limits and failure behavior

The encoded frame must fit `max_datagram_bytes`, which is constrained to
64..65,507 bytes. GraphX never divides a frame among datagrams. IP fragmentation
may still occur when a datagram exceeds the path MTU; examples use a 1,400-byte
limit to fit typical Ethernet paths. Applications should choose smaller IQ or
sample blocks for networks with tunnels or additional headers.
The sender computes the encoded size before allocating the serialized buffer,
so a frame over the configured UDP limit is rejected without a large temporary
serialization.

UDP provides no acknowledgement, delivery, ordering, duplicate suppression,
congestion control, confidentiality, authentication, peer liveness, reconnect,
or end-of-stream indication. Missing receivers normally do not make a send fail.
Use TLS-protected TCP for control traffic that needs reliable, authenticated
delivery.

Malformed, truncated, length-mismatched, unknown-version, and trailing-data
datagrams are dropped without disabling the receiver. Counters distinguish those
events from socket failures. Sequence gaps, duplicates, and out-of-order counts
are estimates over a bounded recent window; late arrival can make an earlier gap
estimate cease to represent permanent loss.
Every socket failure increments its typed counter; repeated text diagnostics on
one edge are limited to one per second.

`receive_buffer_bytes` and `send_buffer_bytes` request kernel socket-buffer
sizes. Kernels can clamp or internally scale them, so GraphX treats a successful
socket option as acceptance rather than requiring exact read-back equality.
Closing a receiver cancels a blocked receive; UDP never returns
`ReceiveStatus::end_of_stream`.
Finite receives enforce one absolute deadline even while malformed or truncated
datagrams keep the socket continuously readable. Dropped traffic cannot restart
or extend that deadline.
Descriptor destruction waits for active send/receive operations after waking a
blocked receiver, preventing close from racing a reused descriptor number.

## Network operations

Broadcast is normally confined to a subnet and may be blocked by container or
host firewalls. The checked-in example uses an internal Docker subnet so it
cannot select a physical interface. Routed multicast requires network support,
often including IGMP snooping/querier configuration and multicast routing.

Source addresses can be spoofed and group membership is not authorization.
Rate-limit publishers, provision bounded kernel buffers, and monitor malformed
and sequence-anomaly counters. Do not log complete payloads.

## Examples

```sh
examples/udp-unicast/run.sh
examples/udp-multicast/run.sh
examples/udp-broadcast/run.sh
```

Unicast and loopback multicast are unprivileged. Broadcast requires Docker for
the isolated example. Docker Desktop results do not replace native Linux
verification of namespace, firewall, or physical multicast behavior.
The broadcast README separates the connected image-preparation step from the
offline run; its runner uses only a preloaded image and disables builds and pulls.

GraphX currently models multicast as one logical source-to-destination edge.
The second process in the multicast example is a diagnostic listener proving
network fan-out, not a native one-to-many graph edge.
