# ADR 0003: Typed receive outcomes and bounded local queues

- Status: Accepted
- Date: 2026-08-31

## Context

The version-1 transport API returned `std::optional<Envelope>`. An empty value
could mean deadline expiry, orderly peer closure, local shutdown, or—in some
implementations—listener startup with no peer. Runtime loops therefore could not
make deterministic lifecycle decisions. The in-process transport also used an
unbounded deque, and Unix-domain listener construction blocked in `accept`.

## Decision

Add `Transport::receive_result()` with four outcomes: `message`, `timeout`,
`end_of_stream`, and `cancelled`. Keep transport/protocol failures as contextual
exceptions. Retain `receive()` as a source-compatible optional-envelope facade,
and retain its virtual slot so existing transport subclasses continue to build.
The default typed adapter can classify a legacy empty optional only as timeout.
Migrate GraphX runtime loops and built-in transports to the typed API.

Bound every in-process channel. Configure capacity, block/reject backpressure,
and the blocking send deadline per edge. Reject inconsistent settings for
endpoints sharing a named channel. Make Unix-domain listener construction
nonblocking with respect to peer arrival and perform timed, cancellable accept in
the receive path. Bound Unix-domain connection and send operations with explicit
deadlines and use an explicit cancellation event to wake polling on every
supported host. Bound TCP listener polling into short cancellation slices so
close is observed even where descriptor close does not interrupt `poll`. Keep
`close()` idempotent and allow it to wake a blocked receive when object
destruction waits for that receive to finish.

## Consequences

- Runtime loops can distinguish ordinary idleness, upstream completion, and a
  local stop request.
- Local traffic cannot grow an application queue without a configured bound.
- The compatibility facade intentionally loses non-message status information;
  lifecycle-aware integrations must adopt `receive_result()`.
- Partial-frame timeout or closure remains a transport failure because the byte
  stream can no longer be safely synchronized. The single-peer Unix transport
  closes both the accepted socket and retained listener before returning such a
  failure; invalid prefixes, malformed envelopes, and failed framed writes use
  the same terminal policy.
- Cancellation and resource release happen before best-effort close-state
  observer notification. Observer exceptions cannot escape `close()` or prevent
  a blocked operation from waking.
- Accepted Unix descriptors acquire scoped ownership immediately, before socket
  setup, and transfer to the transport only after setup succeeds.
- Transport destruction must not race an active operation; ownership and thread
  joining remain caller responsibilities.
