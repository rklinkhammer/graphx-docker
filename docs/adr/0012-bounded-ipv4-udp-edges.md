# ADR 0012: Bounded IPv4 UDP edges

## Status

Accepted for Phase 11.

## Context

Network-connected data sources commonly emit UDP unicast, broadcast discovery,
or multicast streams. GraphX previously supported only reliable streams or
local transports. Adding UDP must not imply TCP-like lifecycle guarantees or
create a second envelope format.

## Decision

GraphX adds one IPv4 UDP transport with explicit `unicast`, `broadcast`, and
`multicast` modes. Each datagram contains one existing `u32be` framed envelope.
Datagrams and socket buffers are bounded, invalid datagrams are dropped with
typed metrics, and receiver cancellation uses a dedicated local wakeup socket.

The existing graph edge remains one producer and one consumer. Multicast can be
observed by additional network listeners, but GraphX does not coalesce multiple
logical edges or promise native graph fan-out in this phase.

## Consequences

Existing protocol captures and the Lua decoder remain useful, and TCP behavior
is unchanged. Applications must tolerate loss, duplication, and reordering.
IPv6, DTLS, reliable delivery, fragmentation/reassembly, and first-class
one-to-many graph topology require later decisions.
