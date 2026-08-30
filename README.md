# GraphX

GraphX is a small, educational framework for describing a processing graph once, running its nodes in separate processes or containers, and inspecting what crosses each edge. This scaffold starts with a C++23 runtime, framed TCP and in-process transports, a three-stage demo, and a React Flow development console.

> Status: framework scaffold. The demo path, live telemetry, and four baseline
> transports work; capture interfaces are intentionally integration points rather
> than pretending a full Wireshark backend already exists.

## Run the complete demo

Requirements: Docker Engine or Docker Desktop with Compose, plus `curl`.

```sh
cd ~/workspace/graphx-docker
scripts/demo.sh start
```

The command builds and starts the complete portable system, waits for telemetry,
and proves that both TCP message counters advance. A healthy run prints five
`PASS` lines. Open [http://localhost:8080](http://localhost:8080); the console
should show **Traffic flowing**, all three nodes running, and increasing sample
counts. To see the doubled values arriving at the sink:

```sh
scripts/demo.sh logs
```

Use `Ctrl-C` to leave the log view, then stop the system with:

```sh
scripts/demo.sh stop
```

The complete user walkthrough, expected output, health checks, and troubleshooting
are in [`docs/complete-system-demo.md`](docs/complete-system-demo.md). Start there
before running the privileged network laboratories or the exhaustive developer
test suite.

## Build and test locally

Requirements: CMake 3.25+, Ninja, a C++20/23 compiler, and network access on the
first configure when yaml-cpp 0.9.0 is not already installed. The fallback
download is version-pinned and SHA-256 verified; see `THIRD_PARTY.md`.

```sh
cmake --preset dev --fresh
cmake --build --preset dev
ctest --preset dev
```

To exercise the complete portable feature surface (including C++20, finite TCP
and shared-memory pipelines, the browser build, telemetry API, Prometheus output,
and infrastructure dry-runs), run `scripts/test-features.sh portable`. Docker and
privileged native-Linux tiers are documented in
[`docs/test-procedure.md`](docs/test-procedure.md).

The library uses C++23 by default but only relies on broadly available C++20-era facilities. Set `CMAKE_CXX_STANDARD=20` if your toolchain needs it.

## Run the container demo manually

Requirements: Docker with Compose.

```sh
docker compose up --build
```

Open [http://localhost:8080](http://localhost:8080). The `generator` connects to `transform:7001`, and `transform` connects to `sink:7002`. Those hostnames are resolved by Docker's service-name DNS on the private `graphx` bridge network.

This foreground form is useful for reading raw logs. The guided
`scripts/demo.sh start` path above is preferred because it also verifies real
end-to-end traffic. Stop the foreground process with `Ctrl-C`, then run
`docker compose down`.

## Run the mixed network laboratory

GraphX now has a first-class network infrastructure layer with Docker bridge,
macvlan, and ipvlan definitions; node interfaces; Open vSwitch ports, VLAN
metadata, and SPAN mirrors; namespace routers, routes, nftables policies, and
`tc netem` fault hooks. Infrastructure lifetime is independent of Compose.

The exact macvlan/ipvlan example requires native Linux. A separate Docker Desktop
profile runs OVS in a privileged userspace-datapath container between two Docker
bridge domains:

```sh
./build/dev/graphx validate examples/mixed-network/graphx.yaml
./build/dev/graphx infra create examples/mixed-network/graphx.yaml --dry-run

# Native Linux
examples/mixed-network/scripts/linux-up.sh

# Docker Desktop for macOS (portable simulation)
examples/mixed-network/scripts/macos-up.sh
```

See [`examples/mixed-network/README.md`](examples/mixed-network/README.md) and
[`docs/network-infrastructure.md`](docs/network-infrastructure.md). Docker Desktop
does not support Docker's macvlan driver, so the macOS profile is deliberately
identified as a simulation rather than an exact substitute.

Three focused native-Linux examples isolate each driver/mode:

- [`examples/macvlan`](examples/macvlan/README.md): one macvlan L2 domain with
  explicit IP and MAC assignments;
- [`examples/ipvlan-l2`](examples/ipvlan-l2/README.md): one independent IPvlan L2
  network per node, connected through OVS and a namespace router;
- [`examples/ipvlan-l3`](examples/ipvlan-l3/README.md): one independent IPvlan L3
  subnet per node in Docker's supported multi-subnet network layout.

## Run the local shared-memory demo

The same three-node application can run as separate local processes over two
bounded POSIX shared-memory rings:

```sh
examples/shared-memory/run.sh
```

See [`examples/shared-memory/README.md`](examples/shared-memory/README.md) and
[`docs/shared-memory-transport.md`](docs/shared-memory-transport.md).

## Run the web console during development

```sh
cd web
npm install
npm run dev
```

Install and run the lightweight telemetry service separately with `npm install --prefix apps/telemetry && node apps/telemetry/server.mjs`. It reads `GRAPHX_CONFIG` (defaulting to the repository `graphx.yaml`) and derives the application graph, network path, transports, and heartbeat policy from that file. Vite proxies `/api` and the configured WebSocket path to port 8080. The console uses unavailable markers until node processes publish telemetry.

## Demo data path

```text
┌ generator ┐    TCP :7001     ┌ transform ┐    TCP :7002     ┌ sink ┐
│ Sample(n) │ ───────────────► │ value × 2 │ ───────────────► │ log  │
└───────────┘  u32be + Envelope └───────────┘  u32be + Envelope └──────┘
```

Each program is a separate executable and container entry point:

- `graphx-generator` emits a numbered `Sample` envelope at a configurable interval.
- `graphx-transform` listens for samples, doubles the text-encoded integer, and emits `TransformedSample`.
- `graphx-sink` listens and prints sequence, value, and trace ID.

The applications load and validate `graphx.yaml` before opening observability
exporters or transports. SIGINT/SIGTERM requests a bounded polling shutdown;
nodes stop taking work, close their transports, and exit without relying on a
forced container stop. In
Compose, listeners bind to `0.0.0.0`; clients use service names instead of fixed
container addresses. TCP retry, exponential backoff, connect/send deadlines, and
reconnect behavior are configured per edge. Listeners retain their listening
socket and accept a replacement client after disconnect.

## Architecture

The core contracts stay deliberately small:

- **Node** owns an identity and typed input/output `Port` descriptions. `FunctionNode` demonstrates an adapter for ordinary transformation functions.
- **Port** names a direction and payload schema. It is metadata, not a socket.
- **Edge** connects two named ports and selects a transport. `GraphConfig` is the validated, versioned source model and `TransportFactory` turns an edge's transport settings into TCP, Unix-domain socket, or in-process endpoints.
- **Network infrastructure** models address domains, node interfaces, OVS switches,
  mirrors, VLAN metadata, routers, routes/policies, and the ordered infrastructure
  path for each logical edge. `graphx infra` projects that model onto Linux and Docker.
- **Envelope** carries sequence, wall-clock timestamp, type, trace ID, string attributes, and opaque payload. Its versioned binary encoding is deterministic.
- **Transport** has `send`, timed `receive`, and `close`. `InProcessTransport`
  uses a synchronized queue; `TcpTransport` handles DNS; Unix-domain sockets use
  the same stream framing; and `SharedMemoryTransport` provides a bounded,
  process-shared ring for local IPC.
- **TraceSink** receives send/receive, connection/reconnect, backpressure,
  processing-duration, latency, byte-count, and error callbacks without coupling
  the runtime to one metrics stack. Metrics/histograms, fan-out, console,
  best-effort UDP JSON, and an optional bounded OTLP/HTTP JSON exporter are included.
- **CaptureSink / ExtcapProvider** mark the boundary for future serialized-frame capture, PCAPNG writing, and Wireshark extcap control.

TCP uses a four-byte unsigned big-endian length followed by one serialized
envelope. Frames are capped at 16 MiB before allocation. One receive deadline
covers both header and payload, and a timeout or closure during a partial frame
closes that connection to prevent stream desynchronization. Writes have a bounded
deadline, providing explicit blocking backpressure. Linux uses `MSG_NOSIGNAL` and
macOS uses `SO_NOSIGPIPE`, so a peer closure becomes an exception instead of
terminating the process. The envelope begins with `GXE` plus a version byte, so
future decoders can reject incompatible payloads cleanly.

When `reconnect` is enabled, an outbound send that detects a broken connection
reconnects and retries the complete frame once. This is intentionally
**at-least-once** behavior: if the connection failed after the peer accepted the
frame but before the sender could observe success, a duplicate is possible.
Consumers that require exactly-once effects must deduplicate by envelope sequence
or trace ID.

### Three related topologies

[`graphx.yaml`](graphx.yaml) is the authoritative source model and holds the logical graph, transport choices, network infrastructure, deployment hints, and observability settings. Version 1 is described by [`config/schema/graphx.schema.json`](config/schema/graphx.schema.json) and enforced by the C++ loader. The files in [`config/`](config/) are human-readable projections that make each concern easy to discuss. [`compose.yaml`](compose.yaml) is checked in as a static deployment projection for now.

Logical nodes contain only GraphX identity, kind, and ports. Container image and
command hints belong under `deployment.services`, so runtime semantics remain
independent of Docker and other schedulers.

The `graphx validate` command checks syntax, limits, identifiers, duplicate definitions, endpoint directions, schemas, transport settings, and cycles. `graphx inspect` prints the normalized model. A future `graphx generate` command may derive Compose/Kubernetes projections; until then, update `graphx.yaml` first and keep static projections aligned.

Network validation additionally checks IPv4 subnet membership, node/router
references, MAC syntax, VLAN ranges, mirror output ports, and logical-edge path
hops. The GUI can switch between the application graph and the infrastructure
path; selecting a logical edge highlights its macvlan/OVS/router/OVS/ipvlan path.

### Configuration commands

```sh
./build/dev/graphx validate graphx.yaml
./build/dev/graphx inspect graphx.yaml
./build/dev/graphx inspect graphx.yaml \
  --set transport.tcp.samples.host=127.0.0.1
./build/dev/graphx infra status examples/mixed-network/graphx.yaml --dry-run
```

The configuration path defaults to `graphx.yaml` and can be set with
`GRAPHX_CONFIG`. Scalar overrides use existing dotted paths. Precedence is:

1. `graphx.yaml`;
2. semicolon-separated `GRAPHX_OVERRIDES` entries;
3. repeated CLI `--set path=value` entries.

For example, a native three-process run can replace Compose DNS names with:

```sh
export GRAPHX_OVERRIDES='transport.tcp.samples.host=127.0.0.1;transport.tcp.transformed.host=127.0.0.1'
./build/dev/graphx-sink
./build/dev/graphx-transform
./build/dev/graphx-generator
```

Run each node in its own terminal. Override paths must already exist, which
prevents misspelled deployment settings from silently creating unused keys.

TCP edge settings accept:

```yaml
connect_timeout_ms: 2000
send_timeout_ms: 5000
reconnect: true
retry:
  max_attempts: 60
  initial_backoff_ms: 100
  max_backoff_ms: 2000
```

Retry backoff doubles up to `max_backoff_ms`. A timed receive that expires before
any frame byte arrives returns no envelope; expiry after a partial header or
payload is an error and closes the connection.

Version 1 limits configuration files to 1 MiB, graphs to 1,024 nodes and 4,096
edges, and nodes to 256 ports. Identifiers match
`[A-Za-z][A-Za-z0-9_-]{0,63}`. An edge must connect an output to an input with
the same schema. Cycles are rejected because the current blocking startup model
cannot safely schedule feedback graphs; this can change only through a
documented version or capability addition.

### Telemetry console

The browser application is divided along the product concepts:

- `Topology` owns React Flow, drag behavior, zooming, minimap, and ELK layered layout.
- `NodeCard` renders a process/container boundary and health metadata.
- `TelemetryEdge` overlays throughput and latency on a selectable edge.
- `EdgeInspector` presents connection/framing facts, edge metrics, recent messages, and trace/capture correlation placeholders.
- `server.mjs` serves the built console, receives runtime events over UDP,
  aggregates metrics, broadcasts WebSocket snapshots, and exposes
  health/topology/control endpoints plus Prometheus text output at `/metrics`.

Reset clears telemetry aggregation. Pause and fault buttons are explicit
development-control placeholders and return HTTP 501; the console surfaces that
result rather than pretending a runtime action occurred. Native netem fault hooks
are available through `graphx infra fault` and the example helpers.

## Repository layout

```text
.
├── apps/
│   ├── generator/          source process
│   ├── transform/          processing process
│   ├── sink/               consumer process
│   └── telemetry/          static web + JSON control service
├── config/                 graph/transport/deployment projections
├── docker/                 telemetry and userspace-OVS images
├── examples/               mixed, macvlan, IPvlan L2, and IPvlan L3 labs
├── include/graphx/         public C++ contracts
├── src/                    envelope, framing, and transports
├── tests/                  dependency-free unit/integration test runner
├── web/                    React + React Flow + ELK console
├── graphx.yaml             source topology model
├── compose.yaml            static Docker deployment
└── CMakeLists.txt
```

## Configuration reference

The demo accepts these variables:

| Variable | Used by | Default | Meaning |
|---|---|---:|---|
| `GRAPHX_CONFIG` | runtime nodes, CLI | `graphx.yaml` | Authoritative configuration path |
| `GRAPHX_OVERRIDES` | runtime nodes, CLI | empty | Semicolon-separated dotted scalar overrides |
| `GRAPHX_INTERVAL_MS` | generator | `500` | Emit interval |
| `GRAPHX_MAX_MESSAGES` | all demo nodes | `0` | Drain/stop after this many messages; zero runs continuously |
| `GRAPHX_TELEMETRY_HOST` | all nodes | `127.0.0.1` | Best-effort UDP telemetry collector |
| `GRAPHX_TELEMETRY_PORT` | all nodes, telemetry | `9000` | UDP event-ingest port |
| `GRAPHX_HEARTBEAT_TIMEOUT_MS` | telemetry | configured value | Operations/test override for offline detection |
| `GRAPHX_OTLP_HOST` | all nodes | empty (disabled) | OTLP/HTTP JSON collector host |
| `GRAPHX_OTLP_PORT` | all nodes | `4318` | OTLP/HTTP collector port |
| `GRAPHX_OTLP_PATH` | all nodes | `/v1/traces` | OTLP trace endpoint |
| `GRAPHX_WEB_ROOT` | telemetry | `web/dist` | Built frontend directory |
| `PORT` | telemetry | `8080` | HTTP service port |

## Extension guide

To add a transport, implement the three-method `Transport` interface, extend the
versioned schema and loader, and add the mapping to `TransportFactory`. TCP,
Unix-domain socket, and shared-memory implementations expose parallel
connect/listen constructors. Serialization stays above the transport layer so
the same envelope and tracing behavior can be compared across them. A future
specialized API may add zero-copy shared-memory views without expanding the
baseline contract prematurely.

To add observability, implement `TraceSink` and add its exporter name to the
typed `observability.metrics` or `observability.tracing` configuration. The
included OTLP/HTTP adapter turns envelope trace IDs into spans. Implement
`CaptureSink` to write exact framed bytes; an `ExtcapProvider` can then expose
that stream as a Wireshark interface and correlate packet blocks with trace IDs.

## Tests

`graphx-tests` covers:

- framing prefix and payload preservation;
- deterministic envelope serialization/deserialization;
- in-process delivery;
- shared-memory wraparound, bounded blocking and rejection policies, size and
  layout validation, cleanup, live-owner protection, stale-owner recovery, and
  cross-process delivery/crash detection;
- TCP request/reply across a loopback socket, including framing and envelope decoding;
- fragmented headers/payloads, consecutive frames, closure boundaries, maximum
  frame rejection, full-frame deadlines, reconnect, listener replacement, and
  cancellation without `SIGPIPE`;
- Unix-domain socket request/reply with the same envelope and framing contract.
- authoritative configuration loading and lookup;
- override precedence and typo rejection;
- aggregated semantic validation diagnostics;
- malformed and oversized configuration rejection;
- cycle rejection and all three transport-factory paths;
- the `graphx validate` CLI against the repository model.
- network-model parsing, reference validation, infrastructure planning, OVS mirrors,
  forwarding, Docker macvlan/ipvlan creation, and netem command generation.

The tests use no third-party framework so a fresh scaffold remains easy to build and study.

## Roadmap

1. **Runtime lifecycle follow-up** — coordinated SIGINT/SIGTERM shutdown, typed
   observability configuration, configuration-driven topology, heartbeat expiry,
   and richer transport events are implemented. Remaining hardening includes TLS
   and interruptible Unix-listener creation before its first peer connects.
2. **OpenTelemetry** — initial OTLP/HTTP spans, Prometheus-style edge metrics,
   connection/backpressure events, processing spans, and trace-ID propagation are
   implemented. Follow-ups include W3C trace-context fields, batching/retry, and
   collector conformance coverage.
3. **Wireshark integration** — PCAPNG custom blocks, extcap interface, and message-to-packet correlation in the edge inspector.
4. **Control plane** — authenticated runtime commands, topology validation/generation, and real container-health events.

## Design boundaries

This repository avoids a plugin registry, dependency-injection container, schema compiler, async runtime, and distributed control protocol at the scaffold stage. Those can be added when a concrete use case proves their shape. The current contracts are small enough to understand in one sitting and stable enough to support the next transport and telemetry adapters.

## License

MIT — see [`LICENSE`](LICENSE).
