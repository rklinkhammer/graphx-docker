# Runtime lifecycle and heartbeat behavior

GraphX demo nodes install SIGINT and SIGTERM handlers before loading the master
configuration. The handlers only set a signal-safe flag. Normal process code
observes that flag, stops taking work, closes transports, and returns normally.

Transform and sink use 200 ms receive polls so an idle TCP or shared-memory node
can notice shutdown without another message arriving. The generator divides its
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

## Current boundary

TCP accept and connected receive operations are pollable. Shared-memory waits
are timed and peer-aware. The existing Unix-domain listener still performs its
initial `accept` during construction, so SIGTERM before its first client connects
cannot be observed until that accept completes. Making listener creation fully
asynchronous is deferred to the next transport-API revision.
