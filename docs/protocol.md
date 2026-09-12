# Envelope protocol

GraphX uses one binary envelope format. Multi-byte integers are network byte order.
Stream transports prefix each envelope with a four-byte unsigned length.

The envelope contains:

1. magic `GXE` and wire version `2`;
2. sequence and nanosecond timestamp;
3. 128-bit message ID, trace ID, and optional parent message ID;
4. length-prefixed type;
5. a bounded set of sorted, length-prefixed attributes;
6. length-prefixed payload.

Message and trace IDs are non-zero lowercase hexadecimal identities. The all-zero
parent field means no parent. Duplicate attribute keys, trailing bytes, malformed
identities, unsupported versions, and envelopes over 16 MiB are rejected.

TCP and Unix-domain transports use `u32be` framing. UDP carries one complete
envelope per datagram when framing is enabled. In-process and shared-memory
transports validate the same envelope before publication. Raw external data-plane
edges use `framing: none` and do not enter the GraphX transport factory.

The Wireshark dissector in `wireshark/graphx.lua` decodes framed USER0 captures and
the configured UDP port range.
