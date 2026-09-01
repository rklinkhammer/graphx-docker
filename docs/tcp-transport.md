# TCP transport lifecycle

GraphX TCP transports are blocking framed streams with bounded connection and
write operations. They intentionally avoid an async runtime.

## Framing and receive deadlines

Every frame is a four-byte unsigned big-endian length followed by one serialized
GraphX envelope. The maximum accepted payload is 16 MiB.
`receive_result(timeout)` uses one deadline for the whole frame:

- no bytes before the deadline reports `timeout`;
- closure before a new header reports `end_of_stream` when reconnect is disabled;
- local close reports `cancelled`;
- closure or timeout during a header/payload throws a contextual error and closes
  that connection;
- malformed and oversized frames throw with edge and endpoint context.

Closing a transport shuts down both its active connection and retained listener,
which interrupts blocked polling/reads. Linux sends use `MSG_NOSIGNAL`; macOS
sockets enable `SO_NOSIGPIPE`.

The legacy `receive(timeout)` facade maps every non-message outcome to
`std::nullopt`; new lifecycle-aware code should use `receive_result`.

## Retry and reconnect

`connect_timeout_ms` bounds each address attempt. Initial connection and later
reconnection use an exponential policy configured by `retry.max_attempts`,
`retry.initial_backoff_ms`, and `retry.max_backoff_ms`. Closing an existing
transport interrupts a retry backoff.

A reconnecting listener retains its listening socket and accepts a replacement
peer after a clean between-frame disconnect. A reconnecting client retries one
complete frame after a failed send. This provides at-least-once delivery, not
exactly-once delivery; applications can deduplicate using envelope sequence and
trace identifiers.

## Backpressure

TCP backpressure is intentionally blocking and bounded. `send_timeout_ms` is the
maximum time allowed to write a complete framed envelope. A timeout closes the
connection; with reconnect enabled, the sender reconnects and retries the frame
once. GraphX does not currently queue frames above the kernel socket buffer, so
there is no hidden unbounded application queue.
