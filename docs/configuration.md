# Configuration

`graphx.yml` with `version: 3` is the only authored format. The C++ loader resolves
catalog type instances and connections into normalized JSON contract version 2.
Version 2 authored files and normalized version 1 are deliberately rejected.

The implemented commands are `validate`, `inspect`, `config normalize`, and
`node-settings --node ID --config FILE` for resolved node validation, and `compile`.
`inspect` prints the resolved JSON. These commands read bounded files and do not
start processes, provision credentials, create infrastructure, or inspect a Docker
engine. Compilation writes inspectable files only. `run plan` verifies a compilation
and prints its network resource plan without runtime operations. `run up|status|down`
provides [owned execution](execution.md); OVS requires explicit local Linux
privileged opt-in. Managed QEMU guests additionally require verified guest artifacts
and the dedicated Linux account; see [guest releases](../guests/README.md). `infra`,
scenario adapters and unconverted example launchers remain gated. The [implementation plan](../design/graph-generation/implementation-plan.md)
tracks the remaining execution adapters.

```sh
build/dev/graphx validate examples/sample-pipeline/graphx.yml --target orbstack
build/dev/graphx config normalize examples/sample-pipeline/graphx.yml --target lima > resolved.json
```

`--target` selects `native-linux`, `native-macos`, `orbstack`, or `lima`.
The validation default is `native-linux`, independent of the host. Native macOS
accepts native processes, while OrbStack accepts container placement. Managed OVS,
namespaces and QEMU require Linux or Lima. Target validation is a pure capability
check; success does not constitute execution evidence.

## Authored model

- `version`: integer 3.
- `catalog`: relative path from the graph file to a SHA-256 catalog lock.
- `graph`: one stable `id`.
- `nodes`: map from instance ID to catalog `type`, `execution`, optional
  `parameters`, credential references and observation declarations.
- `connections`: map from connection ID to `from`, `to`, transport and optional
  settings, explicit attachments, feedback declaration and security references.
- `network`: logical OVS networks, switches, routers, attachments, captures and
  connection paths. Router interfaces expand into attachments. Optional
  `router.interfaces[].port` selects a switch port and preserves its VLAN.
- `portable_network`: explicit subnet/address map when portable automatic
  allocation is insufficient.
- `platform`: bounded capture, history, console, control, OTLP and extension
  overrides. SQLite history defaults enabled.
- `credentials`: provider/identity/member descriptors only. The loader never opens these paths.
- `scenario`: bounded declarations of actions, never executed by normalization.

The [authored schema](../config/schema/graphx.schema.json) is closed at every
structured object. Catalog [type definitions](../config/schema/node-type.schema.json)
provide port direction, wire schema/encoding, transports, connection cardinality,
feedback support, defaults, execution kinds and application identity. The catalog
also owns control capability; telemetry does not infer it from sample node IDs.
The source catalog contains illustrative image pins. Container execution requires
a release catalog derived from verified OCI archives; native execution requires
a verified installed release. Mixed native/container applications are rejected
with `E_EXECUTION_MIX`; use separate graphs.

The default catalog trust root in a development build is `config/catalog` in its
source checkout. Use `--catalog-root DIR` for a relocated or installed catalog;
installations include it under `share/graphx/catalog`. The authored relative lock
path must resolve inside that explicit root. Absolute paths, escapes, symlink
components, duplicate lock entries and digest mismatches fail closed. Locks contain
at most 1,024 files, each at most 1 MiB, with a total at most 16 MiB.

Input is one YAML/JSON document of at most 1 MiB and 64 nesting levels. Explicit
YAML tags, anchors, aliases, merge keys, duplicate keys, unknown fields and wrong
scalar types are rejected. Node and connection limits are 1,024 and 4,096.
Credentials and diagnostics contain references and field paths, never credential
contents. There is no `--set` or environment override interpreter;
`GRAPHX_OVERRIDES` does not change the authored input.

## Resolved contract

The [normalized schema](../config/schema/normalized-graph.schema.json) closes all
nested structures, including node bindings, transport settings, resource models
and platform policy. The root contains `contract_version: 2`, `graph_version: 3`,
`graph_id`, target, catalog/input digests, ordered node and connection arrays,
network, portable network, platform, credential IDs and execution placeholders.
Both endpoints receive the same resolved transport settings. IDs and object maps
use lexicographic ASCII ordering. Logical OVS names expand to bounded deterministic
physical names; no runtime ownership token is fabricated by normalization.

Telemetry accepts only this resolved contract and validates it with the same
schema. Its existing standalone HTTP, history and capture modules remain testable
with explicitly supplied runtime wiring. Graph-driven credential/control-grant
staging is unavailable until P5 and fails before listeners open. `${GX_STATE}` and
other execution placeholders are resolved by later execution adapters, not the
configuration loader. Normalization does not write artifacts. The P3 compiler serializes this resolved
model into the node files and applicable plans described below.

## Application bindings

The C++ `load_node_settings` API reads one resolved node object, using the node
shape from the normalized schema and the release's embedded catalog type
contracts. It rejects mismatched or absent instance identity, unknown type or
revision, wrong ports/roles/schema/encoding, peer cardinality violations, malformed
parameters, invalid resolved endpoints and unbounded startup settings before
opening application resources. It does not interpret authored graphs.

`graphx node-settings --node ID --config FILE` validates this object and prints
canonical JSON. The SDR Python programs use this C++ command through `GRAPHX_CLI`
(default: the installed `graphx` executable), then consume its validated output.
There is no second authored parser or endpoint environment fallback.

The sample and UDP executables require `--node ID --config FILE` plus
`--release-file FILE --release-token TOKEN`. SDR service scripts accept the same
arguments. The token is a fresh 32–128 byte invocation identity supplied by the
caller. After binding listeners/local resources, each process flushes
`ready node=ID` to stdout. It then waits up to `startup.max_wait_ms` for a bounded,
regular, non-symlink release file containing exactly that token, with no newline.
Connectors and sample traffic stay held until release. SIGINT/SIGTERM interrupts
the wait; C++ TCP retry and shared-memory connection setup also accept cancellation.
TCP/UDP senders bind the resolved source address. The execution adapter owns
safe directory staging, ownership checks, readiness collection and release-file
creation by atomic rename; this application interface does not implement graph orchestration.

Ports such as `samples` are application type contracts, not connection IDs. The
source, transform and sink consume their resolved parameters and actual node/edge
identities. A transform rejects the wrong incoming wire type, malformed integer
samples and multiplication overflow. Application capture uses the resolved node
identity and capture limits. Null telemetry credential references disable network
telemetry; a non-null reference requires an explicitly supplied runtime secret
through the existing secret/file interface. Automatic credential staging and
GraphX transport TLS credential mapping remain gated for P5. Raw SDR control keeps
its existing explicit TLS file inputs and uses resolved endpoints/server names.

Node files are currently available through the library/resolved JSON contract;
P3 implements deterministic file generation. The native binding tests extract
node objects into temporary fixtures and supply their own barrier. They do not
establish compiled graph, container, OVS or guest execution acceptance.

## Deterministic compilation

`graphx compile FILE --output DIR --source-root DIR --credential-root DIR`
accepts the same `--target` and `--catalog-root` options as normalization. All
roots are explicit and may not contain symlinks or parent traversal; the output's
parent must exist. The source and credential roots are protected boundaries,
not instructions to build sources or read secrets. Output must be outside them,
the authored input directory and the catalog root. Use canonical absolute paths
(for example `/private/tmp` on macOS rather than its `/tmp` symlink).

The compiler emits a manifest with graph/catalog/input digests and hashes of every
other artifact. Node JSON files match normalized node objects exactly. Plans cover
the platform, credentials, fixed Compose/native launch templates and applicable
OVS/capture/guest/scenario work. Compose is emitted as canonical JSON in
`compose.yaml`, a valid YAML 1.2 representation. No compiler step invokes runtime
tools, builds artifacts, provisions credentials or probes infrastructure.

The manifest's `execution_available` and execution plan's `executable` indicate
whether the graph uses supported native, container, namespace or QEMU adapters. This is a capability
flag, not release verification. `artifact_identities_verified` remains false at
compilation; `graphx run` verifies actual installed files or OCI archive identities
before execution. Physical-device startup remains gated pending its uplink contract.

Publication uses private staging, fsync and exclusive atomic rename. An existing
output is never replaced, even with a matching manifest. Use a fresh output
name; `--replace` is refused until runtime ownership can establish inactivity.
See [P3 verification](../design/graph-generation/p3-verification.md) for the
artifact matrix, reproducibility evidence and remaining execution boundaries.

Catalog node-type revisions are positive integers from 1 through 2147483647.
Release image packaging increments the affected container type revisions and
repins the catalog lock together; see the [release process](release-process.md).

Scenario action fields and references are validated by the C++ loader. See
[explicit scenario execution](scenarios.md) for selection, laboratory compilation,
rotation bounds and recovery behavior.
