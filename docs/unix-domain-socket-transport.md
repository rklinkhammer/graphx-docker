# Unix-domain socket transport

Unix-domain edges use filesystem sockets and `u32be` framed GraphX envelopes. Paths,
connect deadlines, send deadlines, peer closure, partial frames, and cancellation
are handled explicitly. A failed or truncated frame invalidates that stream so later
bytes cannot be misinterpreted as a new envelope.
