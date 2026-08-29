# GraphX

GraphX is a small, educational framework for describing a processing graph once, running its nodes in separate processes or containers, and inspecting what crosses each edge. This scaffold starts with a C++23 runtime, framed TCP and in-process transports, a three-stage demo, and a React Flow development console.

> Status: framework scaffold. The demo path, live telemetry, and three baseline transports work; capture interfaces are intentionally integration points rather than pretending a full Wireshark backend already exists.

## Quick start

### Build and test locally

Requirements: CMake 3.25+, Ninja, and a C++20/23 compiler.

```sh
cmake --preset dev --fresh
cmake --build --preset dev
ctest --preset dev
```

The library uses C++23 by default but only relies on broadly available C++20-era facilities. Set `CMAKE_CXX_STANDARD=20` if your toolchain needs it.

### Run the container demo

Requirements: Docker with Compose.

```sh
docker compose up --build
```

Open [http://localhost:8080](http://localhost:8080). The `generator` connects to `transform:7001`, and `transform` connects to `sink:7002`. Those hostnames are resolved by Docker's service-name DNS on the private `graphx` bridge network.

Stop the demo with `Ctrl-C`, then run `docker compose down`.

### Run the web console during development

```sh
cd web
npm install
npm run dev
```

Install and run the lightweight telemetry service separately with `npm install --prefix apps/telemetry && node apps/telemetry/server.mjs`. Vite proxies `/api` and `/ws` to port 8080. The console shows fallback preview values until node processes begin publishing events, then switches to WebSocket snapshots.

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

Environment variables supply TCP endpoints. In Compose, listeners bind to `0.0.0.0`; clients use service names instead of fixed container addresses. The generator and transform retry outbound connections during startup.

## Architecture

The core contracts stay deliberately small:

- **Node** owns an identity and typed input/output `Port` descriptions. `FunctionNode` demonstrates an adapter for ordinary transformation functions.
- **Port** names a direction and payload schema. It is metadata, not a socket.
- **Edge** connects two named ports and selects a transport.
- **Envelope** carries sequence, wall-clock timestamp, type, trace ID, string attributes, and opaque payload. Its versioned binary encoding is deterministic.
- **Transport** has `send`, timed `receive`, and `close`. `InProcessTransport` uses a synchronized queue; `TcpTransport` handles DNS; and `UnixDomainSocketTransport` provides the same framing and tracing on a local filesystem socket.
- **TraceSink** receives send, receive, latency, byte-count, and error callbacks without coupling the runtime to one metrics stack. Metrics, fan-out, console, and best-effort UDP JSON implementations are included.
- **CaptureSink / ExtcapProvider** mark the boundary for future serialized-frame capture, PCAPNG writing, and Wireshark extcap control.

TCP uses a four-byte unsigned big-endian length followed by one serialized envelope. Frames are capped at 16 MiB before allocation. The envelope begins with `GXE` plus a version byte, so future decoders can reject incompatible payloads cleanly.

### Three related topologies

[`graphx.yaml`](graphx.yaml) is the intended source model and holds the logical graph, transport choices, deployment hints, and observability settings. The files in [`config/`](config/) are human-readable projections that make each concern easy to discuss. [`compose.yaml`](compose.yaml) is checked in as a static deployment projection for now.

A future `graphx generate` tool should validate ports and schemas, then derive the projection files and Compose/Kubernetes output. Until that exists, update `graphx.yaml` first and keep the static files aligned.

### Telemetry console

The browser application is divided along the product concepts:

- `Topology` owns React Flow, drag behavior, zooming, minimap, and ELK layered layout.
- `NodeCard` renders a process/container boundary and health metadata.
- `TelemetryEdge` overlays throughput and latency on a selectable edge.
- `EdgeInspector` presents connection/framing facts, edge metrics, recent messages, and trace/capture correlation placeholders.
- `server.mjs` serves the built console, receives runtime events over UDP, aggregates metrics, broadcasts WebSocket snapshots, and exposes health/topology/control endpoints.

Pause, fault injection, and reset are development-control scaffolds. They update telemetry-service state; wiring those commands into runtime control channels is a later milestone.

## Repository layout

```text
.
├── apps/
│   ├── generator/          source process
│   ├── transform/          processing process
│   ├── sink/               consumer process
│   └── telemetry/          static web + JSON control service
├── config/                 graph/transport/deployment projections
├── docker/                 telemetry multi-stage image
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
| `GRAPHX_INPUT_HOST` | transform, sink | `0.0.0.0` | Listener bind address |
| `GRAPHX_INPUT_PORT` | transform, sink | `7001` / `7002` | Listener port |
| `GRAPHX_OUTPUT_HOST` | generator, transform | `127.0.0.1` | Destination DNS name/address |
| `GRAPHX_OUTPUT_PORT` | generator, transform | `7001` / `7002` | Destination port |
| `GRAPHX_INTERVAL_MS` | generator | `500` | Emit interval |
| `GRAPHX_TELEMETRY_HOST` | all nodes | `127.0.0.1` | Best-effort UDP telemetry collector |
| `GRAPHX_TELEMETRY_PORT` | all nodes, telemetry | `9000` | UDP event-ingest port |
| `GRAPHX_WEB_ROOT` | telemetry | `web/dist` | Built frontend directory |
| `PORT` | telemetry | `8080` | HTTP service port |

## Extension guide

To add a transport, implement the three-method `Transport` interface and map a new `transport:` value in the future topology loader. The TCP and Unix-domain socket implementations expose parallel connect/listen constructors. A likely next transport is:

```cpp
SharedMemoryTransport(segment_name, ring_capacity, edge_id, trace_sink);
```

Keep serialization above the transport layer so the same envelope and tracing behavior can be compared across TCP, UDS, and shared memory. A zero-copy SHM implementation may later add a specialized buffer view without expanding the baseline interface prematurely.

To add observability, implement `TraceSink`. An OpenTelemetry adapter can turn an envelope trace ID into spans and edge measurements into counters/histograms. Implement `CaptureSink` to write the exact framed bytes; an `ExtcapProvider` can then expose that stream as a Wireshark interface and correlate packet blocks with envelope trace IDs.

## Tests

`graphx-tests` covers:

- framing prefix and payload preservation;
- deterministic envelope serialization/deserialization;
- in-process delivery;
- TCP request/reply across a loopback socket, including framing and envelope decoding;
- Unix-domain socket request/reply with the same envelope and framing contract.

The tests use no third-party framework so a fresh scaffold remains easy to build and study.

## Roadmap

1. **TCP hardening** — reconnect policy, cancellation, TLS option, backpressure, and generated configuration loading.
2. **Shared memory** — bounded ring buffer, ownership protocol, and explicit backpressure behavior.
3. **OpenTelemetry** — OTLP spans, Prometheus-style edge metrics, and trace-context propagation.
4. **Wireshark integration** — PCAPNG custom blocks, extcap interface, and message-to-packet correlation in the edge inspector.
5. **Control plane** — authenticated runtime commands, topology validation/generation, and real container-health events.

## Design boundaries

This repository avoids a plugin registry, dependency-injection container, schema compiler, async runtime, and distributed control protocol at the scaffold stage. Those can be added when a concrete use case proves their shape. The current contracts are small enough to understand in one sitting and stable enough to support the next transport and telemetry adapters.

## License

MIT — see [`LICENSE`](LICENSE).
