# Configuration

`graphx.yml` with `version: 3` is the only authored format. The C++ loader resolves
catalog type instances and connections into normalized JSON contract version 2.
Version 2 authored files and normalized version 1 are deliberately rejected.

The implemented commands are `validate`, `inspect`, and `config normalize`.
`inspect` prints the resolved JSON. These commands read bounded files and do not
start processes, provision credentials, create infrastructure, or inspect a Docker
engine. `compile`, `run`, `infra`, and unconverted example launchers return
`E_PHASE_UNAVAILABLE`. The [implementation plan](../design/graph-generation/implementation-plan.md)
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
Catalog image identities are currently illustrative pins with execution unavailable
until release packaging supplies real artifacts.

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
configuration loader. Normalization does not generate Compose, binding files or
guest artifacts; deterministic artifact compilation is P3.
