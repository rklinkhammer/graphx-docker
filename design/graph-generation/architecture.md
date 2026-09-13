# Proposed graph generation architecture

**Architecture design — I-01 through I-11 accepted.** P1 implements the model cutover and P2 implements generic application bindings;
P3 implements deterministic artifact compilation; see [P3 verification](p3-verification.md).
The expected files in this review package remain illustrative. Actual compiler
goldens live under `tests/fixtures/compiled`. P4–P6 add verified packaging, the
default platform and finite native/portable execution; see [P6 verification](p6-verification.md).
Native and container applications require separate graphs (`E_EXECUTION_MIX`).
OVS, namespace, guest and scenario realization retain their later phase gates.

## Verdict and evidence

The design meets the simplification objective through fixed bounded execution adapters. Shared templates do not
look up example IDs. S01 and T01 use identical sample types; T02 and T03 reuse the
same SDR port contracts. All ordinary topology edits produce no Dockerfile.
The compiler produces inspectable files, while the existing resource owners
remain the authority for mutation and cleanup. No new blocking contradiction was
found; there are no unresolved I-12+ issues.

Current source boundaries:

| Finding | Source evidence | Consequence |
|---|---|---|
| C++ parser accepts only version 3 and validates strict keys | [config.hpp](../../include/graphx/config.hpp), [config_v3.cpp](../../src/config_v3.cpp), [authored schema](../../config/schema/graphx.schema.json) | A renderer alone cannot introduce this contract; replace model/schema/normalizer atomically |
| Samples use resolved instance identities and type-declared ports | [generator](../../apps/generator/main.cpp), [transform](../../apps/transform/main.cpp), [sink](../../apps/sink/main.cpp) | P2 shares application behavior and uses bound identities for traces/captures |
| Sample/UDP entrypoints consume mandatory resolved node identities and ports | [publisher](../../apps/udp_publisher/main.cpp), [subscriber](../../apps/udp_subscriber/main.cpp), [shared application](../../src/sample_application.cpp) | P2 removes instance/connection defaults; transport behavior remains shared |
| Runtime image provides shared binaries with explicit commands | [Dockerfile](../../Dockerfile) | P4 removes graph COPY and default sample execution; reuse one release image |
| Sample Compose includes generated services; the compiled platform owns settings | [Compose](../../examples/sample-pipeline/compose.yaml), [authored variants](../../examples/variants), [telemetry Dockerfile](../../Dockerfile) | Compile once; credential staging and graph execution consume that output |
| Network profiles repeat service names, fixed host endpoints and wrappers | [network Compose](../../examples/network-lab.compose.yaml), [network launcher](../../scripts/network-lab.sh), [OVS launcher](../../examples/network-lab-ovs.sh), [IPVLAN L2](../../examples/ipvlan-l2/graphx.yml) | Derived names and declarative resource references replace per-topology wiring |
| SDR listeners use validated port bindings; sink accepts one result stream contract | [processor](../../examples/sdr-node/common/processor.py), [protocol](../../examples/sdr-node/common/protocol.py), [sink](../../examples/sdr-node/common/sink.py), [SDR Dockerfile](../../Dockerfile) | P2 preserves the wire protocol and enforces declared peers through the shared reader |
| External namespace ownership and guest startup live outside the core graph | [boundary helper](../../examples/external-ovs-boundary.sh), [route launcher](../../examples/static-route-policy/scripts/demo.sh), [QEMU launcher](../../examples/qemu-node/tap/scripts/ovs-lab.sh) | Fold only these resource kinds into the common identity store; no second ledger |
| Ownership coordinator already supplies locks, rollback, identity-aware capture and endpoints | [ownership API](../../include/graphx/ownership.hpp), [coordinator](../../src/infra/lifecycle_coordinator.cpp), [endpoint module](../../src/infra/endpoint_resources.cpp) | Extend existing owner instead of replaying unchecked shell commands |
| Communication cycles require type feedback support | [validation](../../src/config_v3.cpp), [graph tests](../../tests/test_config_graph.cpp) | P1 checks type feedback declarations; readiness remains separate |
| QEMU uses the common owned runner; actual boot requires its separate harness | [QEMU README](../../examples/qemu-node/README.md), [guest live test](../../tests/test_guest_execution_live.py), [test procedure](../../docs/test-procedure.md) | Record infrastructure and actual guest boot evidence separately |

## Boundaries and proposed layout

Authored input is `graphx.yml` plus immutable reusable catalog selection and an
explicit target argument. Secret values are external. Scenarios are a separate
optional `scenario.actions` section emitted into a separately invoked plan;
baseline startup ignores it. Users maintain no generated Compose or deployment
manifest. The illustrative expected directories here are review evidence only.

Proposed source layout after authorization:

| Component | Files | Smallest new abstraction and why needed |
|---|---|---|
| Authoritative graph model | `include/graphx/config.hpp`, `src/config_*.cpp` | Type/instance/connection structures; P1 resolves catalog types and shared endpoints; P6 executes supported native/portable projections |
| Pure compilation | `include/graphx/compile.hpp`, `src/compile.cpp`, `src/compile/{bindings,artifacts}.cpp` | P3 consumes the resolved value and verified catalog snapshot through fixed serializers |
| Node bindings | `include/graphx/node_settings.hpp`, `src/node_settings.cpp`, reusable application code under `src/`; thin `apps/*` | P2 implements typed per-port bindings and the local-ready/release protocol |
| Catalog assets | `config/catalog/{types,templates,guests}`, `config/schema/` | Fixed data contracts; no plugins or programmable renderer |
| Execution adapters | `src/infra/{process_resources,qemu_resources}.cpp`, existing lifecycle coordinator; thin `apps/cli/main.cpp` | Owned process records and bounded startup phases; Compose alone cannot run native/QEMU or attach owned endpoints |
| Platform | existing `apps/telemetry/`, native release packaging and `Dockerfile` telemetry target | Consume one resolved contract, default history and sealed packet handoff; existing combined service already owns these capabilities |
| Examples | `examples/*/graphx.yml`, optional scenario actions and acceptance commands | Replace source Compose/overlays and network wrappers only once converted behavior passes |

The compiler returns a value, never invokes Docker, QEMU, `ip`, `nft`, OVS or a
credential provider. The execution CLI validates the output manifest and uses
current APIs. Plans are disposable derivatives. They are never independently
editable runtime authority, nor a journal of actual state.

## Authored schema and defaults (I-01, I-03, I-11)

The structural contract is [graph.schema.json](catalog/graph.schema.json).
Unknown keys and duplicate YAML keys fail closed. YAML aliases/tags and merge
keys are rejected; YAML 1.2 scalar rules apply. Maximum input size is 1 MiB,
1024 nodes, 4096 connections, 256 ports/type, 256 networks/switches/routers,
4096 attachments and 64 scenario actions/captures. ASCII IDs match
`[a-z][a-z0-9_-]{0,63}`. Catalog type IDs may contain dots. Zero application nodes
are allowed; the platform still exists. `platform`, extension IDs and the
compiler's `mg-` service/network namespace are reserved.

Required root fields are `version: 3`, `graph.id`, `catalog`, `nodes`, and
`connections`. Each node requires `type` and explicit `execution.kind`;
execution fields are constrained by that kind. Native requires a selected native
host; namespace requires Linux OVS; QEMU requires guest, architecture and
accelerator; external may declare address or attachment-only. External lifecycle
never starts a process. QEMU attachment ownership and process ownership are
separate: S11 owns a TAP, S15 owns both TAP and QEMU.

Each connection requires `from`, `to`, and `transport`. Endpoint references are
`node.port`, not service names or edge-name conventions. Optional `settings`
contains transport settings only; TCP rejects UDP/shared-memory fields and vice
versa. `attachments` must name both endpoints when selecting a managed path.
`security.profile: mtls` requires a server name and credential bindings on both
managed peers, or external-device client/trust provisioning. Raw versus GraphX
encoding is selected by the port contract and must agree at both ends; it is not
inferred from lifecycle. No implicit conversions or transport fallbacks.

`network` has logical OVS networks, switches, router interfaces, attachments,
policies, declared routes, mirrors and bounded captures. Its vocabulary retains
the existing model's network semantics but removes physical names, Docker
parents and redundant router attachments. `network.realization` is always `ovs`
for managed resources. `portable_network` is a separate subnet/address intent
for unprivileged Docker bridge connectivity. It cannot select MACVLAN/IPVLAN.
Router interfaces expand into owned namespace veth attachments; a graph does
not author the same endpoint twice. Mirror `port` refers to a switch output port.
`edge_paths`, if present, are validated display routes, never execution recipes.

Default precedence: versioned C++ transport defaults → pinned type parameter
and listener-port defaults → explicit graph values. Platform defaults come from
one pinned platform template, with explicit graph settings overlaid fieldwise.
No ambient environment overrides influence compilation. Explicit settings must
pass range, type, transport and placement checks; an invalid explicit setting
is never replaced by a default. `platform.history.enabled` cannot be false.
Type and template schemas carry no executable conditionals. Parameters are
closed against each type's schema and fully materialized in node output.

`types/*.json` specify direction, wire schema, transports, encoding, min/max
connection count, feedback and listener role for every port. All current sample,
UDP and SDR ports support exactly one peer; fan-in and fan-out are rejected.
A new multiple-input sink requires a genuinely implemented bounded sink type,
not changing a fixture's maximum. Missing required connections also fail.
All sample ports reject feedback. SDR sample/control pairs and diagnostic echo
ports explicitly allow feedback. Every edge inside a strongly connected
component must have feedback support on both participating ports. An explicit
`feedback: true` also requires both capabilities even without a cycle. No edge
creates a startup dependency automatically.

## Normalized and node contracts (I-02)

Authored graph version is 3; normalized contract version is 2 (current normalized
contract is 1). The proposed [outer schema](catalog/normalized.schema.json) is
intentionally incomplete below its root; the implementation must close every
nested object. Normative complete fixture instances accompany every target.

`resolved.json` has exactly: contract_version, graph_version, graph_id, target,
catalog_digest, input_digest, nodes, connections, network, portable_network,
platform, credential_references, ordering and execution_values. The digest is
SHA-256 of canonical authored JSON; catalog_digest hashes lock bytes. `nodes`
and `connections` are arrays sorted by ID. `network` is fully expanded resource
intent plus management isolation policy; optional resource arrays are empty
when absent. `portable_network` is null when unused. All defaulted platform
settings are explicit. Credential fields contain reference names only.

Each node file is byte-equivalent to the canonical serialization of its node
object: contract_version, graph_version, graph_id, node_id, type, type_revision,
execution, parameters, bindings, observation, telemetry, credentials, readiness,
capture and startup. `bindings` maps every declared port to a sorted array of
connection objects: connection ID, role (`connect` or `listen`), transport,
schema, encoding, full settings, source/destination addresses, security and
attachment pair. Both endpoints get the same settings; connect uses destination
and source-interface binding, listen uses `bind`. Each node accepts mandatory
`--node ID --config FILE`, verifies the ID, and never falls back to sample names.
P2 also requires invocation-supplied `--release-file` and `--release-token` on
application execution; it emits `ready node=ID` after local binding. P3/P6 adapters
must supply the runtime barrier arguments; tokens are not authored or compiled
graph values. Readiness/probes use the same binding parser. SDR source filtering uses the
resolved source address, not an environment default or arbitrary incoming sender.

Each resolved connection contains id, from/to `{node,port}`, transport, schema,
encoding, listener (`node.port`), full settings, source_address,
destination_address, attachments and security. TCP defaults: 5000 ms connect
and send timeouts, reconnect true, 60 retries, 100–2000 ms backoff. UDP defaults:
unicast, TTL 1, loopback true, reuse false, 65536-byte send/receive buffers,
1400-byte datagrams. GraphX framing is u32be with envelope wire version 2; raw
framing is none. Shared memory must explicitly declare bounded capacity, message
size and timeouts in these examples. Schema and framing bounds both apply.

## Endpoints, names, locality and readiness (I-03–I-06)

Input ports listen; output ports connect/send. Sort connections by ASCII ID.
An explicit port wins, then the type's protocol port (SDR 18400/18401/18402),
otherwise assign 20000 + connection ordinal. Reject exhaustion above 65535.
No search for a currently free port occurs at compilation. Listener conflicts
are keyed by target network namespace, protocol, address and port; wildcard
bind overlaps every local address. TCP and UDP may share a numeric port. Reuse
is allowed only for the same declared multicast group with explicit reuse and
supported port cardinality. Identical host-published ports conflict regardless of
Compose project. A busy host port at execution is a prerequisite failure.

Ordinary container graphs get an internal data bridge and deterministic subnet
`172.28.N.0/24`, where N is the first four hexadecimal SHA-256 digits of graph ID
modulo 128. Sorted node IDs receive host numbers 10 upward; reject more than 244
addresses rather than wrap. Explicit laboratory subnets and addresses are
preserved and validated, including network/broadcast/gateway reservations,
prefix membership, overlapping domains and duplicate IPs within a VLAN.
Execution checks collisions with host routes and Docker networks; no silent
renumbering. The subnet pool is a selected initial restriction, not an allocator
service. Data listeners bind concrete data IPs; service DNS is used for platform
access only. UDP broadcast resolves the sender interface explicitly, receiver
wildcard binds only in its isolated namespace. Multicast group, membership
interface, TTL, loopback and reuse are preserved literally.

Native loopback remains the native host namespace. S03 uses 127.0.0.1:47101;
S04 uses 239.255.42.1:47103, interface 127.0.0.1, TTL 0 and loopback true.
Shared memory requires the same named native IPC domain on the same host; S02
retains capacity 8, 4096-byte messages and its explicit segment names. Container
shared IPC is outside this first catalog: reject it, do not substitute TCP.
Explicit segment names collide across simultaneous deployments and are checked
before opening; owned cleanup must never unlink another process's segment.
Ordinary future shared-memory names can use the same graph-derived hash rule.

Physical names: `gx` + kind (`b`, `i`, `p`, `n`, `t`) + first 12 lowercase hex
digits of SHA-256(`graph_id + NUL + kind_word + NUL + logical_id`). Kind words are
bridge/interface/peer/namespace/tap. Every name is at most 15 bytes. Router
interfaces use logical `router.interface`; generated router attachment IDs are
`router-interface`. QEMU peer aliases `peer-data` and `isolated-data` in S15 are
explicit fixture bindings to router interface IDs, not reserved compiler names:
the authored optional interface `attachment_id` supplies these names, so
these references remain generic. Names collide → reject with E_NAME_COLLISION;
never random suffixes or truncation retries. Graph ID distinguishes deployments;
users supply a different graph ID for a second identical concurrent instance.
Service IDs use node IDs directly, with no sanitization ambiguity.

Startup is a fixed finite sequence: preflight, stage configuration/credentials,
start platform and await health, launch applications with listeners bound but
connectors held, realize selected OVS resources, check local readiness, release
connectors, return. Native code uses the same local-ready/release protocol.
Containers mount an owner-checked barrier directory read-only; only the local
execution adapter writes release. Readiness checks listeners and local resources,
not receipt of sample traffic. This prevents SDR sample/control readiness cycles.
UDP senders wait for receiver local readiness. TCP retry remains bounded and
handles a peer failing after the barrier. External peers require explicit
operator-provided readiness or a bounded declared protocol probe; declared alone
is not healthy. There is no continuous restart loop, dependency solver or daemon.

## Network realization and target capabilities (I-04, I-05)

| Rule | Native Linux | Native macOS | OrbStack | Lima |
|---|---|---|---|---|
| C-NATIVE | native apps + native platform | native apps + native platform | unsupported host-native placement | native apps + native platform inside guest |
| C-CONTAINER | Docker apps + platform | unsupported, no implicit engine selection | unprivileged containers + platform | guest Docker apps + platform |
| C-OVS | authorized Linux OVS; Docker/namespace/QEMU as authored | unsupported | unsupported managed OVS | authorized OVS inside dedicated ARM64 guest |
| C-GUEST | x86_64 guest, TCG | unsupported direct boot | unsupported direct boot | x86_64 guest on ARM64 host guest, TCG |

The per-case matrices below and READMEs use the exact capability cell vocabulary.
A supported cell means proposed generation preserves the declared placement;
it is not runtime evidence. No supported set here hides native applications
behind an OrbStack platform. Native platform packaging is proposed; existing
native examples currently do not require this platform. Lima still requires the
existing identity-checked VM prerequisite; no automatic VM creation or replacement.
No current evidence or baseline support is claimed for KVM. A future x86 Linux
KVM dimension requires guest artifact and acceleration validation separately.

Portable Compose data uses Docker bridge connectivity explicitly. Managed OVS
Compose networks are management only; each managed node gets its own management
bridge with the platform. The platform disables forwarding and does not join the
data plane. Owned OVS setup additionally installs management ACLs before release:
allow node → platform TCP 8080/UDP 9000 and established return; deny node-to-node
and forwarding. Containers have no NET_ADMIN, no privileged socket, and bind
application listeners only to their selected OVS IPs. Dropping a data route
must break that application flow even while telemetry stays reachable.
Namespace diagnostic peers without GraphX telemetry have no management route.
Guests exchange configuration/credentials/readiness over local virtio channels,
not a management NIC that could bypass OVS.

MACVLAN profile means unique MAC per endpoint on OVS; no Docker driver.
IPVLAN L2 shares the declared MAC and uses attachment/IP-aware OVS forwarding and
ARP handling rather than naive MAC learning, including router interfaces.
IPVLAN L3 uses destination-IP forwarding and on-link routes across its subnet
domains; unknown/non-unicast traffic is dropped across domains. Preserve current
flow semantics from `src/network.cpp` and network/infra modules.
Router policies preserve authored order, established/related return acceptance
first, explicit policies next, default drop last. S12's independent denied flow
is not mistaken for an established response. A manual route is declared but not
installed by create; its separately invoked action references router and
prefix, and uses `route_command`. VLAN access tags are 42 for guest and peer,
43 for the isolated port; mirrors are output-only, never usable transit links.

## Platform, credentials, history and builds (I-07–I-10)

Every graph gets one platform process/service containing console, authenticated
telemetry, history and optional packet decoding. With zero managed applications,
S11 still has a platform; external devices are declared/observed, not assigned
fabricated GraphX heartbeats. Raw applications may emit signed telemetry if their
type says so. Multiple radios have distinct runtime credentials and node IDs.
Managed radio guests initially advertise packet/status observation only; adding
GraphX heartbeat support requires the guest implementation to supply it.

Native platform uses the pinned release's Node.js 24 and web assets. Applications
reach loopback UDP 9000 with distinct HMAC credential references. Console binds
127.0.0.1:8080 by default; graph may choose 1024–65535. Console origin allowlist
is derived from the chosen port. Containers publish loopback HTTP only; platform
UDP remains private. Lima forwards **only that chosen loopback HTTP port** using
an explicit non-privileged SSH/local forward with identity-checked VM selection;
port availability is checked on guest and host. No UDP telemetry or privileged
socket crosses to macOS. Forward process lifetime is owned by the existing Lima
launcher invocation; failure reports access unavailable without reconfiguring VM
or silently selecting a port. Inventories count graph processes inside the
selected target; optional host forwarding is one additional access process,
not an application or guest process.

History is always SQLite, 7 days, 100000 records, 256 MiB DB by default; all queue,
query and shutdown bounds are explicit in platform.json. Enforce aggregate
DB+WAL+SHM disk bounds with checkpoint/backpressure: stop accepting records if
bounded deletion/checkpoint cannot recover space; report drops, preserve live
telemetry. Native user state goes under `$XDG_STATE_HOME/graphx/<id>` (Linux) or
`~/Library/Application Support/GraphX/<id>` (macOS). Privileged deployments use
`/var/lib/graphx/runtime/<id>`, live captures `/var/lib/graphx/captures/<id>` and
ownership `/var/lib/graphx/ownership` inside Linux. Docker volumes/binds on
OrbStack stay in its Linux storage. Stop preserves history; explicit `history
remove` checks owner identity, refuses active writers, and deletes only that
history store. Never combine deletion with routine down. Captures are independently
bounded by file size/count/packet/retention caps; root-only live SPAN files are
exported using `export_owned_network_capture` to a bounded, read-only handoff.
Packet decoding stays in the platform instead of adding a second SQLite store.
Application raw capture in S13 is proposed socket-level capture, not proof of
Ethernet observation; a selected Ethernet observation lab uses the OVS handoff
contract and requires its own privileged acceptance.

Credential providers are only `external`, `lab-generated` and
`runtime-generated`; providers run at explicit staging, never compilation.
Each graph gets one ephemeral laboratory CA for its lab-generated references;
issue separate SAN/role-constrained leaf credentials for each declared identity.
That CA is never used for external-provider credentials.
Graphs contain reference IDs, public identity names and required member names,
never tokens, certificates or private-key bytes. Per-node HMAC and read tokens
are generated at execution with OS randomness, mode 0400 files below mode 0700
owner-checked staging. Apps see only their own files; the platform gets telemetry
verification keys and explicitly authorized operator/export credentials. Plan
logs redact values. Local credential mounts provide no encryption at rest. Bind one read-only
directory per authorized reference (never the whole credential root), with
0500 directory/0400 file permissions for UID/GID 65532. Stable directory mounts
allow atomic member replacement during rotation; file-level bind mounts would
retain the old inode. The staging adapter publishes at most current and previous
members plus generation/grace metadata. Readers reload with generation checks
and bounded retries; previous credentials expire on an execution-time monotonic
deadline. This is credential data, not a second resource lifecycle ledger. Native processes receive file paths,
not secret argv/environment values. API authorization reuses current policy,
runtime identity, origin, bounded body, idempotency and audit checks. Merely
adding a node grants no control permission. Observation access requires its read
token even on loopback; platform readiness exposes no graph secrets.

External radio key material stays on the device. Processor external credentials
must trust the actual device CA/SAN (`sdr-node` in S14); generated simulator CA
is never implicitly trusted by physical production devices. The explicitly
invoked laboratory-radio action runs a separate test fixture only after physical
absence/address checks. Its test-radio credentials and health are distinct from
the physical declaration. The scenario must stage processor credentials from
the same test CA for that laboratory run and restore external references after
stop; it cannot reuse physical trust as a shortcut. Credential rotation has
separate operator and runtime actions, 60-second overlap, and explicit old-key
rejection afterward. No continuously running rotation service.

Guest build inputs are source tree digest, builder digest, architecture, fixed
epoch and literal argv; outputs have digests, license information and application
contract identity. Runtime verifies them before creating TAP/boot resources.
QEMU runs as dedicated UID/GID 65532 checked against the required account, with
TCG, local QMP, bounded serial logs and only the owned TAP. Virtio config and
credential sockets are local owned 0600 paths; guest writes private data only to
tmpfs and signals ID+config digest+listener readiness. Peer identity and framed
message bounds are checked before guest launch release. Missing guest binary
from a valid build recipe is E_GUEST_UNAVAILABLE at execution, not a compile
fallback to an external guest. The image-build boundary remains separate from
container service creation. The P8 echo/radio recipes and provisioning agent are implemented under `guests/`;
actual S15/T03 boot evidence is tracked separately in [P8 verification](p8-verification.md).

## Determinism, paths and output ownership (I-06, I-11)

Fixed inputs are graph bytes after strict parse, catalog lock and content,
compiler version, target capabilities, and an explicit roots descriptor. Resolve
graph-relative paths against the authored file's parent; catalog paths against
the declared catalog root, catalog members against the lock's parent; recipe
sources against a declared source root. For this package the catalog root is
`design/graph-generation/catalog`, so `../../catalog/lock.json` is valid even
though it is above a case directory. Credential references resolve only within
a separate execution credential root. No path can escape its allowed root after
canonicalization; reject symlinks, absolute authored paths, device files and
unexpected source types. External device IPs are addresses, never filesystem
paths. Output root must be outside all input roots and credential roots.

Sort node, connection, service, credential, network and artifact IDs by ASCII.
Sort set-like arrays by ID/value. Preserve semantically ordered arrays: argv,
route paths, firewall policies, scenario actions, retries and guest build steps.
JSON is UTF-8, sorted keys, two-space indentation, LF and terminal newline;
integers are decimal and non-finite floats are forbidden. YAML renderer has
fixed quoting/indentation, no aliases, LF and sorted mappings; INI sections and
keys have fixed order. Byte-for-byte output determinism applies to compiled
configuration and plans for fixed inputs, including compiler version. Platform
native versus container and target capability selections are different inputs.
Cross-serializer YAML equivalence is only semantic; the production serializer
version is pinned. These illustrative files do not demonstrate compiler repeatability.

No timestamps or random IDs enter compiled output. Explicit host ports/addresses
are fixed. Execution-only values are exactly the five substitutions in each
set: output location, target-local state root, credential staging root, verified
release root and owner token. OS-assigned container IDs, ifindices, namespace
inodes, process start times, selected free client ephemeral ports, TCP sessions,
telemetry timestamps/nonces and credential bytes are observed/allocated by
existing runtime ownership/protocol code, not unresolved plan fields. Image,
recipe, source, guest digest, listening port, MAC/IP, target and catalog identity
must be resolved before execution. Runtime availability checks never rewrite
these values. Semantic resource IDs do not contain execution owner tokens.

Compiler output creation is exclusive. Stage in a sibling directory on the same
filesystem, fsync contents and manifest, then rename. Manifest contains compiler,
graph ID, input/catalog digests and each artifact path/hash; it is a provenance
index, not lifecycle state. Existing nonempty output without a valid manifest is
E_OUTPUT_OWNERSHIP. Explicit replacement requires the same graph identity,
owner UID, no symlinks, and every old managed file matching its recorded hash;
unlisted or modified files cause rejection. Stale managed files disappear only
through atomic directory replacement after checks. Runtime ownership must report
no active consumers before replacement; compilation itself only reports a plan
replacement prerequisite and never stops them. Use a fresh directory if that
read-only check cannot establish inactivity. Never `rm -rf` an arbitrary output.

## Execution ownership and failures

Plans use `version: 1` declarative objects with complete literal argv arrays or
named API operations; no shell evaluation. The plan is not executable today.
`native-plan.processes` gives ID/kind/namespace/argv/cwd/environment/credential
references/readiness/log/limits. `ovs-plan.resource_intent` is the resolved network;
its fixed stages are descriptive entry points to existing ownership code, not
an open-ended list of commands. `qemu-plan.guests` gives artifacts, argv, channels,
identity, network delivery and readiness. `scenario-plan.actions` is a closed
finite enum with graph resource references. Unknown operations fail closed.

| Artifact/action | Execution owner | Invocation boundary / consumer | Readiness input | Failure result | Cleanup owner | Persisted state |
|---|---|---|---|---|---|---|
| Graph/catalog → resolved, node, Compose, manifest files | unprivileged compiler | implemented `graphx compile`; authoritative loader feeds pure serializers | parsed/validated fixed inputs | diagnostics, no published partial output | compiler owns only staging output | artifact provenance manifest |
| Credential staging | invoking user, narrow credential adapter | proposed staging subcommand replacing sample/SDR PKI blocks | roots, ownership, SAN/policy refs, permissions | abort before app release, no secret logs | staging adapter verifies owner; external originals untouched | files under credential root; references in existing state |
| Container creation/start/stop | selected Docker engine | generated Compose via existing `scripts/lib/demo-runtime.sh` reduced to common invocation | image pins, platform health, local app health | return failure; only newly created matching project services removed | Compose with checked graph labels and container IDs | engine metadata, existing ownership bindings |
| Native platform/apps | invoking user, missing owned-process adapter | CLI adapter using common process identity contract; replace native `run.sh` loops | executable digest, listeners-bound files and platform HTTP ready | bounded wait then rollback new processes | process adapter, PID/start-time/executable checks | extend existing ownership store; native user root |
| OVS bridge/veth/TAP/namespace/routes/policies | authorized Linux caller | `execute_ovs_lifecycle` and existing resource modules, `scripts/network-lab.sh` narrowed | engine IDs, system OVS, UID and address/name checks | fail closed, reverse newly owned operations | same lifecycle coordinator | existing `/var/lib/graphx/ownership` identities |
| Management ACL and release barrier | same local execution invocation | narrowly add ACL setup to endpoint resources and barrier write | all owned endpoints/policies and local listeners ready | no release if any check fails | endpoint owner; owned barrier staging | existing endpoint identities plus owned file |
| Manual route and timed fault | authorized scenario caller | existing `route_command` / fault lifecycle, graph refs | matching router/attachment identity | action fails; baseline deployment is not recreated | existing route/fault owner | existing route/qdisc identities |
| Live network capture | OVS capture owner | existing capture resources | identity-checked mirror, bounded writer | capture unavailable; fail selected acceptance | existing capture owner | root-only ring and capture process identity |
| Sealed capture handoff | narrow existing exporter loop | `export_owned_network_capture`, parameterized QEMU exporter | complete sealed capture, exclusive destination | no partial file served | owned exporter process adapter | bounded snapshots; no duplicate packet history |
| Platform console/history/decoder | platform process | existing telemetry server extended for contract 2 | resolved config, auth refs, SQLite and sealed input | unhealthy/drop counters; no unauthenticated fallback | Compose/native owner; history retained | bounded SQLite and audit data |
| History deletion | explicit user command | platform/CLI narrow history-remove | no writer, exact history owner | reject mismatch/busy store | same store owner | deletes history only, audit result retained |
| Guest build | unprivileged release/build invocation | adapt `examples/qemu-node/scripts/build.sh` to catalog recipe | source/dependency digests, trusted builder, architecture | no valid artifact manifest | build owns its new workspace | guest files, checksums, licenses/SBOM |
| QEMU boot/config/credentials/QMP | missing narrow QEMU process adapter | extend existing TAP launcher logic in common process owner | guest digests, TAP, peer, local channel handshake | timeout/failed application contract; stop own guest | process owner then TAP owner | existing process identity, bounded QMP/serial evidence |
| External QEMU/device | external operator | no boot operation; optional attachment and probe only | explicit external status or protocol observation | declared/unobserved; never fabricated healthy | external owner; GraphX removes only its owned link | declaration plus observed evidence |
| Laboratory SDR substitute | explicitly invoked scenario adapter | parameterize `external-ovs-boundary.sh`, common process owner | physical absence, own test CA, unused address | reject simultaneous device presence | test process/namespace owner | existing identity record, bounded test log |
| Lima HTTP forwarding | existing host Lima boundary | validated VM selection plus bounded nonprivileged local forward | VM digest and guest/host port free | console access failure, no VM mutation | forwarding process owner | process identity only; no forwarded privileged socket |

The adapter reads one verified resolved configuration and executes only the
fixed stages needed for its selected kinds. A plan digest mismatch requires
recompile, not trusting edited argv. No scheduler, distributed reconciliation,
generic plugin hooks, restart policies, desired-state database or deployment
daemon. Process identities extend the same ownership format instead of keeping
`.env` ledgers per launcher. On interruption, reverse only resources created by
this invocation; external and pre-existing matching resources survive. If an
identity changed, stop cleanup and retain recoverable evidence/state. Failure to
clean does not hide the original error. Recovery uses existing explicit recover
semantics and rechecks identity; it never adopts an arbitrary same-named resource.

## Selected restrictions and implementation risks

These do not prevent a consistent contract or acceptance test and are not new
issues. Baseline supports single-peer ports, IPv4, native-only shared memory,
TCG guest execution and bounded fixed templates. Endpoint/subnet/name hash
collisions reject instead of searching. Every generated pin must be replaced by
verified packaging identities in P4 (runtime/images) and P8 (guests) before execution acceptance. P3 explicitly marks current artifact identities unverified. The hardest work
is bind-before-connect application readiness, management-path isolation,
IPVLAN shared-MAC forwarding, native secure platform packaging and guest
configuration channels. Static fixture validation cannot establish those.

S13 deliberately demonstrates socket-level raw capture without an extra
privileged sniffer; Ethernet timing/drop observation remains selected OVS
acceptance, not evidence from raw capture. Platform-integrated packet decoding
must preserve packet provenance and limits before removing existing observer
containers. The prototype normalized outer schema is not a substitute for a
fully closed production schema. Both are explicit phase gates below.

## Accepted-decision traceability

All rows are `static-design` contract locations; runtime checks remain planned.

| Decision | Proposed contract location | Concrete fixtures | Planned verification |
|---|---|---|---|
| I-01 | Authored schema / Normalized contract; [schema](catalog/graph.schema.json) | [S01–S15, V01–V06, T01–T03](inventory.json); [S01](scenarios/sample-pipeline/graphx.yml) | `planned-portable`; version rejection; schema/normalizer/consumer atomic replacement |
| I-02 | Normalized and node contracts; [schema](catalog/graph.schema.json) | [S01, T01, T02](inventory.json); [S01](scenarios/sample-pipeline/graphx.yml) | `planned-portable`; renaming and two-instance runtime output independence |
| I-03 | Authored schema / Endpoints and readiness; [schema](catalog/graph.schema.json) | [T02, T03, N02–N05, N12–N13](inventory.json); [T02](variants/multi-radio/graphx.yml) | `planned-portable`; port/schema/cardinality/SCC rejection; no readiness cycles |
| I-04 | Endpoints, names, locality; [schema](catalog/graph.schema.json) | [S02–S04, S06, N06](inventory.json); [S02](scenarios/shared-memory/graphx.yml) | `planned-portable`; native Linux/macOS IPC and loopback behavior |
| I-05 | Network realization; [schema](catalog/graph.schema.json) | [S05, S07–S12, S14–S15, N01, N11](inventory.json); [S05](scenarios/udp-broadcast/graphx.yml) | `planned-portable`; `planned-privileged`; privileged OVS profile/VLAN/routes/management bypass denial |
| I-06 | Determinism, paths and output ownership; [schema](catalog/graph.schema.json) | [T01, N07–N09, N14](inventory.json); [T01](variants/renamed-multi-source/graphx.yml) | `planned-portable`; `planned-privileged`; byte determinism; collisions; path/symlink/stale-output negatives |
| I-07 | Platform, credentials, history and builds; [schema](catalog/graph.schema.json) | [S11, S15, T03, N10, N15](inventory.json); [S11](scenarios/network-observation/graphx.yml) | `planned-portable`; `planned-guest-boot`; external not booted; actual TCG application boot and missing artifacts |
| I-08 | Platform, credentials, history and builds; [schema](catalog/graph.schema.json) | [S13–S14, T02–T03](inventory.json); [S13](scenarios/sdr-simulated/graphx.yml) | `planned-portable`; raw protocol vs lifecycle and telemetry provenance |
| I-09 | Execution ownership and failures; [schema](catalog/graph.schema.json) | [S11–S12, S14–S15, V04](inventory.json); [S11](scenarios/network-observation/graphx.yml) | `planned-portable`; `planned-privileged`; baseline excludes actions; route/fault/rotation apply/clear |
| I-10 | Platform, credentials, history and builds; [schema](catalog/graph.schema.json) | [S01–S15, V01–V06](inventory.json); [S01](scenarios/sample-pipeline/graphx.yml) | `planned-portable`; `planned-privileged`; default history bounds; native access; Lima HTTP only; explicit deletion |
| I-11 | Boundaries / Authored schema; [schema](catalog/graph.schema.json) | [S01, T01–T03, catalog](inventory.json); [S01](scenarios/sample-pipeline/graphx.yml) | `planned-portable`; unchanged shared types; zero topology Dockerfiles; reject arbitrary overrides |

## Case and diagnostic traceability

Each positive row indexes every target set and expected file through its inventory.
All artifacts are `static-design`; no row asserts runtime execution. Per-case
READMEs include the four-target capability matrix, counts and semantic checks.

| ID | Input | Expected output / diagnostic | Target | Acceptance evidence and status |
|---|---|---|---|---|
| S01 | [graph](scenarios/sample-pipeline/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/sample-pipeline/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](scenarios/sample-pipeline/README.md) |
| S02 | [graph](scenarios/shared-memory/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/shared-memory/expected/inventory.json) | native-linux, native-macos, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable`: [case acceptance](scenarios/shared-memory/README.md) |
| S03 | [graph](scenarios/udp-unicast/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/udp-unicast/expected/inventory.json) | native-linux, native-macos, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable`: [case acceptance](scenarios/udp-unicast/README.md) |
| S04 | [graph](scenarios/udp-multicast/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/udp-multicast/expected/inventory.json) | native-linux, native-macos, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable`: [case acceptance](scenarios/udp-multicast/README.md) |
| S05 | [graph](scenarios/udp-broadcast/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/udp-broadcast/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](scenarios/udp-broadcast/README.md) |
| S06 | [graph](scenarios/application-capture/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/application-capture/expected/inventory.json) | native-linux, native-macos, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable`: [case acceptance](scenarios/application-capture/README.md) |
| S07 | [graph](scenarios/macvlan/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/macvlan/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged`: [case acceptance](scenarios/macvlan/README.md) |
| S08 | [graph](scenarios/ipvlan-l2/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/ipvlan-l2/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged`: [case acceptance](scenarios/ipvlan-l2/README.md) |
| S09 | [graph](scenarios/ipvlan-l3/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/ipvlan-l3/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged`: [case acceptance](scenarios/ipvlan-l3/README.md) |
| S10 | [graph](scenarios/mixed-network/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/mixed-network/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged`: [case acceptance](scenarios/mixed-network/README.md) |
| S11 | [graph](scenarios/network-observation/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/network-observation/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged`: [case acceptance](scenarios/network-observation/README.md) |
| S12 | [graph](scenarios/static-route-policy/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/static-route-policy/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged`: [case acceptance](scenarios/static-route-policy/README.md) |
| S13 | [graph](scenarios/sdr-simulated/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/sdr-simulated/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](scenarios/sdr-simulated/README.md) |
| S14 | [graph](scenarios/sdr-external/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/sdr-external/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged`: [case acceptance](scenarios/sdr-external/README.md) |
| S15 | [graph](scenarios/qemu-tap/graphx.yml) | [all target artifacts and unsupported diagnostics](scenarios/qemu-tap/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged; planned-guest-boot`: [case acceptance](scenarios/qemu-tap/README.md) |
| V01 | [graph](variants/history/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/history/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](variants/history/README.md) |
| V02 | [graph](variants/observability/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/observability/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](variants/observability/README.md) |
| V03 | [graph](variants/control/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/control/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](variants/control/README.md) |
| V04 | [graph](variants/credential-rotation/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/credential-rotation/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](variants/credential-rotation/README.md) |
| V05 | [graph](variants/secure-otlp/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/secure-otlp/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](variants/secure-otlp/README.md) |
| V06 | [graph](variants/otlp-mtls/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/otlp-mtls/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](variants/otlp-mtls/README.md) |
| T01 | [graph](variants/renamed-multi-source/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/renamed-multi-source/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](variants/renamed-multi-source/README.md) |
| T02 | [graph](variants/multi-radio/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/multi-radio/expected/inventory.json) | native-linux, orbstack, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker`: [case acceptance](variants/multi-radio/README.md) |
| T03 | [graph](variants/mixed-container-qemu-sdr/graphx.yml) | [all target artifacts and unsupported diagnostics](variants/mixed-container-qemu-sdr/expected/inventory.json) | native-linux, lima; other targets diagnostic | `static-design` parse/shape/reference checks; `planned-portable; planned-docker; planned-privileged; planned-guest-boot`: [case acceptance](variants/mixed-container-qemu-sdr/README.md) |
| N01 | [input](negative/ambiguous-endpoint/graphx.yml) | [E_ENDPOINT_AMBIGUOUS](negative/ambiguous-endpoint/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N02 | [input](negative/invalid-port/graphx.yml) | [E_PORT_UNKNOWN](negative/invalid-port/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N03 | [input](negative/invalid-schema/graphx.yml) | [E_SCHEMA_MISMATCH](negative/invalid-schema/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N04 | [input](negative/unsupported-fanin/graphx.yml) | [E_CARDINALITY](negative/unsupported-fanin/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N05 | [input](negative/unsupported-feedback/graphx.yml) | [E_FEEDBACK_UNSUPPORTED](negative/unsupported-feedback/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N06 | [input](negative/incompatible-ipc/graphx.yml) | [E_IPC_PLACEMENT](negative/incompatible-ipc/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N07 | [input](negative/name-conflict/graphx.yml) | [E_NAME_RESERVED](negative/name-conflict/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N08 | [input](negative/address-conflict/graphx.yml) | [E_ADDRESS_CONFLICT](negative/address-conflict/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N09 | [input](negative/port-conflict/graphx.yml) | [E_PORT_CONFLICT](negative/port-conflict/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N10 | [input](negative/missing-guest/graphx.yml) | [E_GUEST_ARTIFACT](negative/missing-guest/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N11 | [input](negative/unsupported-target/graphx.yml) | [E_TARGET_CAPABILITY](negative/unsupported-target/diagnostic.json) | native-macos | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N12 | [input](negative/sdr-shared-sink/graphx.yml) | [E_CARDINALITY](negative/sdr-shared-sink/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N13 | [input](negative/unsupported-fanout/graphx.yml) | [E_CARDINALITY](negative/unsupported-fanout/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N14 | [input](negative/path-escape/graphx.yml) | [E_PATH_ESCAPE](negative/path-escape/diagnostic.json) | native-linux | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
| N15 | [input](negative/unsupported-accelerator/graphx.yml) | [E_TARGET_CAPABILITY](negative/unsupported-accelerator/diagnostic.json) | lima | `static-design` parse and diagnostic coverage; `planned-portable` exact code/path and no output |
