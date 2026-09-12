# Configuration

For an end-to-end walkthrough covering nodes, transports, managed networking,
capture, and telemetry, start with the [complete user guide](user-guide.md).

`graphx.yaml` is the single authoritative input. The loader accepts `version: 2`
and rejects unknown keys, wrong scalar types, invalid references, graph cycles,
inconsistent transport definitions, and contradictory network attachments.

The top-level sections are:

- `graph`: nodes, typed ports, and directed edges.
- `transport`: TCP, UDP, Unix-domain, in-process, and shared-memory settings.
- `network`: semantic networks, OVS switches, routers, attachments, paths,
  captures, and bounded faults.
- `deployment`: descriptive project, service, and telemetry placement metadata.
- `observability`: metrics, tracing, capture, OTLP, history, SLO, and control limits.

Validate and inspect a file with:

```sh
graphx validate graphx.yaml
graphx inspect graphx.yaml
```

Overrides use dotted paths and are applied before semantic validation:

```sh
graphx validate graphx.yaml --set observability.telemetry.port=9100
```

`GRAPHX_OVERRIDES` accepts semicolon-separated assignments. Explicit `--set` values
take precedence. The configuration version cannot be overridden.

Functional runtime policy has one owner: `graphx.yaml`. This includes telemetry
heartbeat timing, capture provider and limits, history retention/queue/query
limits, and OTLP paths, timing, retry, and queue limits. Use
`GRAPHX_OVERRIDES` for a temporary deployment-specific change instead of adding
a second environment variable for the same setting.

Environment variables are reserved for deployment wiring that cannot safely or
portably live in normalized configuration: credentials and secret-file paths,
host bind and published ports, container filesystem paths, and operational
feature toggles such as `GRAPHX_CAPTURE_ENABLED` and `GRAPHX_HISTORY_ENABLED`.
OTLP credentials remain environment- or secret-file-backed; an operator may set
`GRAPHX_OTLP_ENDPOINT` when collector discovery belongs to the deployment.

## Normalized contract

`graphx config normalize` emits deterministic JSON with resolved defaults and one
shape for all consumers. The contract is defined by
`config/schema/normalized-graph.schema.json` and contains the graph, OVS network
model, deployment, and observability settings. Consumers use that shape directly:
edges contain their endpoint and transport objects, while services and edge paths
remain ordered arrays. It never contains credentials.

```sh
graphx config normalize graphx.yaml > normalized.json
```

The repository Compose stack uses `graphx.yaml` by default. Set
`GRAPHX_CONFIG_FILE` to mount another compatible source file into the example
nodes and the one-shot normalization service:

```sh
GRAPHX_CONFIG_FILE="$PWD/my-graphx.yaml" docker compose up --build
```

This changes configuration input; it does not generate, create, or start services
from the `deployment` section. Docker Compose or another process manager owns
application execution and management connectivity. GraphX separately owns only
declared Linux data-plane resources.

The source schema is `config/schema/graphx.schema.json`. Semantic checks in the C++
loader remain authoritative where JSON Schema cannot express cross-reference or
host-realization constraints.

## Instance selection and SDR source settings

`deployment.instance_id` is optional for existing configurations. When present it
must be a nonempty, case-sensitive ASCII identifier matching
`[A-Za-z][A-Za-z0-9_-]{0,63}`. No instance name is invented when it is omitted.
It identifies a deployment selection, independently of `graph.id` and the Compose
`deployment.project` name. Configuration validation does not start processes.

Declare the field in the source before overriding it; the existing override
mechanism only changes paths that exist. Precedence remains source, then
`GRAPHX_OVERRIDES`, then explicit `--set` options:

```sh
build/dev/graphx config normalize examples/sdr-node/two-source/graphx.yaml \
  --set deployment.instance_id=lab-b
```

Normalized contract version 1 includes `deployment.instance_id` when supplied and
omits it otherwise. Ordinary configurations retain their existing normalized
shape. C++ consumers use `config.node_for_instance(instance_id, node_id)`;
normalized-JSON consumers use `selectNormalizedNode(config, {instanceId, nodeId})`
from `apps/telemetry/normalized-config.mjs`. Both reject missing or mismatched
instance selection and unknown nodes. These APIs select configuration; they do
not authenticate a principal or register a live execution.

A source node may declare typed `sdr` settings. `samples_edge` references an external
UDP edge originating at the source; `control_edge` references an external TCP edge
terminating at it. The sample receiver must also be the controller. Both edges
use `framing: none`; the control edge requires mutual TLS with peer verification
and a `server_name` matching the source credentials. Endpoints, addresses, and
ports stay in the existing transport configuration. The control edge's TLS
certificate/key references describe the client; the source's references describe
its server identity.

```yaml
sdr:
  samples_edge: samples-east
  control_edge: control-east
  frequency_hz: 100000000
  sample_interval_ms: 200
  credentials:
    ca_file: /run/sdr-east/ca.pem
    certificate_file: /run/sdr-east/server.pem
    private_key_file: /run/sdr-east/server.key
    server_name: sdr-east
```

The two edge references and all four credential reference fields are required.
Frequency defaults to 100000000 Hz and is bounded to 1000000–6000000000 Hz.
Sample interval defaults to 200 ms and is bounded to 20–60000 ms. These integer
fields reject quoted numbers. This interval controls packet emission, not an IQ
sample rate. Credential paths must be absolute, at most 1024 bytes, and free of
control characters. Server names use 1–253 ASCII letters, digits, dots, or hyphens,
starting with a letter or digit. Normalization emits resolved numeric defaults
and file references without opening files or serializing credential contents.
Inline credential properties are rejected. File existence, permissions, and TLS
material validity are runtime responsibilities.

Any node with `sdr` settings requires `deployment.instance_id`. Duplicate node IDs,
unknown or incorrectly directed edge references, and inconsistent controller
relationships fail authoritative validation. JSON schemas enforce structure and
bounds; graph-reference semantics are checked by the C++ loader and by the
normalized node-selection boundary.

The [two-source example](../examples/sdr-node/two-source/README.md) exercises this
configuration contract. The current SDR launchers still use their existing
environment settings. The OVS lifecycle resolves instance-scoped infrastructure;
`graphx config normalize FILE --resources` exposes its resolved names and Compose
project. Application adoption remains separate: these fields do not make
simultaneous full example launches safe. See the
[resource resolution contract](instance-identity.md#infrastructure-resource-resolution).
