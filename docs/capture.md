# Capture

For the end-to-end configuration and verification workflow, see
[PCAP configuration](user-guide.md#7-configure-pcap-capture).

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

## Raw SDR application bytes

SDR Python services capture the bytes they send and receive as bounded PCAPNG
LINKTYPE_USER1 (148). These records are labeled `raw-application`; they contain
no synthesized Ethernet, IP or GraphX envelope. GraphX framed application capture
uses USER0 (147), and network-wire capture uses Ethernet (1). The platform can
list and download each node's separately mounted `NODE/NODE.pcapng` without
recursive traversal or following symlinks. Its UI identifies raw records as
application bytes. Application capture requires no packet-capture capabilities.
