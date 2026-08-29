# Shared-memory transport

GraphX provides a bounded, copy-based, single-producer/single-consumer transport
using POSIX `shm_open`, `mmap`, and process-shared pthread synchronization. It is
intended for local processes on one Linux or macOS host.

## Version 1 layout

One mapped segment contains:

1. a magic value and layout version;
2. configured ring capacity and maximum framed-message size;
3. monotonic head and tail sequence counters;
4. producer and consumer process IDs, closed state, and recovery count;
5. a process-shared mutex plus `not_empty` and `not_full` condition variables;
6. fixed-size slots containing a native `uint32_t` slot length and the exact
   GraphX `u32be + Envelope` framed bytes.

Slots are selected with `sequence % capacity`; payload bytes are copied into and
out of the ring. The initial implementation deliberately does not expose mapped
memory to node code or attempt zero-copy ownership transfer.

The total payload capacity is limited to 256 MiB. A slot can hold at most
16,777,220 bytes, including the four-byte frame prefix. Connector settings must
exactly match the segment's capacity and maximum message size.

## Ownership and lifecycle

`listen` is the sole consumer and segment owner. It refuses to replace a segment
whose recorded consumer process is still alive. An abandoned segment is unlinked
and recreated. `connect` claims the sole producer role and waits up to
`connect_timeout_ms` for the owner to create and initialize the segment.

An orderly close marks the channel closed, wakes both sides, unmaps it, and lets
the owner unlink the POSIX name. The consumer drains already committed messages
before reporting closure.

Segments are created with mode `0600`. GraphX does not provide authorization or
encryption above operating-system ownership; do not use a shared segment across
untrusted processes.

## Backpressure

The `block` policy waits on `not_full` for at most `send_timeout_ms`. The `reject`
policy immediately throws when the ring is full. There is no overflow allocation
or hidden unbounded queue. Oversized messages are rejected before taking a slot.

## Crash behavior

Both sides record process IDs. Waiters periodically test peer liveness, so a
consumer can finish after an exited producer and a blocked producer reports an
exited consumer. PID liveness is necessarily best-effort because operating
systems can reuse process IDs.

On Linux, the process-shared mutex is robust. If a process dies while holding it,
the survivor makes the mutex consistent, discards potentially inconsistent queued
slots, marks the channel closed, and requires segment recreation. macOS does not
offer the same robust process-shared mutex facility: ordinary peer death is
detected, but death inside the short ring critical section can leave that segment
unrecoverable until it is recreated by a new listener.

This first version does not support multiple producers, multiple consumers,
cross-host access, dynamic resizing, or container IPC isolation management.
