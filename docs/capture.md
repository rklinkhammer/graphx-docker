# Capture

Application capture writes framed GraphX envelopes to bounded PCAPNG files using the
USER0 link type. Metadata includes direction, edge, sequence, timestamp, message,
trace, parent, and type information.

Network capture is a separate owned lifecycle. An OVS mirror sends Ethernet frames
to a capture attachment, and a bounded capture process writes rotating files under
an absolute, identity-checked directory. GraphX records the process start time,
interface identity, and directory identity before cleanup.

`tools/graphx-extcap` exposes current captures to Wireshark. The Lua dissector
decodes envelope wire format 2 and can also decode configured UDP ports. Capture
catalog and download APIs validate paths and enforce file and response limits.
