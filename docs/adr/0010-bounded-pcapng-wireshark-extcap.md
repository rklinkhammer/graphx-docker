# ADR 0010: Bounded PCAPNG with a Lua dissector and validating extcap

Status: accepted

## Context

GraphX already captured exact canonical framed envelopes as PCAPNG USER0 records
and had a shell adapter that copied or tailed a selected file. Phase 9 requires
those seams to be operable against untrusted files and useful in Wireshark
without changing the versioned envelope protocol. Capture also previously had
no disk bound, and the collector's shared-volume download path could not rely on
the writers being trusted.

## Decision

Keep application capture transport-neutral and keep exact `u32be + GXE` bytes in
Enhanced Packet Blocks. Add explicit snap, file-byte, and packet-count limits.
Commit blocks atomically at the writer abstraction. Open outputs nonblocking and
without truncation, validate a single-link regular-file descriptor, and only
then truncate it; reject symlinks, hard links, and special files without
changing them. Bound optional PCAPNG comments independently of valid envelope
size, preserving the canonical packet and emitting a truncation marker when
necessary. Stop only capture when a limit or write error occurs.

Use a self-registering Lua dissector bound to Wireshark USER0. It decodes both
supported envelope versions with boundary checks, mandatory nonzero v2
message/trace identities, unique attribute keys, and expert diagnostics. Lua
avoids a Wireshark C plugin ABI/build dependency and is directly reviewable; the
tradeoff is that operators must install it into a GraphX-specific profile.

Replace byte-blind `tail` with a Python 3 extcap process that validates the
PCAPNG 1.0 section, indefinite section length, single interface, interface-zero
packet ordering, interface DLT, bounded block length, and trailing block length,
then emits only complete blocks. It explicitly rejects unsupported capture
filters, later sections/interfaces, and mismatched application/Ethernet
interfaces.

Treat raw captures as sensitive. The telemetry collector retains observation
authorization and opens each shared-volume download nonblocking and no-follow,
validates the exact single-link regular-file descriptor, and streams that same
descriptor. Bound catalog refreshes independently by examined directory entries
and returned files, sort the bounded result, cache it for one second, and expose
truncation metadata. Direct downloads do not depend on catalog inclusion. The
new Wireshark paths do not authenticate or control runtimes and cannot bypass
the Phase 8 control plane.

## Consequences

- The envelope and framing formats remain unchanged.
- Disk use is bounded, but capture stops at a limit instead of silently
  overwriting evidence or performing implicit retention.
- USER0 remains private-use and can conflict with another local dissector.
- Python 3 is the only extcap runtime dependency; Wireshark/TShark is required
  only to use or runtime-test the dissector.
- OVS/Ethernet capture remains separate and uses standard Ethernet dissection.
- Large valid SHB/IDB options are parsed as complete bounded blocks rather than
  through a fixed-size preamble.
- Release packaging and supported-version policy remain Phase 10 work.
