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

`GRAPHX_OVERRIDES` accepts comma-separated assignments. Explicit `--set` values
take precedence. The configuration version cannot be overridden.

## Normalized contract

`graphx config normalize` emits deterministic JSON with resolved defaults and one
shape for all consumers. The contract is defined by
`config/schema/normalized-graph.schema.json` and contains the graph, OVS network
model, deployment, and observability settings. It never contains credentials.

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
