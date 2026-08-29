# Shared-memory process pipeline

This example runs generator, transform, and sink as three local processes. Both
logical edges use bounded POSIX shared-memory rings rather than sockets.

```sh
cmake --preset dev && cmake --build --preset dev
./build/dev/graphx validate examples/shared-memory/graphx.yaml
examples/shared-memory/run.sh
```

The default run sends 20 messages and exits cleanly. Override
`GRAPHX_MAX_MESSAGES`, `GRAPHX_INTERVAL_MS`, or `GRAPHX_BUILD_DIR` as needed.
Each listener owns its segment and unlinks it during an orderly shutdown. A new
listener also removes a stale name before creating its segment.

The implementation is single-producer/single-consumer and copy-based. It does
not work across hosts or across containers with separate IPC namespaces unless
they are deliberately configured to share the same POSIX shared-memory mount and
IPC namespace.
