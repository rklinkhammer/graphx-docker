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

The C++ loader validates the source configuration once. Supported Compose
launches run `graphx config normalize` in a one-shot service and place the
versioned JSON in a runtime volume mounted read-only by telemetry. Telemetry
loads that file through `GRAPHX_NORMALIZED_CONFIG`; it does not independently
interpret the deployed YAML topology. The runtime volume is Docker-managed (and
therefore guest-native for Lima laboratories), not a macOS shared directory.
Canonical demo launchers recreate and complete this one-shot service on every
start so an exited init container cannot preserve stale configuration.

Direct local Node development temporarily retains a bounded `GRAPHX_CONFIG`
YAML fallback. It is a compatibility path, not the deployed configuration
contract. `GRAPHX_CONFIG_DIRECTORY` may identify the original configuration
directory when normalized configuration contains relative history or capture
paths. The browser derives both its initial HTTP model and WebSocket snapshots
from the same loaded normalized topology.

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
