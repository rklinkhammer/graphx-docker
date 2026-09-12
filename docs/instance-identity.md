# Instance identity contract

This contract defines instance-aware configuration and the requirements for runtime
isolation. The authoritative loader and normalized JSON support optional
`deployment.instance_id` and typed SDR source settings. Configuration consumers
can select a node against an explicit matching instance identity. Execution IDs,
instance-scoped infrastructure, and runtime enforcement are not implemented yet.
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

Ownership tokens retain the existing ledger representation and validation. This
contract does not change the ownership-state format or equate ownership tokens
with the canonical message-identity format. Tokens are not operator API credentials.

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

Configuration validation and selection cases have executable coverage. Runtime
isolation, restart, authorization, and recovery cases below remain requirements
for runtime implementation, not claims of current coverage. Extend the nearest configuration, ownership, control, and SDR tests.

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

## Current implementation boundary

The current infrastructure ledger and lock are keyed by graph ID; runtime policy
identities are keyed by configured node ID. Existing examples also contain fixed
resource names and paths. They do not yet satisfy the concurrent-instance contract.
Configuration supports explicit instance selection through the existing dotted-path
overrides; no new activation CLI, credential format, ledger layout, or wire format
is introduced. Missing instance IDs remain absent for existing configurations;
instance-aware node lookup and SDR source settings require an explicit ID. Current
launchers do not consume the new SDR settings or isolate resources by instance.
Do not use an instance override as a way to launch concurrent copies yet.
Existing running resources must be deliberately stopped before a later
identity-layout migration; migration must not silently adopt them.
