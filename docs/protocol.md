# GraphX envelope protocol

This document is the normative contract for the GraphX application envelope.
Transport framing, envelope encoding, and deployment configuration have separate
versions. The current envelope wire version is 2; configuration remains version
1. Every transport carries the same serialized envelope.

## Stream framing

Stream transports prepend one unsigned 32-bit big-endian envelope length. The
length excludes the prefix and is limited to 16,777,216 bytes. Receivers reject
a zero length, an oversized length, and a connection that ends or times out in
the middle of a frame. In-process and shared-memory transports preserve the same
serialized-envelope contract even where their physical storage differs.

UDP carries that same four-byte prefix and one complete envelope in each
datagram. Its encoded frame is additionally limited to the IPv4 UDP maximum of
65,507 bytes and the configured `max_datagram_bytes`. A mismatched length,
truncated datagram, or trailing data causes the entire datagram to be dropped;
GraphX does not split or reassemble envelopes at the application layer.

## Scalar and collection encoding

- All integers in the envelope are big-endian.
- `timestamp_ns` is the signed 64-bit count of nanoseconds since the Unix epoch;
  its two's-complement bit pattern is encoded as an unsigned 64-bit value.
- A string or payload is `u32 byte_length` followed by exactly that many bytes.
  These fields are byte strings; the protocol does not claim or validate UTF-8.
- Attributes are `u32 count` followed by key/value byte strings. Writers sort by
  key bytes for deterministic output. Readers reject duplicate keys and counts
  greater than 4,096.
- The complete envelope, including headers, is limited to 16,777,216 bytes.
  Writers calculate the checked size before reserving storage and readers enforce
  the frame limit before parsing.
- Decoders require exact consumption. Truncation, trailing bytes, bad magic, and
  unknown versions are errors, not forward-compatible extensions.

## Version 1 layout

Version 1 remains readable and exactly re-serializable for compatibility.

| Offset/order | Field | Encoding |
|---|---|---|
| 0 | magic | ASCII `GXE` |
| 3 | version | `u8`, value 1 |
| 4 | sequence | `u64` |
| 12 | timestamp | `i64` bit pattern |
| 20 | type | byte string |
| next | trace ID | byte string; legacy values are unconstrained |
| next | attributes | count and sorted key/value strings |
| next | payload | byte string |

A decoded v1 envelope has `wire_version == 1`, an empty `message_id`, and an
empty `parent_message_id`. A v1 serializer rejects message lineage fields rather
than silently discarding them.

## Version 2 layout

| Offset/order | Field | Encoding |
|---|---|---|
| 0 | magic | ASCII `GXE` |
| 3 | version | `u8`, value 2 |
| 4 | sequence | `u64` |
| 12 | timestamp | `i64` bit pattern |
| 20 | message ID | 16 raw bytes, non-zero |
| 36 | trace ID | 16 raw bytes, non-zero |
| 52 | parent message ID | 16 raw bytes; all zero means absent |
| 68 | type | byte string |
| next | attributes | count and sorted key/value strings |
| next | payload | byte string |

The public C++ representation renders identities as exactly 32 lowercase
hexadecimal characters. Uppercase, wrong-length, non-hexadecimal, and all-zero
required identities are invalid. The all-zero value is reserved solely for an
absent parent on the wire.

## Identity semantics

`message_id` identifies one logical message. A retry or ordinary in-place
transformation preserves it. Consumers implementing idempotency or duplicate
suppression key on `message_id`, scoped to their retention policy; sequence and
trace IDs are not unique-message keys.

`trace_id` groups causally related messages. A root created with
`Envelope::make` gets a new message and trace ID. `Envelope::derive` creates a
new logical message, preserves the parent's canonical trace ID, and records the
parent's message ID in `parent_message_id`. Copying, serialization, transport,
and deserialization preserve all three values.

IDs are correlation values, not credentials, secrets, signatures, or proof of
origin. The built-in generator combines per-process entropy, monotonic time, and
an atomic counter to produce concurrent-safe 128-bit values; it does not promise
cryptographic unpredictability. Phase 5 must not reuse them for authorization.

The v2 trace value can be used directly as the 16-byte OTLP trace ID. GraphX does
not yet carry W3C `traceparent`, span IDs, sampling flags, or remote-parent span
context; those remain Phase 6 work.

## Compatibility and rollout

| Writer | Reader | Result |
|---|---|---|
| v1 | v1 | supported |
| v1 | Phase 3 reader | supported; legacy identity limitations remain |
| v2 | Phase 3 reader | supported |
| v2 | v1 | rejected as an unknown version |

New root envelopes use v2. A received v1 envelope remains v1 when forwarded or
mutated, which lets a new intermediary preserve compatibility and exact bytes.
There is no silent downgrade from v2 because v1 cannot represent message
identity or lineage.

For a mixed deployment, upgrade consumers and downstream intermediaries first,
then producers. Confirm every receiving process accepts v2 before enabling the
new producer binary. Rollback is safe while producers still emit v1. After v2
production starts, rollback requires draining v2 traffic or restoring v2-capable
readers; changing the version byte is not a valid downgrade.

## Golden vectors and rejection behavior

Canonical vectors live in
[`tests/fixtures/envelope-v1.hex`](../tests/fixtures/envelope-v1.hex) and
[`tests/fixtures/envelope-v2.hex`](../tests/fixtures/envelope-v2.hex). Tests
decode their documented fields and demand byte-for-byte re-serialization. The
negative suite covers selected truncation boundaries, unknown versions, trailing
bytes, zero or malformed identities, duplicate and excessive attributes,
oversized envelopes, and forbidden lossy v1 serialization.

Errors are exceptions with boundary context. Callers must treat a protocol error
as a failed message/connection according to the transport contract; they must not
reinterpret it as timeout, cancellation, or end-of-stream.

## Wireshark representation

Phase 9 introduces no new wire version. PCAPNG application packets contain the
same four-byte stream length followed by the exact v1 or v2 envelope. The USER0
Lua dissector at `wireshark/graphx.lua` applies the bounds and exact-consumption
rules above, including mandatory nonzero v2 message/trace identities and unique
attribute keys, and exposes `graphx.*` display fields. The optional parent ID
may remain all-zero to represent no parent. PCAPNG comments add bounded capture
correlation context but are not part of the envelope and cannot override decoded
values; oversized optional metadata is replaced by an explicit truncation
marker without dropping the canonical frame.
