# GraphX Architecture and Network Topology

**Version:** 1.3
**Repository baseline:** GraphX 1.1.0
**Review date:** 2026-09-10
**Status:** Living architecture document

## Executive summary

GraphX is an educational, configuration-driven framework for describing a directed processing graph once, running its nodes as processes, containers, or externally managed systems, and observing both application messages and the network paths that carry them. Its defining architectural choice is to keep several concerns related but distinct:

1. the **logical graph** says what processes data;
2. the **transport model** says how each edge communicates;
3. the **network-infrastructure model** says which L2/L3 devices and paths support that communication;
4. the **deployment model** says where nodes run and how they are packaged;
5. the **observability model** says what is measured, retained, exported, and captured; and
6. the **control and GUI plane** presents those models and applies narrowly scoped runtime commands.

The versioned `graphx.yaml` file is the authoritative source for these views. Version 1 is migration input only: validation, inspection, projection, normalization, and deterministic migration remain available, while every infrastructure action fails before mutation. Version 2 expresses OVS semantic profiles and typed attachment intent; M3 realizes identity-owned OVS bridges, M4 attaches verified managed containers with owned veth pairs, M5 realizes namespace veths, mirrors, router forwarding/routes/policy, semantic profile flows, and the migrated network laboratories, M6 adds identity-owned QEMU TAP endpoints with exact non-root access, M7 owns bounded mirror capture and timed netem faults, and M8 removes Docker-driver and Docker Desktop simulation realization. The C++ loader and the telemetry service both validate and normalize configuration, while the browser derives its Application and Network views from the normalized topology instead of keeping a second topology definition.

GraphX supports five GraphX-aware transports—bounded in-process queues, TCP, Unix-domain sockets, POSIX shared memory, and IPv4 UDP—and also represents external raw TCP/UDP edges that GraphX observes but does not instantiate. The same canonical GraphX envelope and `u32be` frame are used by the stream transports, shared memory, UDP datagrams, application capture, and Wireshark tooling. Raw external edges use `framing: none` and are deliberately rejected by the GraphX transport factory.

Network infrastructure is a peer layer rather than an accidental consequence of Docker Compose. Its configuration models Ethernet, macvlan, and ipvlan semantic domains; node interfaces; Open vSwitch bridges and ports; VLAN metadata; SPAN mirrors; namespace routers; routes; forwarding policies; and ordered per-edge network paths. System OVS on Linux is the sole backend. On macOS that Linux runtime is the dedicated Lima VM; Docker Desktop is not a privileged network-lab backend.

The QEMU examples prove a second important boundary: an application does not need to link GraphX or emit GraphX envelopes to appear in the topology. A shared x86_64 guest exchanges ordinary TCP and UDP traffic in three profiles: two slirp compatibility paths and the primary M6 TAP/OVS path for native Linux and Lima. A passive packet observer converts network evidence into bounded live metrics, Ethernet PCAPNG, and separate packet-history records. The GUI displays logical nodes, network/runtime boundaries, accelerator evidence, protocol readiness, capture downloads, and history while restricting control to the origin generator.

Observability is intentionally best effort and bounded. Runtime events update live WebSocket topology, Prometheus metrics, rolling SLO state, optional OTLP export, optional SQLite metadata history, and capture correlation. GraphX application PCAPNG and standard Ethernet PCAPNG are complementary: the former explains envelopes; the latter explains the real network path. The GUI ties these artifacts together by edge identity, message identity where available, filenames, packet indexes, and capture offsets.

The architecture is suitable for reproducible laboratories and controlled demonstrations. Its present limits are equally important: GraphX-managed execution allows only DAGs (external raw device relationships may loop); infrastructure provisioning is create/destroy rather than reconciliation; UDP is bounded but unreliable; multicast remains one logical producer-to-consumer edge; OVS and application captures are intentionally separate and are not automatically cross-correlated; fault realization currently supports timed netem rather than OVS drop rules; the older QEMU profiles still use user-mode networking for compatibility; and the telemetry/control system is a single-collector control domain rather than a distributed control plane.

For Apple Silicon development, Migration M1 adds an optional Lima ARM64 Linux
execution environment. The VM contains rootful Docker, system OVS, Linux
namespace/veth/TAP, nftables, netem, QEMU, capture, and build tools while the
repository remains mounted from macOS. High-I/O and privileged runtime state is
VM-local. M1 verifies these primitives with a disposable topology. M2 adds the
strict configuration version 2 intent boundary, fixed OVS semantic profiles,
typed attachments, and deterministic version-1 migration. M3 adds a persistent,
identity-safe OVS bridge lifecycle for version 2. M4 adds container veth
attachment through OVS, resolving each workload by its Compose project and
service labels and recording its full container ID and network-namespace inode.
M5 adds owned router namespaces, namespace veths, mirrors, forwarding, routes,
nftables policy, semantic profile flows, and migrated OVS laboratories.
M6 adds GraphX-owned TAP lifecycle, exact UID/GID access for non-root QEMU,
access/trunk VLAN realization, QMP pause/resume, and OVS SPAN evidence. Version 1
and QEMU user networking remain compatibility paths. M7 adds bounded declarative Ethernet
capture, timed netem ownership, safe snapshot export, and
policy/route/link/attachment/application diagnostics. M8 retires version-1
infrastructure execution, Docker-driver realization, the Docker Desktop
simulation, and imperative fault mutation while retaining deterministic migration.

## 1. Scope and architectural principles

This document consolidates the implemented architecture, accepted architecture decision records, checked-in examples, and current operational documentation. It describes implementation rather than aspirations unless a subsection is explicitly marked **Proposed**.

The design follows these principles:

- **One authoritative model.** `graphx.yaml` is executable configuration, not illustrative documentation.
- **Separation of concerns.** Logical topology, transport, infrastructure, deployment, observation, storage, and control are correlated without being collapsed into one layer.
- **Bounded resource use.** Queues, frames, datagrams, retries, history, capture, API work, and shutdown waits have explicit limits.
- **Fail closed at trust boundaries.** Invalid configuration, credentials, raw-edge transport requests, unsafe capture files, and unsupported QEMU accelerators are rejected.
- **Best-effort observation must not block data processing.** Telemetry, OTLP export, capture, and history degrade independently of graph execution.
- **Platform truthfulness.** Linux and Lima results are reported separately, and TCG evidence is never presented as KVM evidence.
- **Evidence over inference.** VM acceleration, guest readiness, graph readiness, and service readiness are separate states with separate evidence.

## 2. Architectural evolution and decisions

The present design evolved through the following accepted decisions.

| Decision | Architectural effect | Current manifestation |
|---|---|---|
| Authoritative versioned configuration | Eliminated drift between descriptive YAML, runtime construction, and GUI topology | Strict config v1/v2 model, JSON Schema, semantic validation, migration, overrides, topology normalization |
| Network infrastructure as a peer layer | Separated logical edges from L2/L3 realization and Compose lifetime | `network` objects, `graphx infra`, external networks, edge paths, OVS/router laboratories |
| Typed receive outcomes and bounded runtime | Distinguished idle, peer completion, cancellation, and failure | `ReceiveResult`; bounded queues, deadlines, close wakeups, idempotent close |
| Envelope v2 identities | Added stable message, trace, and causal-parent identities without breaking v1 | v2 writers, v1/v2 readers, deduplication key, capture/telemetry correlation |
| Quality gates and fuzzing | Made malformed-input and lifecycle behavior a release concern | CTest, sanitizer, static-analysis, fuzz, package, security, and platform profiles |
| Explicit security boundaries | Separated data-plane TLS, telemetry authentication, observation, and control | TLS 1.3/mTLS, HMAC/replay checks, bearer roles, strict request limits, hardened containers |
| Centralized bounded operations telemetry | Kept remote exporter credentials out of nodes and distinguished service from graph health | Telemetry service OTLP, Prometheus, Grafana, SLO window, separate readiness endpoints |
| Isolated SQLite telemetry history | Added durable metadata without placing storage in the graph data path | Dedicated worker, bounded queues, retention, WAL, read-only observation API |
| Authorized runtime control plane | Replaced optimistic pause/resume with attributable, acknowledged commands | Policy principals, per-node identities, idempotency, expiry, audit, signed ACKs |
| Bounded PCAPNG, Lua, and extcap | Made captures operable and safer without changing the wire protocol | USER0 application capture, Ethernet capture, Lua dissector, validating live-follow extcap |
| Immutable release and compatibility surfaces | Established the 1.0.0 product/package contract | Authoritative `VERSION`, packages, manifests, checksums, SBOMs, compatibility policy |
| Bounded IPv4 UDP edges | Added unicast, broadcast, and multicast while retaining framing and bounds | UDP transport, typed anomaly counters, three focused examples |
| Unified QEMU profiles | Modeled raw network nodes consistently across host and Linux-container execution | Shared guest/observer/UI, external and container profiles, QMP and probe evidence |
| Explicit manual-route activation | Kept teaching-state transitions declared, reviewable, and narrow | `install: manual`, exact `graphx infra route apply/clear`, strict evidence projection |
| Lima macOS execution layer | Moved privileged Linux development behind a reproducible Apple Silicon VM boundary | Pinned M1 template, rootful Docker/system OVS provisioning, disposable primitive verifier |
| OVS semantic network profiles | Accepted OVS as the future single backend while preserving user intent | M2 config v2 profiles and typed attachments; M3 identity-owned bridges; M4 managed-container veth endpoints; M5 namespace veths, mirrors, router state, profile flows, and migrated labs; M6 owned QEMU TAP while v1 Docker drivers continue |

The UDP decision is canonical ADR 0012 and the unified QEMU profiles decision is
canonical ADR 0013. The decision index records that QEMU was initially assigned
0012, preserving the historical mapping without retaining two current records
with the same number.

## 3. System context and architectural planes

```mermaid
flowchart LR
  C[graphx.yaml] --> V[Strict loader and schema]
  V --> G[Logical graph]
  V --> T[Transport model]
  V --> N[Network infrastructure]
  V --> D[Deployment]
  V --> O[Observability and control]
  G --> R[Node runtimes]
  T --> R
  N --> I[graphx infra / platform networking]
  D --> P[Processes / Compose / QEMU]
  R --> U[Authenticated UDP telemetry]
  U --> S[Telemetry service]
  S --> W[WebSocket and HTTP APIs]
  W --> GUI[GraphX browser console]
  S --> M[Prometheus / Grafana]
  S --> H[SQLite history worker]
  S --> X[OTLP exporter]
  R --> A[Application PCAPNG]
  I --> E[Ethernet PCAPNG / SPAN]
  A --> GUI
  E --> GUI
```

### 3.1 Data plane

The data plane carries application work over a configured edge. GraphX-aware edges transport an `Envelope`; external edges carry protocol-native bytes and are descriptive/observable only. Data-plane execution is independent of the telemetry service: telemetry loss cannot stop a graph edge.

### 3.2 Infrastructure plane

The infrastructure plane realizes address domains, interfaces, switching, routing, filtering, mirroring, and impairment. It can be absent for local transports and simple Docker bridge deployments. Its lifecycle is explicitly separate from application deployment so external networks and network devices can survive Compose project restarts.

### 3.3 Observation plane

The observation plane receives normalized, bounded events, computes live state and derived signals, exports metrics/traces, persists optional metadata, and catalogs capture artifacts. It reports what the collector observed; best-effort UDP telemetry is not an accounting protocol.

### 3.4 Control plane

The control plane issues bounded commands to explicitly controllable nodes. Commands are authenticated, authorized, targeted, expiring, idempotent, and acknowledged. Pause stops a source from producing new messages while in-flight work drains; reset is collector-local; neither operation suspends a process or rewinds durable history.

### 3.5 Presentation plane

The React Flow browser console provides Application, Network, History, and capture/inspection experiences from the telemetry API and WebSocket stream. Prometheus and Grafana provide the operations-oriented view. Wireshark, TShark, and extcap provide packet/envelope analysis.

## 4. Authoritative configuration and deployment model

### 4.1 Configuration structure

Configuration versions 1 and 2 share the following top-level organization:

| Surface | Purpose | Representative objects |
|---|---|---|
| `graph` | Logical computation | graph ID, nodes, typed ports, directed edges |
| `transport` | Per-edge communication | TCP, UDP, Unix, shared memory, in-process settings |
| `network` | L2/L3 realization and path | v1 drivers/interfaces or v2 profiles/attachments, switches, routers, edge paths |
| `deployment` | Placement and packaging | service image, command, telemetry service/port |
| `observability` | Signals, export, capture, SLO, history, control limits | telemetry endpoint, PCAPNG, OTLP, SQLite, bounded command settings |

Unknown keys on core surfaces are rejected. Version 2 rejects legacy `driver`, `parent`, `mode`, `network.interfaces`, and `deployment.network` fields. Its fixed profiles define MAC identity, learning/filtering, ARP, broadcast/multicast, routing, isolation, and management behavior; its typed attachments distinguish container veth, namespace veth, QEMU TAP, external, and mirror intent. Validation includes reference integrity, port direction/schema compatibility, transport/edge consistency, DAG constraints, addresses and subnet membership, MAC and VLAN syntax, mirror outputs, router attachments, and edge-path hops. A file is limited to 1 MiB; the model limits nodes, edges, and ports to bounded counts.

Overrides use dotted paths. Precedence is:

1. checked-in configuration file;
2. `GRAPHX_OVERRIDES`;
3. explicit CLI `--set` values.

The top-level source `version` is immutable and cannot be changed by either
override layer.

Secrets do not belong in `graphx.yaml`. Compose secret mounts and deployment environment variables project observation tokens, control policies/tokens, runtime identities, shared HMAC secrets, TLS material, OTLP credentials, and optional organizational build trust.

### 4.2 Logical nodes and ports

A node has an ID, kind, runtime, execution location, lifecycle owner, control classification, optional accelerator and architecture, and typed input/output ports. These fields let the same GUI represent a GraphX process, Docker service, host program, or QEMU VM without pretending they have the same runtime contract.

An edge references `from` and `to` ports, a transport name, and a data-plane classification. Both configuration versions require the GraphX-managed data plane to be a directed acyclic graph because the current blocking startup/lifecycle model cannot safely schedule feedback cycles. External raw edges are descriptive rather than scheduled and may form a physical control/data loop; ADR 0014 records that narrow exception.

### 4.3 Deployment

Deployment metadata maps node IDs to images and commands. It never enters `Node`, `Edge`, or `Transport`. Version 2 also declares the Compose project identity used to resolve a unique running service container without trusting a mutable container name. The standard demo runs generator, transform, sink, and telemetry as hardened Compose services on a private management bridge. M4 data-plane interfaces are separate veth peers attached only to OVS. QEMU changes the placement model without changing the logical application.

Common container defaults are read-only root filesystems, small tmpfs mounts, all capabilities dropped, `no-new-privileges`, PID limits, an init process, bounded logs, and loopback-only published management ports. Linux QEMU KVM mode adds only `/dev/kvm` and its group.

### 4.4 Lifecycle boundaries

The CLI validates, inspects, projects, normalizes, and migrates both configuration versions. Every version-1 infrastructure action is rejected before state or platform mutation. For version 2, M3 provides persistent OVS bridge ownership, M4 extends the lifecycle to container veth attachment, M5 adds namespace veths, mirrors, router forwarding/routes/policy and semantic profile flows, M6 adds persistent TAP creation, and M7 adds owned capture and timed faults. Compose manages application processes and management connectivity only; GraphX verifies project/service labels, image, full container ID, PID, and namespace inode before moving the data-plane peer. A replacement namespace, TAP, Port, Interface, capture, or qdisc is reported unhealthy and cleanup fails closed.

Infrastructure provisioning is designed for clean laboratories. It does not persist desired state, reconcile drift, or guarantee rollback of every partial direct CLI create; the example launchers add preflight and cleanup behavior around this seam.

## 5. Logical graph and message protocol

### 5.1 Standard processing example

The root example is the canonical GraphX-aware pipeline:

```text
generator.samples -- TCP/u32be --> transform.samples
transform.transformed -- TCP/u32be --> sink.transformed
```

The generator creates `Sample` envelopes, the transform doubles the sample into `TransformedSample`, and the sink consumes it. This small graph is reused across bridge, shared-memory, macvlan, ipvlan, OVS, capture, telemetry, and GUI demonstrations so the transport or infrastructure change is visible without changing the application concept.

### 5.2 Envelope and framing

Every canonical GraphX frame is a four-byte unsigned big-endian length followed by a `GXE` envelope. The maximum envelope payload is 16 MiB for the stream protocol. Version 1 contains sequence, timestamp, type, legacy trace string, sorted attributes, and payload. Version 2 adds fixed 128-bit nonzero message and trace IDs plus an optional parent-message ID.

New roots emit v2. Readers accept v1 and v2. Normal forwarding, retry, and transformation retain message identity; explicit derivation creates a new message in the same trace with a causal parent. TCP complete-frame retry is at least once, so v2 `message_id` is the correct deduplication key.

### 5.3 Receive and shutdown semantics

Lifecycle-aware transports report `message`, `timeout`, `end_of_stream`, or `cancelled`. Protocol/transport failures remain contextual exceptions. Blocking operations have deadlines, `close()` is idempotent, and a control thread may close a transport to wake a blocked operation. The caller must keep the object alive until the operation returns.

## 6. Transport architecture

| Transport | Scope and representation | Backpressure/lifecycle | Main limitations | Example |
|---|---|---|---|---|
| In-process | One bounded mutex-protected FIFO per named channel | Block-with-deadline or immediate reject; FIFO drain; close wakes waiters | One process; endpoint settings must match | Configuration profile and unit tests |
| TCP | Cross-process/host framed byte stream; optional TLS 1.3/mTLS | Bounded connect/write; reconnect and exponential retry; typed receive | Retry is at least once; blocking runtime | Standard demo, all L2/L3 pipeline labs |
| Unix-domain socket | One-host framed stream | Nonblocking listener construction; timed accept/connect/write; cancellation socket | One listener peer for v1; local filesystem ownership | Transport tests/documented profile |
| Shared memory | POSIX mapped SPSC ring containing exact framed bytes | Fixed slots; block/reject; peer-PID checks; robust mutex on Linux | One producer/consumer; copy-based; IPC namespace considerations | `examples/shared-memory` |
| UDP | One framed envelope per IPv4 datagram | Bounded buffers/datagram; invalid datagrams dropped; cancellation socket | Loss, duplicates, reordering; no ACK, security, congestion control, or EOS | Unicast, broadcast, multicast examples |
| External raw TCP/UDP | Descriptive protocol-native bytes, `framing: none` | Owned by external application/profile | Not constructible by `TransportFactory`; passive evidence only | QEMU and SDR profiles |

### 6.1 TCP and security

TCP edges may enable TLS 1.3 and mutual TLS with CA, certificate, key, peer verification, client-certificate requirement, and server-name settings. Complete framed writes are bounded. A failed send may reconnect and retry the complete frame once. Control traffic that requires reliability and authentication should use TLS-protected TCP rather than GraphX UDP.

### 6.2 UDP modes

- **Unicast:** one IPv4 destination; the example uses loopback and five 1,400-byte-bounded messages.
- **Broadcast:** enables `SO_BROADCAST`; the portable example confines traffic to an internal Docker subnet, and native Linux acceptance uses two disposable namespaces on a bridge with no physical interface or default route.
- **Multicast:** joins/transmits in `224.0.0.0/4`; the example uses `239.255.42.1`, loopback, TTL 0, and address reuse. A diagnostic listener proves network fan-out, but the logical graph still has one subscriber.

Each datagram retains `u32be + GXE` so the decoder, application capture, and Lua dissector remain reusable. Frames are never fragmented by GraphX, although IP itself may fragment a large datagram. The implementation counts malformed, truncated, oversized, socket-error, sequence-gap, duplicate, and out-of-order events.

## 7. Network topology and infrastructure configuration

### 7.1 Why logical edges and network paths differ

A logical edge states application intent: producer port, consumer port, schema, and transport. A network path states realization: interfaces, address domains, switches, routers, and policy hops. One edge may traverse several infrastructure devices; several edges may share one network. Keeping both views prevents Docker membership from being mistaken for application connectivity.

The GUI correlates them through `network.edge_paths`, an ordered list whose first and last hops are the edge endpoints and whose intermediate hops reference configured infrastructure objects.

### 7.2 Network objects

| Object | Configuration responsibilities | Operational realization |
|---|---|---|
| Version-1 network | ID, driver, one/more subnets, gateway, parent, mode, ownership | Docker bridge/macvlan/ipvlan network |
| Version-1 node interface | Owner, network, IP/prefix, optional MAC | Container interface/IPAM attachment |
| Version-2 network | ID, fixed semantic profile, subnets, gateway, uplink, external intent | OVS-only intent; bridge realization begins in M3 and semantic flows are realized in M5 |
| Version-2 attachment | Kind, owner, network, address/MAC, interface/peer/switch and TAP UID/GID as applicable | Container veth is realized in M4; namespace veth, external boundary, and mirror intent in M5; QEMU TAP is realized in M6 |
| OVS switch | Bridge ID, datapath, ports, veth peer, VLAN access/trunk metadata, optional mirror | OVS bridge/ports and SPAN configuration |
| Router | Namespace/container kind, interfaces, addresses, forwarding, routes, policies | Linux netns or router container; IP forwarding and nftables |
| Edge path | Logical edge ID and ordered hops | Presentation/inspection correlation |
| Fault | Router/interface and delay, jitter, loss, optional rate | `tc netem` qdisc |

The loader validates IPv4 subnet membership, MAC syntax, VLAN ranges, references, attachment ownership, mirrors, and path connectivity before infrastructure commands run. Version-1 plans execute argument arrays without a shell and can be reviewed with `--dry-run`; version 2 cannot enter that planner. Source-to-source migration loads the literal version-1 file without runtime `GRAPHX_OVERRIDES`.

### 7.3 Docker bridge

The root demo uses one private bridge (`172.30.0.0/24`) with service-name DNS. It is the most portable container topology and requires no host privileges. It demonstrates application topology and transport behavior but does not expose unique LAN-visible MAC identities, independent routing domains, or host OVS switching.

### 7.4 Macvlan L2

The MACVLAN-semantic example attaches generator, transform, and sink to one
`10.30.0.0/24` OVS domain using explicit addresses and MACs. GraphX owns the
container veth attachments; Docker supplies management connectivity only. The
profile demonstrates LAN-like per-container L2 identity without creating a
Docker macvlan network.

### 7.5 IPvlan L2

The IPVLAN-L2-semantic example creates three independent `/24` OVS domains. A
namespace router connects the bridges and allows only generator-to-transform
and transform-to-sink policy flows. Container veth pairs provide the endpoint
attachments.

```mermaid
flowchart LR
  G[generator\n10.41.1.10] --> NG[gx-ipvl2-generator]
  NG --> SG[br-l2-gen\nOVS + SPAN]
  SG --> R[ipvlan-l2-router\nnamespace + nftables]
  R --> ST[br-l2-xform\nOVS + SPAN]
  ST --> NT[gx-ipvl2-transform]
  NT --> T[transform\n10.41.2.20]
  T --> NT
  R --> SS[br-l2-sink\nOVS + SPAN]
  SS --> NS[gx-ipvl2-sink]
  NS --> K[sink\n10.41.3.30]
```

This topology makes switching, routing, security policy, fault injection, and observation points explicit. M7 diagnostics identify policy, route, link, attachment, and application failure layers rather than collapsing them into generic disconnection.

### 7.6 IPvlan L3

The IPVLAN-L3-semantic example uses three subnets: `10.42.1.0/24`,
`10.42.2.0/24`, and `10.42.3.0/24`. OVS plus the GraphX-owned namespace router
realize broadcast-free routed behavior without a Docker ipvlan parent or data
network.

### 7.7 Mixed macvlan/ipvlan with OVS and routing

The mixed reference topology demonstrates a full cross-domain path:

```mermaid
flowchart LR
  G[generator] --> M[gx-mac-domain]
  M --> OM[br-gx-mac]
  OM --> R[domain-router]
  R --> OI[br-gx-ipv]
  OI --> I[gx-ipv-domain]
  I --> T[transform]
  T --> S[sink]
```

```text
generator 10.10.0.10 / explicit MAC
  -> gx-mac-domain (macvlan L2)
  -> br-gx-mac (OVS system datapath + SPAN)
  -> gx-router (10.10.0.1 and 10.20.0.1, forwarding, nftables, netem)
  -> br-gx-ipv (OVS system datapath + SPAN)
  -> gx-ipv-domain (ipvlan L2)
  -> transform 10.20.0.20
  -> sink 10.20.0.30
```

The first logical edge crosses both domains; the second remains inside the
IPVLAN-semantic domain. This contrast shows why each logical edge owns a
distinct `edge_path`. One Compose project manages the processes and its private
management network; GraphX owns the OVS data plane.

### 7.8 Switching, VLANs, mirrors, policies, and faults

Only Open vSwitch is presently modeled. Ports may identify a host interface/veth peer and carry access-tag or trunk metadata. A switch can mirror all selected traffic to an output port. Standard Ethernet capture should occur on those SPAN interfaces, not through the GraphX application capture writer.

Routers can be Linux namespaces or containers. They expose named interfaces, routes, forwarding, and backend-neutral source/destination/action policies realized with nftables in the native implementation. M7 declarations apply bounded delay, jitter, loss, and rate behavior to a selected realized endpoint for a mandatory duration; the recorded netem qdisc self-expires and remains exactly cleanable.

### 7.9 Static-route and deny-policy laboratory

The Phase 14 `examples/static-route-policy` laboratory is the focused route and
policy reference. Three OVS-backed Layer-2 domains meet at one namespace router.
The left-to-middle flow is receiver-confirmed, the reverse flow is denied by a
named nftables rule and counter, and the left-to-right diagnostic address is
unreachable until an exact declared route is applied.

```text
left 10.64.1.10   -> br-route-left   --+
middle 10.64.2.10 -> br-route-middle --+-> gx-route-router -> br-route-right -> right
                                                                     10.64.30.10/32
```

Routes normally install during infrastructure creation. A route with
`install: manual` remains part of the validated model and dry-run plan surface,
but `create` omits it. The CLI may then apply or clear only the exact destination
declared for the selected router. This is deliberately narrower than accepting
arbitrary route arguments from the browser or shell environment.

Each bridge mirrors traffic to a dedicated capture veth. The lab records a
bounded, user-readable Ethernet PCAPNG and a strict, atomically replaced JSON
evidence projection. Telemetry accepts only known edge IDs and the four states
`allowed`, `policy-denied`, `missing-route`, and `route-applied`; the Application
and Network views color every logical/path edge consistently and show the
evidence type in the inspector. Receiver results and nftables/kernel state are
authoritative; capture and GUI are corroborating views.

The native launcher refuses occupied fixed names and address ranges, marks OVS
and namespace resources with a random run owner, checks those markers before
cleanup, retains evidence, and supports repeated stop. Portable inspection
validates configuration and exact command generation only. Native Linux remains
required to accept routing, policy, mirroring, delivery, and host-isolation
claims.

### 7.10 Native Linux and macOS execution

| Capability | Native Linux | macOS with Lima |
|---|---|---|
| MACVLAN/IPVLAN | OVS semantic profiles | Same OVS semantic profiles in guest |
| OVS datapath | Host system datapath | Lima guest system datapath |
| Router | Linux namespace | Lima guest Linux namespace |
| veth/TAP | Host kernel | Lima guest kernel |
| SPAN capture | Host-local capture interfaces | VM-local capture interfaces and storage |
| Evidence | Native platform row | Lima ARM64 platform row |

OrbStack runs unprivileged application demonstrations on macOS, but it is not a
privileged network-infrastructure backend.

## 8. QEMU connectivity architecture

### 8.1 Logical contract

All profiles preserve the same guest application and four raw TCP/UDP directions:

```text
                 TCP :18001
host-origin  ----------------->  qemu-node
     |                               |
     +----------- UDP :18001 ------> |
                                     |
                 TCP :19001          v
host-receiver <----------------- qemu guest application
host-receiver <------ UDP :19001
```

The guest does not link GraphX and does not exchange GraphX envelopes. Edges are `data_plane: external` with `framing: none`; they are validated and visualized but cannot be instantiated by `TransportFactory`. This is the pattern for incorporating a pre-existing network appliance, VM, SDR, or hardware node into a GraphX topology without rewriting it.

### 8.2 Shared guest and evidence

All profiles use the same reproducible Buildroot x86_64 guest, kernel, root filesystem, `qemu-network-node` application, deterministic host peers, packet observer, telemetry semantics, and browser integration. Startup verifies an artifact manifest and records SHA-256 hashes.

QMP and application probes answer different questions:

- QMP `query-status` proves VM execution state.
- QMP `query-kvm` proves whether KVM is present and enabled.
- Separate TCP and UDP probes on private port 18002 prove the guest application is responding.
- Data-plane traffic remains on port 18001 and cannot be consumed by readiness probes.

The GUI reports requested, launcher-selected, and QMP-proven accelerators separately. Runtime states include `not-started`, `booting`, `ready`, `degraded`, `stopped`, and `unavailable`. A QMP-paused VM remains visible as paused while the guest and protocol probes become unavailable; fresh probes restore readiness after resume.

### 8.3 External QEMU profile

In the deprecated portable compatibility profile, QEMU and the packet observer
run on the host; origin, receiver, telemetry, and GUI run in Docker. Host-side
observation avoids container-runtime bind-mount cache delay while a PCAP grows.

Origin reaches the guest through a QEMU host forward at
`host.docker.internal:18001`. The guest reaches the receiver at the slirp
gateway `10.0.2.2:19001`. On macOS, OrbStack provides
`host.docker.internal` and host services bind to loopback. On Linux, the
hostname is pinned to the private `172.30.12.1` demo bridge gateway. This is a
deprecated compatibility profile; TAP/OVS is the default.

QMP is a private Unix socket in a mode-0700 state directory, used for evidence and graceful shutdown. Explicit KVM or HVF requests fail rather than silently falling back. Apple Silicon runs the x86_64 guest with TCG; HVF applies only to Intel macOS; Linux can select KVM when available.

### 8.4 Containerized QEMU profile

The Linux-only profile runs origin, receiver, QEMU, observer, telemetry, and GUI as Compose services. QEMU user-mode networking remains the common guest contract. A bounded relay in the QEMU container forwards guest egress from `10.0.2.2:19001` to the receiver service, preserving the guest image used by the external profile.

The default profile is unprivileged: read-only root, no Docker socket, no `NET_ADMIN`, no `/dev/net/tun`, no added capabilities, and a non-root UID/GID. KVM is an overlay that adds only `/dev/kvm` and the host KVM group. The private QMP socket lives in a mode-0700 tmpfs and is not shared with telemetry or the observer.

### 8.5 QEMU TAP and OVS profile

The M6 Linux profile, including macOS through Lima, connects the unchanged x86_64 guest to a GraphX-owned persistent TAP and system OVS bridge. The privileged infrastructure lifecycle records the TAP ifindex, alias, OVS Port and Interface UUIDs, owner UID/GID, VLAN metadata, and configuration/graph ownership markers. QEMU runs as dedicated UID/GID 65532 with access to that TAP only; it receives no general network-administration capability.

The profile proves TCP/UDP unicast, guest MAC learning, access-VLAN reachability, a distinct VLAN's isolation, broadcast/multicast forwarding, and OVS SPAN capture growth. QMP independently confirms TCG runtime state and pause/resume transitions. On Apple Silicon, x86_64 TCG is reported as TCG rather than KVM. Runtime evidence stays on the Lima-native filesystem under `/var/lib/graphx/qemu/m6`.

### 8.6 QEMU capture and history

QEMU produces a bounded source PCAP. The shared passive observer tolerates an incomplete final record, rejects impossible lengths/timestamps, skips bounded malformed records, and converts valid packets to standard Ethernet PCAPNG. It also emits `network_packet` telemetry and stores packet metadata in a separate SQLite history service.

Defaults are 64 MiB/100,000 PCAPNG packets, a 64 MiB source PCAP ceiling, 50,000 history records, one-day retention, a 64 MiB packet-history database, and a 64-byte payload preview. Reaching the source-capture limit stops QEMU instead of permitting unbounded storage. Payload preview is bounded metadata, while the downloadable capture contains packet bytes and must be protected accordingly.

### 8.7 QEMU compatibility and remaining limits

The external and container slirp profiles remain compatibility options. Physical-network attachment and guest-native GraphX integration are not implemented. M6 exposes bounded QMP VM pause/resume in its launcher, but general guest control remains outside the GraphX runtime control plane.

## 9. Observability, metrics, history, capture, and GUI

### 9.1 End-to-end information flow

```mermaid
flowchart LR
  R[GraphX runtime callbacks] --> U[Bounded HMAC UDP JSON]
  Q[QEMU packet observer] --> U
  U --> T[Telemetry service]
  T --> L[Live in-memory topology and counters]
  L --> WS[WebSocket snapshots]
  WS --> GUI[Application / Network GUI]
  T --> PM[Prometheus metrics]
  PM --> GF[Grafana dashboards and alerts]
  T --> OT[Bounded OTLP/HTTP exporter]
  T --> HW[Bounded history queue]
  HW --> SQL[SQLite metadata history]
  SQL --> HP[History API / GUI]
  R --> AP[USER0 application PCAPNG]
  Q --> EP[Ethernet PCAPNG]
  AP --> CP[Capture catalog/download]
  EP --> CP
  CP --> GUI
  AP --> WX[Wireshark Lua / extcap]
  EP --> WX
```

### 9.2 Live telemetry and GUI behavior

GraphX-aware runtimes publish send, receive, error, connection, reconnect, backpressure, processing, heartbeat, and UDP anomaly events. QEMU observation publishes explicitly typed `network_packet` events. The telemetry service normalizes topology from the same configuration and broadcasts bounded snapshots over WebSocket. The browser initially fetches `/api/topology`, then updates without refresh over the configured WebSocket path.

The GUI surfaces:

- **Application:** logical nodes, ports, edges, connection/runtime state, live counters, rate, latency, errors/drops, and recent correlated messages or packets.
- **Network:** infrastructure nodes and ordered edge paths; for QEMU, distinct host/container, VM, guest-application, and TCP/UDP readiness layers.
- **History:** newest-first bounded metadata with filters and cursors. Paging away from the newest page pauses live refresh until the operator returns.
- **Capture:** cataloged capture files and edge-oriented downloads. Application and Ethernet files are labeled by their actual DLT/representation.
- **Control:** control credential entry, permitted actions, command state, and acknowledgements. QEMU itself is shown as non-controllable; only the configured origin is paused/resumed.

Changing views does not alter the authoritative topology; layout is recalculated from the current model. Live count updates depend on WebSocket connectivity and observation authorization, while manual refresh uses the HTTP snapshot.

### 9.3 Metrics and SLOs

Per-edge metrics include messages and bytes by direction, five-second send rate, histogram-derived p95 receive latency, errors, drops, rejects, reconnects, backpressure events/time, connection state, and UDP anomaly counters. Per-node heartbeat events report CPU use as a percentage of one core.

The telemetry service distinguishes process liveness (`/api/live`), service readiness (`/api/ready`), graph readiness (`/api/graph/ready`), compatibility health (`/api/health`), and current SLO (`/api/slo`). Docker health uses service readiness so an application outage remains observable instead of restarting the collector.

The default SLO window is 300 seconds with a 10-second warm-up, 99% graph availability, maximum 1% error and drop ratios, and maximum 10 ms p95 latency. Status is `warming`, `met`, or `violated`. The window is in memory; durable history stores prior evaluations but does not replay them into a restarted live window.

Prometheus is the operations contract. The provisioned Grafana dashboard covers readiness, SLO, throughput, latency, errors/drops, CPU, OTLP, history, and control. Alerts cover sustained unavailability, SLO violations, exporter loss, history failure, invalid control policy, and command timeout.

### 9.4 OTLP export

The native C++ exporter is an optional bounded loopback-only seam. Remote OTLP belongs at the telemetry service, where HTTPS, bearer authentication, private CA, and optional mutual TLS can be configured without distributing exporter credentials to graph nodes.

Queues are bounded by count and encoded bytes. Requests have absolute deadlines, capped responses, bounded exponential retry with jitter, and retry only for selected connection/HTTP failures. Export remains best effort and does not replace durable history. GraphX v2 trace IDs map directly to OTLP trace IDs; each operation receives a fresh span ID. Full SDK behavior, W3C propagation, sampling, and span-parent construction remain outside the current adapter.

### 9.5 Durable history

SQLite history is optional in the core model and enabled by default by the guided demo with smaller limits. A dedicated Node worker exclusively owns the database. The main event loop communicates through bounded write/query queues; it never performs SQLite work directly. WAL mode, full synchronous durability, batching, age/count retention, database page bounds, query limits/deadlines, and bounded shutdown are explicit.

History stores metadata such as graph/event/node/edge IDs, sequence, trace/message IDs, latency, bytes, CPU, SLO evaluations, and authorized control-audit records. It excludes application bodies, capture contents, and configured credentials. Backend state (`starting`, `ready`, `degraded`, `closed`) is independent from service readiness. Overload drops newest history offers and increments a metric rather than blocking the telemetry loop.

QEMU packet history is intentionally separate from GraphX message history. It preserves protocol/address/length/truncation and bounded preview fields without claiming that raw packets were GraphX envelopes.

### 9.6 Application PCAPNG

The GraphX capture sink writes one file per node using PCAPNG `LINKTYPE_USER0` (147). Each Enhanced Packet Block contains the exact canonical `u32be + GXE` frame and a bounded JSON comment with edge, direction, wire version, sequence, message/parent/trace IDs, and type. The matching telemetry capture event carries filename, packet index, and byte offset.

Files have explicit snap, byte, and packet limits. Complete blocks are committed atomically. Unsafe symlinks, hard links, special files, truncation races, and unsupported DLTs are rejected through descriptor-based validation. Capture failure stops capture for that process, not graph traffic. There is no automatic rotation; a new directory or operator-managed archive is required.

### 9.7 Ethernet PCAPNG and OVS capture

`EthernetPcapngCaptureSink` and the QEMU observer write actual IEEE 802.3 frames with `LINKTYPE_ETHERNET` (1). Native network labs use OVS SPAN interfaces with `tcpdump` for live inspection or `dumpcap` for PCAPNG. These files explain routing, switching, VLAN, retransmission, broadcast/multicast, and impairment behavior that application capture cannot show.

Application and Ethernet captures are complementary, not interchangeable. Automated packet matching between them is not implemented. Today the operator correlates using time, edge/path, identities where available, endpoints, and visible payload/protocol attributes.

### 9.8 Wireshark, TShark, and extcap

The Lua dissector self-registers on USER0 and decodes v1/v2 GraphX frames with boundary checks, identity checks, unique attribute keys, and expert diagnostics. It should be installed in a GraphX-specific profile because USER0 is private-use and can conflict with another local protocol.

The Python extcap adapter exposes separate GraphX application (DLT 147) and Ethernet (DLT 1) interfaces. It validates PCAPNG sections, a single interface, block sizes/trailers, DLT, and complete blocks before copying or following a file. It rejects unsupported capture filters; display filters belong in Wireshark. The QEMU inspection helper streams captures to TShark to work around confined Linux packages that reject direct workspace paths.

### 9.9 Security and access relationship

Observation and control are separate credentials. Observation protects topology details, WebSocket data, metrics, history, capture catalog/downloads, and SLO state when configured. Control principals have explicit node/action scopes and separate audit permissions. Telemetry datagrams and runtime acknowledgements use HMAC with replay and clock checks; policy mode gives each node a distinct runtime secret.

Credential rotation uses atomic current snapshots plus a bounded redaction-only overlap. Retired credentials never authenticate. Input is normalized before fan-out to live state, OTLP, capture metadata, history, audit, or logs. Raw capture payloads are not sanitized and require stronger operational protection than metadata telemetry.

## 10. Example-to-concept map

| Example | Architectural concepts illustrated | Platform/privilege |
|---|---|---|
| Standard TCP Docker demo | Authoritative config, DAG, framed TCP, service DNS, live GUI, control, capture, history | OrbStack or Linux Docker Engine; unprivileged |
| Standalone capture | Application PCAPNG, correlation comments, USER0/Wireshark | Local build; unprivileged |
| Shared memory | Separate processes, bounded SPSC rings, local IPC lifecycle/backpressure | Linux/macOS; unprivileged |
| UDP unicast | One framed envelope per datagram, loss-tolerant semantics | Loopback; unprivileged |
| UDP multicast | Local multicast membership and diagnostic fan-out | Loopback; unprivileged |
| UDP broadcast | Isolated directed broadcast; native namespace proof | Docker portable; privileged native Linux acceptance |
| MACVLAN semantics | Explicit L2 IP/MAC identity on OVS with container veth | Native Linux or Lima; privileged infrastructure |
| IPVLAN L2 semantics | Independent L2 domains, OVS bridges, namespace routing/policy | Native Linux or Lima; privileged infrastructure |
| IPVLAN L3 semantics | Routed subnets with broadcast-free profile behavior | Native Linux or Lima; privileged infrastructure |
| Mixed network | Cross-domain routing, OVS, mirrors, nftables, netem, edge paths | Native Linux or Lima |
| Static route/policy | Three OVS domains, ordered allow/deny rules, explicit route transition, GUI diagnostics | Native Linux; privileged infrastructure lifecycle |
| External QEMU | External raw node, host QEMU, slirp, passive observation, portable GUI | macOS/Linux; host QEMU + Docker |
| Container QEMU | Nested VM deployment, KVM/TCG evidence, least privilege, packet history | Linux x86_64; optional `/dev/kvm` |
| QEMU TAP and OVS | Non-root QEMU, owned TAP/OVS/VLAN lifecycle, QMP, SPAN evidence | Linux; Lima on Apple Silicon uses x86_64 TCG |
| Simulated SDR | Raw UDP IQ, mutual-TLS control, raw results, live GUI, packet history | OrbStack or Linux Docker Engine; unprivileged except capture sidecar capabilities |
| External SDR | External hardware boundary, macvlan, OVS/SPAN, ordinary Ethernet capture | Native Linux; privileged infrastructure lifecycle |

### 10.1 How examples, demos, and tests relate to the system

Examples, demos, and tests are not parallel implementations of GraphX. They are
different evidence layers over the same configuration, runtime, infrastructure,
observation, and presentation components:

```mermaid
flowchart TB
  M[Authoritative graphx.yaml model] --> L[Library, CLI, and node runtimes]
  M --> T[Telemetry and browser topology]
  M --> I[Infrastructure plans]
  E[Example configurations] --> M
  D[Demo lifecycle scripts] --> L
  D --> T
  D --> I
  U[Unit and contract tests] --> M
  U --> L
  P[Portable integration profiles] --> D
  P --> T
  N[Native Linux acceptance] --> D
  N --> I
  Q[Quality, sanitizer, and fuzz gates] --> L
  R[Verification report] --> U
  R --> P
  R --> N
  R --> Q
```

- An **example** is a checked-in configuration plus the smallest code or
  Compose assets needed to illustrate one architectural concern.
- A **demo** is an operator-facing lifecycle around one or more examples. It
  starts real components, exposes observable success criteria, and owns cleanup.
- A **test** checks a bounded contract. Unit tests isolate implementation;
  portable integration runs real host/Docker behavior; native-Linux acceptance
  proves kernel and host-network semantics that cannot be simulated faithfully.
- A **verification profile** aggregates tests but does not change what they
  prove. A report must retain platform, commit, logs, manual evidence, and skips.

### 10.2 Component and evidence traceability

| System component | Primary examples or demos | Automated evidence | Manual/system evidence |
| --- | --- | --- | --- |
| Configuration loader, schema, projections | Every `graphx.yaml`; root demo | `graphx-config-*`, schema/AJV, projection and documentation CTests | `graphx validate`, `inspect`, and reviewed infra dry-runs |
| Envelope, framing, identity | Standard TCP, capture, shared memory, UDP | C++ unit/golden tests, malformed cases, envelope/frame fuzzers | USER0 PCAPNG decoded with Lua/extcap |
| Transport factory and lifecycle | Standard TCP, shared memory, UDP examples | CTest transport suites, finite pipelines, SIGTERM and reconnect checks | Process output, delivery counts, clean shutdown |
| Infrastructure planner/executor | Native network labs, route/policy lab | Config/planner/transaction tests and portable dry-run assertions | Native links, OVS, namespace, route, policy, netem, and cleanup inspection |
| Deployment and ownership | Root Compose, separate network projects, QEMU/SDR launchers | Compose rendering, hardening and lifecycle script tests | Container/device boundaries, ownership markers, repeated teardown |
| Telemetry and operations | Root, QEMU, SDR, route/policy demos | Node tests, HTTP/WebSocket integration, Prometheus/SLO/OTLP checks | Live counters, readiness, bounded failure behavior |
| Control and security | Root source control, SDR mTLS relay | Authentication, authorization, anti-replay, rotation, redaction and audit tests | Token workflow, pause/resume, direct SDR command rejection/success |
| History | Root, QEMU, SDR | SQLite worker, retention, restart, query/auth tests | History navigation and persistence across collector restart |
| Application capture | Root and capture example | PCAPNG writer, security, catalog, download, Lua/extcap tests | Message identity and frame inspection |
| Ethernet capture | Native OVS labs, QEMU, SDR, route/policy | Observer/parser tests and portable simulated captures | SPAN/live capture, TShark decode, receiver/counter correlation |
| Browser presentation | Root and every graphical demo | React/node component tests and production build | Application/Network/History transitions and live updates without refresh |
| External-node model | QEMU and SDR | Config, raw-edge rejection, observer, QMP/probe, SDR protocol tests | VM acceleration/readiness or native SDR-path evidence |

The shortest path from architecture to evidence is therefore:

1. the architecture and ADR define a boundary;
2. `graphx.yaml` expresses it in an example;
3. a demo makes the example observable and repeatably cleanable;
4. focused tests validate individual contracts;
5. portable and native profiles establish platform-appropriate integration;
6. an independent report states exactly which evidence ran.

The consolidated [`demo guide`](demo-guide.md) describes the runnable scenarios.
The [`manual test procedures`](manual-test-procedures.md) define macOS and Linux
system acceptance without conflating Lima ARM64, native Linux, TCG, or KVM
evidence.

## 11. Architectural limits, drift, and risks

### 11.1 Current limits

- Configuration v1 rejects cycles in the GraphX-managed data plane and native one-to-many graph edges. Descriptive external raw edges may form device data/control loops.
- General infrastructure tooling does not reconcile state or persist ownership metadata; the Phase 14 laboratory adds local run-scoped ownership markers without changing that general contract.
- UDP supports IPv4 only and provides no DTLS, retransmission, congestion control, fragmentation/reassembly, or peer authorization.
- Shared memory is SPSC, fixed-size, copy-based, and sensitive to IPC namespace design.
- Unix-domain transport accepts one peer for its v1 listener lifetime.
- Telemetry and control are single-collector; pending commands do not survive restart.
- Application USER0 capture stops at its limit; M7 Ethernet mirror capture uses
  a bounded rotating ring and retains sealed sessions for an external evidence
  retention workflow.
- GraphX application PCAPNG uses private USER0 and requires a dedicated Wireshark profile.
- OVS Ethernet and GraphX application captures are not automatically correlated.
- QEMU slirp compatibility profiles do not model physical L2; the M6 TAP profile covers OVS L2/VLAN behavior but not physical-network attachment or guest-native GraphX control.
- OrbStack is not a privileged network-lab backend; macOS uses Lima.

### 11.2 Documentation and model consistency

- The UDP decision is canonical ADR 0012 and the QEMU decision is canonical ADR 0013; the ADR index retains the historical collision note.
- Top-level projection files under `config/` are explicitly non-authoritative and are generated or checked with `graphx project` from validated `graphx.yaml`.
- Root documentation identifies GraphX 1.1.0 and distinguishes accepted Phase 3–13 reports from the Phase 1 and Phase 2 documentary gaps; Phase 14 awaits native verification.
- Some examples provide live GUI metrics while topology-only examples provide only static visualization; the distinction should remain visible in every example README.
- Infrastructure routes support create-time and explicit manual activation; the Phase 14 laboratory is the focused coverage. Reconciliation and arbitrary runtime route mutation remain out of scope.

## 12. Proposed additional examples

The following are proposals, not current capabilities.

### 12.1 Single SDR → switch → processor → sink (implemented in Phase 13)

The `examples/sdr-node` suite models one external Ethernet-connected SDR sending UDP IQ/sample blocks to one containerized processor, with a mutual-TLS TCP control edge back to the SDR and a result edge to a sink. Its native-Linux profile places OVS and SPAN on the data path; its portable profile clearly models Docker bridge switching as a simulation. Both reuse one deterministic endpoint and packet observer and feed Ethernet PCAPNG, bounded packet history, live telemetry, capture download, and the existing GUI.

Delivered profiles and deferred extension:

1. simulated SDR container using raw UDP data and authenticated TCP control;
2. external-device contract plus native Linux namespace/OVS/SPAN verifier;
3. optional QEMU-based SDR emulator profile remains a later enhancement.

### 12.2 Routed multicast receiver set

**Priority: high.** Extend the multicast laboratory across two subnets with an IGMP-aware OVS/router setup and multiple diagnostic receivers. Keep the logical edge limitation explicit at first, then use the example as acceptance evidence for a future one-to-many graph-edge ADR.

### 12.3 Static routes and deny-policy laboratory (implemented in Phase 14)

The `examples/static-route-policy` lab now supplies three routed domains, a
receiver-confirmed intended flow, an nftables-counter-confirmed denied flow, and
a missing-route transition controlled by one declared manual route. It exposes
ordered paths, OVS mirrors, capture, and distinct GUI diagnostics. Portable
coverage is automated; independent native-Linux acceptance remains the Phase 14
exit gate.

### 12.4 Dual capture correlation laboratory

**Priority: medium.** Run the standard TCP pipeline across OVS while recording both GraphX USER0 frames and Ethernet frames. Generate a correlation report keyed by message ID, timestamp window, edge, endpoint, and frame length. This would prototype the currently deferred automated cross-file matching.

### 12.5 QEMU TAP profile

**Status: implemented in M6.** ADR 0016 places the Apple Silicon macOS runtime
in Lima and ADR 0017 selects OVS with an owned TAP as the QEMU data plane. The
profile proves guest MAC, VLAN isolation, multicast/broadcast forwarding, QMP
pause/resume, and direct SPAN capture while QEMU runs as a dedicated non-root
identity. The user-network profiles are deprecated compatibility paths after M8 closure.

### 12.6 Degraded observability laboratory

**Priority: later.** Intentionally fill history queues, reach capture limits, interrupt OTLP, rotate credentials, and sever graph edges while proving that graph traffic, service readiness, graph readiness, SLO status, and GUI diagnostics remain distinct.

## 13. Recommended architectural roadmap

### Accepted Lima and OVS migration

ADR 0016 and ADR 0017 accept a new migration track. M1 supplies the Linux
execution boundary, M2 supplies the version-2 configuration boundary, M3
supplies persistent identity-safe OVS bridge ownership, M4 supplies
restart-aware container veth attachment through OVS, M5 adds Linux router
namespaces, namespace veths, mirrors, policy, semantic IPvlan flows, and
migrated laboratory configurations, and M6 adds owned QEMU TAP/OVS attachment.
Migration work packages use `M` identifiers so they do not collide with the
existing feature-phase history. The completed sequence and its original work
packages are preserved in [`archive/`](archive/README.md); current rules are
maintained in [`project-decisions.md`](project-decisions.md):

1. M0 records the decisions and frozen GraphX 1.1.0 baseline.
2. M1 provides the Lima macOS Linux execution environment.
3. M2 implements configuration version 2, packet-verifiable MACVLAN/IPVLAN
   semantic profiles, typed attachments, and deterministic migration without
   changing version-1 meaning.
4. M3 implements locked persistent ownership state and an identity-safe OVS
   bridge lifecycle; M4 extends it to managed-container veth attachment.
5. M5 migrates the existing network and external-device laboratories.
6. M6 adds the owned QEMU TAP/OVS profile.
7. M7 integrates capture, faults, and diagnostics with the common lifecycle.
8. M8 retires legacy realization paths after the compatibility gate while retaining migration input.

During the compatibility window, version-1 files remain available only for
validation, inspection, projection, normalization, and migration. Version-2 create, status, destroy, and interrupted-create
recovery use the ownership ledger. Managed containers use veth without Docker
data-plane networks; M5 router namespaces, mirrors, policy, and IPvlan flows use
the same system-OVS path on native Linux and in Lima. M6 QEMU uses an owned TAP
on that path. M7 integrates declarative mirror capture and timed netem
ownership on that same identity-checked lifecycle.

### Completed documentation and consistency foundation

1. The QEMU decision is ADR 0013, with its former number recorded in the canonical ADR index.
2. Root maturity wording matches 1.1.0 and qualifies the accepted Phase 3–13 verification record.
3. Root and contributor documentation link the architecture source, editable DOCX, ADR index, and focused references.
4. `graphx project` generates or verifies the four projections under `config/`; CTest enforces the drift check in local and Linux/macOS CI builds.

### Future architectural changes

New features must preserve the current decision guide and accepted ADRs. A
change to the configuration authority, OVS-only backend, macOS OrbStack/Lima
split, compatibility window, or identity-safe ownership boundary requires a
new or superseding ADR before implementation. IPv6, authenticated UDP/DTLS,
graph fan-out, physical-device attachment, and distributed telemetry/control
remain separate decisions rather than implied extensions of M0–M8.

## Appendix A. Authoritative source map

| Concern | Primary implementation or documentation evidence |
|---|---|
| Architecture decisions | `docs/adr/README.md`, with records `docs/adr/0001-*.md` through `docs/adr/0017-*.md` |
| Configuration model | `include/graphx/config.hpp`, `include/graphx/network.hpp`, `src/config.cpp`, `config/schema/graphx.schema.json` |
| CLI/infrastructure | `apps/cli/main.cpp`, `include/graphx/infra.hpp`, `src/infra.cpp` |
| Envelope/framing | `include/graphx/envelope.hpp`, `src/envelope.cpp`, `src/framing.cpp`, `docs/protocol.md` |
| Transport abstraction | `include/graphx/transport.hpp`, `src/transport_factory.cpp`, transport-specific headers/sources/docs |
| Runtime nodes | `apps/generator/main.cpp`, `apps/transform/main.cpp`, `apps/sink/main.cpp`, `apps/common.hpp` |
| Telemetry/control/history | `apps/telemetry/server.mjs`, `operations.mjs`, `control.mjs`, `history.mjs`, `history-worker.mjs` |
| Browser GUI | `web/src/App.jsx`, `web/src/useTelemetry.js`, `web/src/components`, `web/src/data/topology.js` |
| Capture/Wireshark | `src/capture.cpp`, `apps/telemetry/capture-files.mjs`, `wireshark/graphx.lua`, `tools/graphx-extcap` |
| Operations stack | `compose.observability.yaml`, `deploy/observability` |
| Standard deployment | `graphx.yaml`, `compose.yaml`, `compose.history.yaml`, `scripts/demo.sh` |
| Network laboratories | `examples/macvlan`, `examples/ipvlan-l2`, `examples/ipvlan-l3`, `examples/mixed-network` |
| Route/policy laboratory | `examples/static-route-policy`, `docs/adr/0015-explicit-manual-route-activation.md` |
| UDP examples | `examples/udp-unicast`, `examples/udp-broadcast`, `examples/udp-multicast` |
| QEMU profiles | `examples/qemu-node`, `docs/qemu-demos.md` |
| SDR profiles | `examples/sdr-node`, `docs/adr/0014-external-device-control-cycles.md` |
| Current decisions and historical verification | `docs/project-decisions.md`, `docs/archive/README.md`, and accepted records under `docs/adr/` |

## Appendix B. Terminology

| Term | Meaning in GraphX |
|---|---|
| GraphX edge | Directed logical connection between typed node ports |
| Network path | Ordered infrastructure hops realizing or describing an edge |
| GraphX data plane | Edge whose runtime carries canonical GraphX envelopes |
| External data plane | Edge represented by GraphX but implemented by another system |
| Application capture | USER0 PCAPNG containing canonical GraphX frames |
| Ethernet capture | Standard link-layer PCAPNG from OVS, QEMU, or another L2 source |
| Service readiness | Telemetry listeners/configuration are ready |
| Graph readiness | All configured nodes are fresh/running and all edges connected |
| VM readiness | QMP proves VM state/acceleration |
| Guest readiness | Independent TCP and UDP probes prove guest application response |
| Observation credential | Read access to topology, live telemetry, metrics, history, and captures |
| Control principal | Scoped identity allowed to issue/read commands or audit |

## Appendix C. Document maintenance rules

Update this document when an ADR changes an architectural boundary; a configuration surface is added; a new transport, network device, runtime type, exporter, history store, capture format, GUI view, or example is introduced; or a platform-support statement changes. Keep implemented and proposed material visibly separated. Validate example paths and configuration names against the repository before release, and regenerate the DOCX after every material Markdown revision.
