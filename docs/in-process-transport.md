# In-process transport

The in-process transport is a bounded, mutex-protected FIFO shared by endpoints
created for one named channel. Its default capacity is 64 envelopes.

`backpressure: block` waits for capacity for at most `send_timeout_ms`.
`backpressure: reject` fails immediately when the queue is full. Neither policy
allocates overflow storage. A blocked sender and receiver are both woken when
the channel closes.

Committed envelopes drain in FIFO order after peer close. A subsequent typed
receive reports `end_of_stream`; receive after closing the local transport
reports `cancelled`; an elapsed deadline reports `timeout`.

Every endpoint sharing a named channel must specify identical `capacity`,
`backpressure`, and `send_timeout_ms` values. The transport factory rejects a
second endpoint with inconsistent settings instead of silently selecting one.
