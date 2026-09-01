# Runtime lifecycle and heartbeat behavior

GraphX demo nodes install SIGINT and SIGTERM handlers before loading the master
configuration. The handlers only set a signal-safe flag. Normal process code
observes that flag, stops taking work, closes transports, and returns normally.

Transform and sink use the typed receive result with 200 ms polls. A timeout
continues the heartbeat loop, terminal peer end-of-stream drains and exits
normally, and local cancellation exits immediately. A reconnecting TCP listener
treats clean connection closure as a transient disconnect while it waits for a
replacement peer; that mode therefore relies on coordinated shutdown or the
finite message limit rather than peer closure to terminate. The generator divides its
emit delay into short waits so both shutdown and heartbeat publication remain
responsive even with a long sample interval. `GRAPHX_MAX_MESSAGES` applies to
all three demo nodes, which makes finite TCP and shared-memory runs drain and
exit without a test harness killing listeners.

The typed `observability.telemetry` configuration controls UDP destination,
WebSocket path, heartbeat interval, and heartbeat timeout. Runtime events also
count as liveness observations. The telemetry service marks a node offline when
no event or heartbeat arrives before the timeout and broadcasts the transition
to browser clients.

The telemetry service reads the same `GRAPHX_CONFIG` file and normalizes its
nodes, edges, transports, deployment images, infrastructure components, and edge
paths into each API/WebSocket snapshot. The browser therefore does not need a
second hand-maintained copy of the topology.

## Receive outcomes and cancellation

`Transport::receive_result()` reports `message`, `timeout`, `end_of_stream`, or
`cancelled`. Transport and protocol failures remain exceptions. The original
`receive()` API remains as a version-1 compatibility facade and returns only an
optional envelope; callers that need lifecycle meaning must use the typed API.

TCP and Unix-domain accept and connected receive operations are pollable.
TCP listener accept uses a short bounded polling slice so cross-thread close is
observed even on hosts where closing a descriptor does not wake an existing
`poll` call.
Unix-domain `listen` now binds and returns without waiting for its first client;
the timed receive path performs accept, so shutdown before the first connection
is observable. Unix-domain connect and send operations have configured
deadlines. Shared-memory and in-process condition waits are bounded and are woken
by close. `close()` is idempotent across all transports, and resource release
and cancellation occur even if a best-effort trace observer throws.

Object lifetime is still owned by the caller: a transport may be closed from a
control thread to wake a blocked receive, but it must not be destroyed until the
receiving thread has returned.
