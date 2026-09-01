# Unix-domain socket transport

Unix-domain sockets use the same `u32be` framed GraphX envelope as TCP, but only
within one host. `connect_timeout_ms` bounds local connection establishment and
`send_timeout_ms` bounds a complete framed write. Any failed framed write, any
partial header or payload timeout/closure, and any invalid prefix or envelope
permanently invalidate the version-1 transport because the stream can no longer
be trusted to remain synchronized. The accepted socket and retained listener
are closed before the failure is returned; later receives report
`end_of_stream` rather than an idle timeout.

Listener creation binds the socket path and returns immediately. The first
`receive_result()` polls and accepts within the caller's receive deadline. This
allows `close()` to cancel a listener before any client connects. Clean peer
closure between frames reports `end_of_stream`; local close reports `cancelled`;
and an idle receive deadline reports `timeout`. A private socket-pair event wakes
poll explicitly, avoiding platform-dependent assumptions about closing a file
descriptor from another thread.

Version 1 accepts one peer for the listener lifetime. The owned filesystem path
is removed on close or destruction. Sends suppress `SIGPIPE` on supported
platforms and surface peer failure as an exception.
