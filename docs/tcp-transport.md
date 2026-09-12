# TCP transport

TCP edges use length-prefixed GraphX envelopes and support bounded connect and send
deadlines, reconnect policy, retry count, and exponential backoff. A listener accepts
subsequent peers after disconnect; close cancels blocked work.

TLS can verify the server, require client certificates, and use a configured CA,
certificate, key, and server name. TLS material is never included in normalized
telemetry output.
