# Runtime lifecycle

GraphX transports expose typed receive outcomes: message, timeout, end of stream,
and cancellation. The convenience `receive()` method returns the message when that
is all a caller needs. Built-in transports implement bounded connection, send,
receive, backpressure, and shutdown behavior.

Infrastructure uses one create/status/recover/destroy lifecycle. The ownership
ledger records the graph ID, optional instance ID, resolved resource mappings,
configuration hash, owner token, expected objects, and stable identities for
resources that were created. Create publishes state
atomically; recover and destroy verify the complete identity set before mutation.
Collisions, replacements, partial ownership, and insecure state files fail closed.

The [instance identity contract](instance-identity.md) defines identity lifetimes,
restart freshness, and isolation requirements for instance-aware execution. Its
implementation-boundary section distinguishes infrastructure isolation from the
remaining application runtime requirements. Explicit instances use separate
ledgers and locks; legacy configurations retain graph-ID-keyed ownership. Manual
route changes require a matching ready ledger and a verified owned namespace.
