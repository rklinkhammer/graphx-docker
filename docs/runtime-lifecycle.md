# Runtime lifecycle

GraphX transports expose typed receive outcomes: message, timeout, end of stream,
and cancellation. The convenience `receive()` method returns the message when that
is all a caller needs. Built-in transports implement bounded connection, send,
receive, backpressure, and shutdown behavior.

Infrastructure uses one create/status/recover/destroy lifecycle. The ownership
ledger records the graph ID, optional instance ID, resolved resource mappings,
configuration hash, owner token, expected objects, and stable identities for
resources that were created. Create publishes state
atomically; recover and destroy verify the complete identity set before mutation.
Collisions, replacements, partial ownership, and insecure state files fail closed.

The [instance identity contract](instance-identity.md) defines identity lifetimes,
restart freshness, and isolation requirements for instance-aware execution. Its
implementation-boundary section distinguishes infrastructure isolation from the
runtime activation and control requirements. Explicit instances use separate
ledgers and locks; legacy configurations retain graph-ID-keyed ownership. Manual
route changes require a matching ready ledger and a verified owned namespace.


## Instance-aware activation

For a configuration with `deployment.instance_id`, the trusted launcher provisions
one protected runtime identity JSON file for the instance. It extends the existing
runtime credential manifest; it is not a topology manifest. Version 2 requires
`graph_id`, `instance_id`, and exactly one `{id, secret_file}` entry for every
configured node. Each node has a different credential of at least 32 bytes.
For example, the two-source configuration uses:

```json
{
  "version": 2,
  "graph_id": "two-source-sdr",
  "instance_id": "lab-a",
  "nodes": [
    {"id": "sdr-east", "secret_file": "sdr-east.secret"},
    {"id": "processor-east", "secret_file": "processor-east.secret"},
    {"id": "sdr-west", "secret_file": "sdr-west.secret"},
    {"id": "processor-west", "secret_file": "processor-west.secret"}
  ]
}
```

The manifest directory and file must belong to the lifecycle user and must not be
writable by group or other. Keep the directory stable and protected against rename.
The CLI rejects symlinks and hard-linked manifests, bounds input to 64 KiB, takes
an exclusive lock, and publishes updates atomically. Secret files are provisioned
separately; activation does not create or expose credential material.

Before starting each node process, activate its registration:

```sh
execution=$(build/dev/graphx runtime activate examples/sdr-node/two-source/graphx.yaml \
  --identity-file /path/to/identities.json --node sdr-east)
```

Activation generates a fresh nonzero 128-bit ID and records `execution_id` in that
node's entry. It refuses a second activation while the entry is active. Supply
`GRAPHX_NODE_ID=sdr-east`, `GRAPHX_EXECUTION_ID="$execution"`, and that node's
`GRAPHX_TELEMETRY_SHARED_SECRET_FILE` to the process. Native GraphX executables read
`GRAPHX_CONFIG`; SDR adapters read `GRAPHX_NORMALIZED_CONFIG`. Neither the process
nor the collector invents an instance or execution ID.

After stopping and reaping the process, retire that exact registration:

```sh
build/dev/graphx runtime retire examples/sdr-node/two-source/graphx.yaml \
  --identity-file /path/to/identities.json --node sdr-east --execution-id "$execution"
```

Retirement refuses an obsolete ID. A crashed process leaves its slot active so
that the launcher must verify it has stopped before retirement and a fresh
activation. Do not reuse an execution ID, launch multiple processes from one
activation, or restore an old registration manifest. The launcher owns process
supervision; these CLI commands register identity and do not launch or kill a
process. A collector restart reads the surviving registrations without replacing
them. Credential rotation preserves the execution ID.

## Runtime enforcement and storage

Instance telemetry requires the matching version-2 runtime identity manifest.
Signed events carry `graphId`, `instanceId`, `nodeId`, and `executionId`. The
collector rejects unknown, inactive, cross-instance, retired, and non-adjacent
edge observations before updating live counters, history, captures, or control
endpoints. Replacement executions clear the affected live metrics and control
state; retained history keeps its original attribution. Commands bind to the
execution shown in the browser snapshot, and both dispatch and runtime execution
recheck that identity. Old acknowledgements cannot complete a new command.

Native processes and SDR telemetry use the same identity fields. Typed SDR source
settings select raw sample/control edges, credentials, frequency, and interval;
controllers select the source whose control edge originates at their node. The
sample wire format remains unchanged. An adapter identity proves its own process
session, not an external physical device's boot identity.

Normalized configuration includes the derived `deployment.resource_key` for an
explicit instance. Capture and history consumers use that same key below their
configured directories. Application capture filenames additionally identify the
node execution; creation is exclusive, preserving prior captures. History binds
its database metadata and queries to the graph/instance tuple. Prometheus labels,
OTLP traces, command responses, and audit records retain the applicable instance
and execution identities. External QEMU/diagnostic evidence needs matching
registered identity and a protected evidence file to affect instance readiness.

Configurations without an instance retain legacy node-based telemetry and
version-1 credential manifests. The [shared Compose launcher](compose-runtime.md) provisions credentials,
registers executions, and records container/network identities for supported
container graphs. Specialized laboratory launchers retain their documented limits.
Selecting an instance alone does not isolate operator-selected TCP/UDP ports,
shared-memory segments, Unix sockets, or QMP endpoints.
