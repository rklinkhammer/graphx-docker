# P3 network design

Status: implementation present; privileged acceptance pending. See
[p3-verification.md](p3-verification.md) for executed checks and the isolated test plan.

Extend authored version 3 and normalized contract 2 with optional attachment
`mtu` (576–9000; absent means 1500) and mirror `delivery` (`host`, the existing
default, or `container`). Container delivery requires a container recorder owner,
no IP address/routes, one passive attachment and no application data-plane ports.
The existing ownership lifecycle resolves the container identity and namespace,
creates both veth ends, records aliases/indices/OVS UUIDs, moves the peer into that
namespace, and checks identity before readiness and cleanup. No alternate manifest
or launcher is introduced.

Jumbo prerequisites cover every selected attachment on a VITA application bridge,
including mirror delivery. Validate UDP payload plus 28 IPv4/UDP bytes, with 4156
bytes required for VITA and up to 8864 for P2 power. Authored processor path_mtu
must be satisfied by the selected path; 9000 is the demonstration target. Managed VITA paths are restricted to one owned application bridge; routed jumbo
paths are rejected. The maximum approved packet budgets are reserved even when a
run selects smaller packets. Verify both ends at runtime, along with OVS requested
and observed interface MTU and attachment identity, before publishing readiness. Capture snap length must
cover IP MTU plus 22 bytes (Ethernet plus two VLAN tags, excluding unavailable FCS).
Socket and recorder buffers are bounded independently of IP MTU. Jumbo veth
paths disable TSO/GSO/GRO and transmit checksum offload with `ethtool` on both
ends; readiness verifies those settings. This avoids oversized coalesced capture
records and deferred checksum bytes. Missing tool/support fails startup before
application release. No physical interface is changed. The rationale follows the
[kernel segmentation](https://docs.kernel.org/networking/segmentation-offloads.html)
and [checksum offload](https://docs.kernel.org/networking/checksum-offloads.html)
contracts; actual GraphX bytes still require live qualification.

One OVS select-all mirror selects both directions on the owned application bridge.
A frame matching ingress and egress selection is copied once by that mirror;
separate forwarding instances can still yield multiple observations. Mirrored
traffic includes ARP and encrypted control, without protocol filtering. Management
and unrelated bridges are outside scope. The output port is dedicated to mirroring. Mirror names use graph-scoped interface
names; recovery lookup also requires the ownership token, so another graph can
reuse the same logical attachment ID.
Container-originated frames are independently dropped at the host peer's ingress
using an owned tc filter before enabling the endpoint. Verify the filter at health
checks; link deletion removes it. The recorder opens its bound packet socket with
NET_RAW, drops process capabilities, and installs a syscall restriction preventing
transmission through or duplication of that descriptor before publishing readiness.
No NET_ADMIN, Docker/OVS socket, host network or privileged container is permitted.

The recorder consumes normalized configuration and the common release barrier.
It receives/discards Ethernet bytes in fixed storage, counts received bytes,
truncation and available kernel drops/errors, and reports bounded summaries. It
never opens packet files or archives. Normalization disables its application
capture and the compiler omits a writable capture mount. Shutdown is bounded and may lose queued
packets. Recorder readiness follows the selected common lifecycle policy; degraded
startup is specified in [lifecycle design](lifecycle-design.md).

Optional diagnostic capture remains the existing owned dumpcap/PCAPNG lifecycle,
with bounded retention. For a container mirror attachment, the common lifecycle derives a separate
host-owned mirror veth for each enabled diagnostic capture. Its ID is
`diagnostic:<capture-id>`; interface names are graph-scoped and deterministic.
Expected and observed namespace, ifindex, alias and OVS identities use the same
ownership ledger and collision/recovery checks as other mirror endpoints.
Dumpcap binds the independent host peer. Recorder namespace deletion cannot
delete diagnostic delivery. Captures disabled means no derived diagnostic endpoint.
This does not replace the recorder or its passive ingress-drop filter.

Portable qualification covers schemas, normalization, compiler output, MTU/path
rejection, recorder accounting, identity checks and lifecycle command fixtures.
Actual packet forwarding, tc enforcement, namespace capability behavior, complete
mirrored bytes and fragmentation require an explicitly authorized isolated Linux
or GraphX Lima run. A concrete test plan and unrun commands will be recorded in
p3-verification.md before requesting that authorization. Nominal four-radio IQ
payload is 16 MB/s; measured wire traffic and best-effort reception are separate.

## Receive and qualification boundaries

The fixed frame buffer is 9022 bytes. Linux packet VLAN auxiliary metadata accounts
for a stripped VLAN tag in received byte counts; no archive or synthetic packet
stream is produced. The receive socket requests 1 MiB, accepts the kernel clamp,
and verifies/reports a positive effective buffer no larger than 2 MiB (Linux
accounts twice the requested size). Each loop receives at most 64 frames before
servicing shutdown and one-second status reporting. Only one bounded summary line
is pending; stdout and the common log-drainer destination are nonblocking.

The seccomp filter uses TSYNC, validates the syscall architecture, and allows only
receive, existing telemetry/log output and runtime housekeeping. It forbids writes
to the packet descriptor, descriptor duplication/transfer, new sockets, exec,
io_uring and writable file opens. Pipes and status-only fcntl operations remain
allowed for the runtime and sanitizer; fcntl duplication commands stay denied. Capabilities are dropped before worker threads
are created. The host tc matchall/drop filter independently blocks egress from the
recorder namespace. Health checks reject altered filters and replaced peer identity;
cleanup checks the same identity, including an interrupted peer move. A stopped
container may already have removed its veth, which is absence rather than a new
identity to adopt. Partial resource creation retains the existing fail-closed
ownership recovery contract.

OVS specifies one copy per output destination when a packet matches both ingress
and egress selection; the live acceptance test must verify this with unique frame
identifiers ([OVS Mirror reference](https://www.openvswitch.org/support/dist-docs/ovs-vswitchd.conf.db.5.html)).
Syscall restrictions follow the Linux seccomp filter interface
([kernel documentation](https://kernel.org/doc/html/latest/userspace-api/seccomp_filter.html)).
Neither specification is substituted for GraphX live evidence.

The opt-in radio/processor/detector catalog now accepts native or container execution;
recorder is container-only. Existing design image references are not newly published
P3 application images. Verified private qualification images and the live harness are recorded in
[preparation evidence](p34-preparation.md); complete release/image publication belongs to P5. P1/P2 native
behavior and the vrt_framework dependency pin remain unchanged.
