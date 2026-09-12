# Instance identity contract

This contract defines instance-aware configuration and the requirements for runtime
isolation. The authoritative loader and normalized JSON support optional
`deployment.instance_id` and typed SDR source settings. Configuration consumers
can select a node against an explicit matching instance identity. The OVS
lifecycle resolves and owns resources by instance. Execution IDs and application
runtime enforcement are not implemented yet.
Configuration and envelope wire versions remain 2. See
[configuration](configuration.md#instance-selection-and-sdr-source-settings) for
supported fields and APIs.

## Identity domains

| Identity | Meaning and scope | Assignment and lifetime |
|---|---|---|
| Graph ID | Logical topology, not a running deployment | Configuration author; stable across deployments |
| Instance ID | One deployment of a graph within a deployment authority | Operator selects it before startup; stable through restarts and recovery |
| Node ID | Logical node within a graph | Configuration author; unique within the graph and stable across restarts |
| Execution ID | One activation of one node process, VM, or external-device session | Trusted lifecycle authority assigns a fresh value before registering each activation |
| Ownership token | Correlates resources with an infrastructure ownership transaction | Existing infrastructure lifecycle creates and records it; retained for recovery, replaced after completed destruction and fresh creation |

A deployment authority is the trusted runtime and its ownership-state root: for
example, a native Linux host or the dedicated GraphX Lima guest. Tuple uniqueness
is required within that authority; it is not a global multi-host discovery claim.
Independent authorities must not be combined into one collector without an
additional explicit authority boundary. The initial deployment model uses one
collector per graph instance.

The instance key is `(graph ID, instance ID)`. The node key is
`(graph ID, instance ID, node ID)`. A live command target additionally includes
the execution ID. These are structured tuples, not ambiguously concatenated
strings. Changing an instance ID selects a different deployment; it never renames
or adopts an existing deployment.

Node `kind`, `runtime`, and the existing `execution` placement property are not
identities. In particular, `execution: local` does not identify an execution.
Container names, PIDs, IP addresses, MAC addresses, ports, and image tags are not
substitutes for node or execution identity. Configuration hashes describe content
and drift, not deployment identity.

Message, trace, and parent-message identities remain correlation identifiers.
They must not be reused as execution IDs, ownership tokens, or credentials.

## Representation and validation requirements

Graph, instance, and node IDs use the existing configuration identifier alphabet:
`^[A-Za-z][A-Za-z0-9_-]{0,63}$`, with lengths of 1 through 64 ASCII bytes. Comparison
is case-sensitive and exact. No trimming, case folding, Unicode normalization,
silent truncation, or path interpretation is permitted. Missing instance identity
must be rejected by instance-aware operations before mutation. A compatibility
launcher may explicitly select a documented instance value, but downstream code
must not independently invent defaults.

Execution IDs use canonical nonzero 128-bit values encoded as 32 lowercase hex
characters. They are generated from a cryptographically secure random source
independently for each activation; clocks, PIDs,
container names, and deterministic topology hashes cannot generate them. Failure
to generate or register a fresh ID prevents activation. An execution ID is public
correlation data, not a bearer credential.

Registration must authenticate the lifecycle authority and reject conflicting
active registrations atomically. Retired IDs cannot be reactivated through replay;
freshness checks must not rely on retaining an unbounded list of previous IDs.
Bound registration state and replay windows using the existing bounded control
and runtime-state mechanisms.

Ownership tokens retain the existing ledger representation and validation.
Instance-aware ownership uses ledger version 3; legacy unscoped ownership uses
version 2. Neither format equates ownership tokens with message identities.
Tokens are not operator API credentials.

Validation belongs in the authoritative C++ model; normalized JSON carries the
resolved non-secret identity to consumers, which enforce its schema at their
boundaries. Unknown nodes, duplicate node IDs, malformed identity components, and
inconsistent instance selections are errors before processes or resources start.
Secret material must never appear in normalized configuration.

## Lifecycle and freshness requirements

| Event | Required behavior |
|---|---|
| Two simultaneous deployments | Same graph/node IDs are allowed under distinct instance IDs; locks, ownership records, credentials, endpoints, and managed resources remain isolated |
| Duplicate start of one instance | Serialize on its instance lock; refuse a second active deployment rather than stealing ownership |
| Node process or VM restart | Preserve graph/instance/node identity; register a fresh execution ID before accepting commands or live telemetry |
| Collector restart | Revalidate authenticated live execution registration; never assign new execution IDs to still-running nodes just because the collector restarted |
| Connection reconnect | Preserve execution identity only when the same authenticated execution can be proved; otherwise require a new activation |
| Interrupted infrastructure creation | Preserve recorded ownership identity and partial transaction state for existing recovery; never mint a replacement token to adopt survivors |
| Recovery | Verify the recorded resource identities, configuration binding, and instance scope; refuse ambiguous or replaced resources |
| Completed destroy followed by create | Instance name may be reused; generate fresh ownership and execution identities, so stale targets cannot become valid again |
| Configuration change | Detect drift using the existing configuration-hash checks; no implicit reconciliation or adoption |

An external device without an intrinsic process identity is represented by a
trusted adapter's authenticated execution session. This proves the adapter session,
not physical-device identity. Losing that session invalidates the active target.
A QMP socket path alone likewise cannot prove a VM execution identity.

Each logical node has at most one registered active execution per instance in this
contract. Concurrent replicas require separate node IDs; implicit replica naming
and multi-writer execution registration are outside this contract.

## Authorization and observation requirements

Authenticate the principal and the runtime independently. Authorize the action
against the complete node key and bind the command to the registered execution.
Recheck that binding when dispatching and executing, and when accepting results.
A command queued before a restart must not be retargeted to the replacement.

Scope idempotency by authenticated principal and instance. Bind each stored request
to its action, node, execution, and payload. Reuse with a different target or payload
is a conflict, not a new command. Audit records preserve the original target even
after restart. Credential rotation changes credentials, not logical or execution
identity; old credentials never broaden instance scope.

Authenticate live observations against their registered node/execution tuple.
Reject unknown, mismatched, or retired executions from current health and counters.
Retained history and sealed captures keep their original attribution and may remain
queryable as historical evidence; they cannot establish current readiness.
Scope bounded queues, history queries, capture catalogs, and audit lookups by
instance. Transport reconnects and reused sequence numbers must not merge different
executions. Do not alter raw external-device payloads to invent GraphX identity;
the trusted observer supplies attribution at the observation boundary.

## Resource ownership and storage requirements

Derive managed names from the instance key and logical resource ID with an explicit
resource-kind domain. Generated names must respect platform limits, including
Linux's 15-byte interface-name limit. Store the logical-to-physical mapping in the
existing ownership ledger. Truncation or hash collisions must cause refusal, never
adoption. A generated name or matching ownership token alone is insufficient:
cleanup must still verify stable kernel, OVS, container, and process identities.

Scope Compose projects, ownership locks, shared-memory segments, Unix sockets,
credential files, runtime directories, capture exports, and QMP endpoints by
instance. Privileged state and high-I/O artifacts remain under `/var/lib/graphx`
on native Linux or inside Lima. Portable macOS/OrbStack storage uses its applicable
runtime boundary; privileged sockets are not forwarded to macOS.

Names do not isolate every resource. Explicitly detect conflicting published ports,
addresses/routes in shared namespaces, physical devices, external interfaces, and
operator-supplied paths before mutation. Serialize reservation where supported;
preflight alone is not a race-free reservation. Roll back partial acquisition
through the existing ownership lifecycle. External/shared resources are never
deleted merely because an instance references them.

## Implementation acceptance cases

Configuration selection and infrastructure isolation have executable coverage,
including interruption and recovery. Application restart, authorization, and live
observation cases below remain runtime requirements. Extend the nearest
configuration, ownership, control, and SDR tests.

| Case | Expected result |
|---|---|
| Empty, 65-byte, whitespace, Unicode, separator, or malformed identity | Reject before startup; valid 1-byte and 64-byte identifiers accepted |
| Same node IDs in two distinct instances | Independent startup and correctly attributed observations |
| Unknown node or duplicate ID within a graph | Reject before mutation |
| Competing starts for the same instance | At most one succeeds; no ownership takeover |
| Equal generated resource name or conflicting port/device | Refuse collision; preserve existing resources |
| Stop instance A while B carries traffic | B's processes, resources, commands, counters, and captures remain intact |
| Restart one node | Fresh execution ID; stale commands, acknowledgments, and live observations rejected |
| Collector reconnect or restart | Authenticated surviving execution retained; replay cannot register a retired one |
| Credential for A targets B | Denied and audited without exposing secrets |
| Idempotency key reused with a changed execution/payload | Conflict; no dispatch to the replacement |
| Interrupted create and identity-checked recovery | Only transaction-owned resources recovered; no fresh token adoption |
| Replaced resource or malformed ledger during cleanup | Fail closed; preserve replacement and evidence |
| Destroy/recreate using the same instance name | Old execution targets remain invalid; historical attribution preserved |

The end-to-end proving topology consists of two independently deployed copies of
a two-SDR graph, including restart, interrupted startup, recovery, and isolated
shutdown. Native Linux, Lima, TCG guest boot, and KVM evidence must be reported
separately. TAP lifecycle tests alone do not establish guest execution.

## Infrastructure resource resolution

`resolve_instance_resources(config)` resolves a logical configuration for the OVS
lifecycle. `graphx config normalize FILE --resources` exposes that same resolved
configuration to infrastructure consumers. Ordinary normalization retains logical
names. Always pass the logical YAML to lifecycle commands; resolved JSON is an
output for consumers, not a replacement input manifest.

With an explicit `deployment.instance_id`:

- State and lock filenames use a domain-separated SHA-256 of the structured
  graph/instance tuple. The ledger records both original IDs and the typed
  logical-to-physical name mappings.
- OVS bridges and managed interface names use 15-byte names; namespaces, Compose
  projects, and mirror names use 64-byte names. A collision is refused, not adopted.
  Target interface references in router routes resolve together with attachments.
- Capture directories gain the instance state key below the configured VM-native
  directory. Capture export requires the matching ready ledger and retains its
  exclusive destination creation and stable capture identity checks.
- The configuration digest binds both source bytes and effective normalized
  configuration, including environment overrides. Destroy/recover retain existing
  identity-based cleanup on configuration drift; routes and exports require an
  exact match.
- A protected authority lock serializes infrastructure mutation and collision
  preflight across instances sharing the state root. Each instance also has its
  own lock. Use one state root per authority; separate state roots do not provide
  shared reservation. Existing kernel/OVS collisions still fail closed.
- Manual `infra route apply|clear` requires a ready matching ledger and verified
  namespace inode/ownership marker. `--state-dir` selects the same authority used
  at creation. Dry runs require neither Linux nor existing resources.

Ledger version 3 rejects missing, duplicate, and incorrectly derived mappings.
Resource cleanup continues to check UUIDs, ifindices, namespace inodes, container
IDs, capture processes, and transaction ownership. Names alone grant no ownership.
Legacy configurations retain graph-ID filenames and literal resource names in
version 2 ledgers. Adding or changing an instance selects new resources; it never
adopts a legacy deployment. Stop legacy resources deliberately before migration.

## Current implementation boundary

The infrastructure resolver applies to native Linux and to Linux inside the
GraphX Lima VM. Native macOS can normalize and plan; OrbStack does not provide
managed OVS. The privileged two-instance test checks simultaneous namespace
traffic with identical addresses, isolated route changes and shutdown, interrupted
create/rollback/recovery, duplicate starts, and unowned/replaced bridge refusal.
TAP lifecycle tests still do not establish QEMU guest execution.

Application runtime policy remains keyed by configured node ID. Current example
launchers still contain fixed runtime directories, credentials, sockets,
shared-memory names, QMP paths, and published ports. Their migration and execution
identity enforcement remain separate from the OVS ownership implementation.
The resolved Compose project must be used when starting containers for an explicit
instance: infrastructure lookup refuses containers from a different project.
Changing only an instance ID does not make the existing full demo launchers safe
to run concurrently.

External attachments and physical devices are references, not managed resources;
they are neither renamed nor claimed nor removed by the OVS lifecycle. Published
ports and application credentials are also outside that lifecycle. Their owning
runtime must reserve shared resources and reject conflicts before starting them.
Isolated namespace routes and addresses may be repeated in different instances;
this does not authorize overlapping routes or addresses in a shared host namespace.
