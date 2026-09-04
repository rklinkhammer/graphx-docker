# GraphX

[![CI](https://github.com/rklinkhammer/graphx-docker/actions/workflows/ci.yml/badge.svg)](https://github.com/rklinkhammer/graphx-docker/actions/workflows/ci.yml)

GraphX is a small, educational framework for describing a processing graph once, running its nodes in separate processes or containers, and inspecting what crosses each edge. This scaffold starts with a C++23 runtime, framed TCP and in-process transports, a three-stage demo, and a React Flow development console.

> Status: framework scaffold. The demo path, live telemetry, five baseline
> transports, bounded correlated PCAPNG application capture, a GraphX
> Wireshark dissector, and a validated live-follow extcap adapter work.

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

For step-by-step use of the browser topology, observation/control credentials,
and application or Ethernet capture across the examples, see
[`docs/graphical-examples-guide.md`](docs/graphical-examples-guide.md).

## Build and test locally

Requirements: CMake 3.25+, Ninja, OpenSSL 3 development files, a C++20/23 compiler, and network access on the
first configure when yaml-cpp 0.9.0 is not already installed. The fallback
download is version-pinned and SHA-256 verified; see `THIRD_PARTY.md`.

```sh
scripts/verify.sh quick
```

To exercise the complete portable feature surface (including C++20, finite TCP
and shared-memory pipelines, the browser build, telemetry API, Prometheus output,
and infrastructure dry-runs), run `scripts/verify.sh portable`. Docker, quality,
release, and privileged native-Linux profiles are documented in
[`docs/test-procedure.md`](docs/test-procedure.md).

Before a pull request, run the combined local quality and Docker profile:

```sh
scripts/verify.sh full
```

The GitHub Actions workflow runs C++20 and C++23 on Linux and macOS, ASan/UBSan
on both hosts, repository-wide formatting, clang-tidy, cppcheck, bounded
libFuzzer smoke runs, portable integration, JavaScript audits, and Compose image
builds. It never opts into privileged macvlan/ipvlan/OVS host mutation.

The library uses C++23 by default but only relies on broadly available C++20-era facilities. Set `CMAKE_CXX_STANDARD=20` if your toolchain needs it.

## Versioned packages and releases

`VERSION` is the authoritative product version. Confirm a binary with
`./build/dev/graphx --version`. A local release candidate can be built and fully
verified without publication authority:

```sh
python3 scripts/release/build_release.py \
  --build-dir build/release-local \
  --output-dir outputs/release-local \
  --tag "v$(tr -d '\n' < VERSION)" \
  --allow-dirty
python3 scripts/release/verify_release.py outputs/release-local --source .
```

The development-only `--allow-dirty` flag is never permitted for a published
release. Each platform candidate contains an installable native archive, an
artifact-scoped SPDX 2.3 SBOM, a release manifest, and a strict SHA-256 checksum
set. The verifier binds these files to trusted tag, commit, platform, and source
epoch values, requires the complete canonical package layout, and bounds both
compressed and expanded archive work. The archive contract is an exact regular-
file inventory: programs use mode `0755`, data/libraries/headers/documentation use
`0644`, and missing, extra, or mode-altered entries are rejected. The native SBOM
describes GraphX, bundled yaml-cpp, and the externally linked OpenSSL version
selected by CMake. Exact
version tags drive an approval-gated workflow that stages
both multi-architecture images by run identity, attaches provenance and SBOM
attestations, and only then promotes non-replaceable version tags; it does not
publish `latest`.

See [`docs/release-process.md`](docs/release-process.md),
[`docs/compatibility-policy.md`](docs/compatibility-policy.md),
[`docs/upgrade.md`](docs/upgrade.md), [`SUPPORT.md`](SUPPORT.md), and
[`SECURITY.md`](SECURITY.md).

## Secure deployment boundary

Phase 5 adds optional TLS 1.3/mTLS to TCP graph edges, HTTPS/mTLS for telemetry,
separate observation and control credentials, HMAC-authenticated
anti-replay UDP telemetry/control, strict API limits, and least-privilege Compose
defaults. Telemetry binds to loopback by default and runtime controls remain
disabled unless a control credential and matching runtime identity model are
configured. Phase 8 adds a versioned least-privilege policy, distinct per-node
HMAC identities, attributable
commands, idempotency, expiry, correlated acknowledgements and bounded audit.
Arbitrary negative-ACK text is reduced to documented non-secret protocol codes
before it reaches command responses, audit, history, or logs. All credential
roles must contain mutually distinct values, and ordinary signed telemetry is
bounded, normalized, and filtered against active, newly rotated, and recently
superseded credentials before it reaches observation, export, or durable-history
consumers. File-only control/runtime rotation uses a bounded pre-fan-out reload
and 60-second redaction overlap. Deployments that may restart the collector
during that overlap project the old values through the bounded, expiring
`GRAPHX_PREVIOUS_CREDENTIALS_FILE`; those values are redaction-only and never
authenticate. See the coordinated rotation procedure before rotating production
credentials.
Secrets may be supplied inline or through mounted files; they do not belong in
`graphx.yaml`.

See [`docs/security.md`](docs/security.md) for configuration examples, API
authentication, limits and container policy, and
[`docs/control-plane.md`](docs/control-plane.md) for the Phase 8 boundary.

## Operations boundary

Phase 6 adds secure bounded OTLP/HTTP JSON export from the telemetry service,
separate process/service/graph health semantics, rolling in-memory SLO
evaluation, Prometheus alerts, and a provisioned Grafana operations dashboard.
The native C++ OTLP seam is intentionally loopback-only; remote export belongs
at the telemetry service's authenticated TLS boundary. Start the optional stack
with `docker compose -f compose.yaml -f compose.observability.yaml up -d --build`.
Use `compose.otlp-secure.yaml` to mount a bearer-token file and private CA, and
add `compose.otlp-mtls.yaml` when the receiver requires a client certificate.
`scripts/test-phase6-operations.sh` verifies the complete operations stack and
`scripts/test-phase6-secure-otlp.sh` verifies the secure Compose projection.
See [`docs/observability.md`](docs/observability.md) for signal semantics,
absolute request deadlines, bounded retry policy, endpoints, SLO formulas, and
deployment guidance.

## Durable telemetry history

Phase 7 adds optional SQLite-backed, metadata-only telemetry and SLO history.
Storage runs in a dedicated worker behind bounded queues, remains independent of
graph and service readiness, applies age/count retention at startup, during
idle operation, and on queries/writes, and exposes authenticated read-only
status and cursor APIs. Enable the persistent Compose volume with:

```sh
docker compose -f compose.yaml -f compose.history.yaml up -d --build
```

The console's **History** view reads the same observation-protected API; paging
older records pauses live refresh until the operator returns to the newest page.
`scripts/test-phase7-history.sh` proves authentication denial/success plus a
write and authenticated retrieval across a telemetry container restart. See
[`docs/history.md`](docs/history.md) for data scope,
limits, failure semantics, query filters, backup/restore, and schema rules.

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

## Run the PCAPNG capture demo

```sh
examples/capture/run.sh
```

This portable demo writes one correlated PCAPNG file per node. For the standard
Docker demo, use `GRAPHX_CAPTURE_ENABLED=true scripts/demo.sh start` and download
captures from the selected edge in the console. See
[`docs/capture.md`](docs/capture.md) for the file representation, Wireshark
dissector/extcap setup, resource limits, and USER0 limitations.

## Run the local shared-memory demo

The same three-node application can run as separate local processes over two
bounded POSIX shared-memory rings:

```sh
examples/shared-memory/run.sh
```

See [`examples/shared-memory/README.md`](examples/shared-memory/README.md) and
[`docs/shared-memory-transport.md`](docs/shared-memory-transport.md).

## Run the UDP examples

GraphX supports bounded IPv4 UDP unicast, broadcast, and multicast edges. The
unicast and multicast examples run locally; the broadcast example uses an
internal Docker subnet so it cannot transmit on a physical interface:

```sh
examples/udp-unicast/run.sh
examples/udp-multicast/run.sh
examples/udp-broadcast/run.sh
```

The broadcast image must be built or loaded first. Its runner disables image
builds and pulls so the actual isolated example can run without internet access;
see `examples/udp-broadcast/README.md`.

See [`docs/udp-transport.md`](docs/udp-transport.md) for delivery semantics,
limits, interface selection, MTU guidance, observability, and security limits.

## Run the web console during development

```sh
cd web
npm install
npm run dev
```

Install and run the lightweight telemetry service separately with `npm install --prefix apps/telemetry && node apps/telemetry/server.mjs`. It reads `GRAPHX_CONFIG` (defaulting to the repository `graphx.yaml`) and derives the application graph, network path, transports, and heartbeat policy from that file. Vite proxies `/api` and the configured WebSocket path to port 8080. The console uses unavailable markers until node processes publish telemetry.

The collector keeps sent and received message/byte counters separately, derives
rates over a five-second window, and publishes receive-latency histograms,
errors, drops, rejections, reconnects, backpressure, and process CPU. See
[`docs/observability.md`](docs/observability.md) for exact semantics and the
boundary between current capabilities and later production hardening.

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
- **Envelope** carries sequence, wall-clock timestamp, type, logical-message
  identity, trace/parent lineage, string attributes, and opaque payload. Its
  deterministic v2 encoding and v1 compatibility contract are specified in
  [`docs/protocol.md`](docs/protocol.md).
- **Transport** has `send`, typed timed `receive_result`, a compatibility
  `receive`, and idempotent `close`. Typed receive distinguishes a message,
  timeout, peer end-of-stream, and local cancellation. `InProcessTransport`
  uses a bounded synchronized queue with block/reject backpressure;
  `TcpTransport` handles DNS; Unix-domain sockets use
  the same stream framing; and `SharedMemoryTransport` provides a bounded,
  process-shared ring for local IPC.
- **TraceSink** receives send/receive, connection/reconnect, backpressure,
  processing-duration, latency, byte-count, and error callbacks without coupling
  the runtime to one metrics stack. Metrics/histograms, fan-out, console,
  best-effort UDP JSON, and an optional bounded OTLP/HTTP JSON exporter are included.
- **CaptureSink / PcapngCaptureSink / ExtcapProvider** record canonical framed
  envelopes with correlation metadata and preserve the boundary for alternate
  live capture providers. Per-file bytes, packet counts, and the telemetry
  catalog are bounded; the included
  extcap adapter validates and follows complete PCAPNG blocks through
  Wireshark's FIFO, and the Lua dissector exposes v1/v2 fields.

TCP uses a four-byte unsigned big-endian length followed by one serialized
envelope. Frames are capped at 16 MiB before allocation. One receive deadline
covers both header and payload, and a timeout or closure during a partial frame
closes that connection to prevent stream desynchronization. Writes have a bounded
deadline, providing explicit blocking backpressure. Linux uses `MSG_NOSIGNAL` and
macOS uses `SO_NOSIGPIPE`, so a peer closure becomes an exception instead of
terminating the process. The envelope begins with `GXE` plus a version byte.
Current readers accept legacy v1 and fixed-identity v2, reject unknown versions,
and enforce exact consumption and resource bounds. New factory-created messages
use v2; mixed deployments must upgrade consumers before producers.

When `reconnect` is enabled, an outbound send that detects a broken connection
reconnects and retries the complete frame once. This is intentionally
**at-least-once** behavior: if the connection failed after the peer accepted the
frame but before the sender could observe success, a duplicate is possible.
Consumers that require idempotent effects must deduplicate v2 traffic by
`message_id` within an application-defined retention window. Sequence is an
ordering value and trace ID is a causal-group value; neither is a unique-message
key. Version-1 traffic has no protocol-level message identity.

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
- `EdgeInspector` presents connection/framing facts, directional edge metrics,
  recent messages, live trace IDs, and correlated capture downloads.
- `server.mjs` serves the built console, receives runtime events over UDP,
  aggregates metrics, broadcasts WebSocket snapshots, and exposes
  health/topology/control endpoints plus Prometheus text output at `/metrics`.

Each node heartbeat includes process CPU usage measured over the heartbeat
interval. The node cards show sub-percent values with two decimal places, and
Prometheus exposes the same samples as `graphx_node_cpu_percent`. This is GraphX
process CPU—not aggregate host or Docker daemon CPU.

Edge counters and latency are measured from observed runtime events. Message and
wire-byte rates are derived from sends in the active five-second window. The UI
shows sent/received values independently and uses `—` when a value is genuinely
unavailable instead of displaying a synthetic zero.

Reset clears telemetry aggregation. Phase 8's versioned control policy scopes
named principals to actions and source nodes. Pause/resume commands carry UUIDs,
expiry, actor-scoped idempotency and per-node signed acknowledgements over the
HMAC telemetry channel. Policy deployments use distinct node secrets; the
single shared secret remains only in explicit legacy compatibility mode. Source nodes stop producing new envelopes while
in-flight work drains. The earlier `GRAPHX_CONTROL_TOKEN` route remains a
compatibility principal. See [`docs/control-plane.md`](docs/control-plane.md).
Native netem fault hooks remain separate through `graphx infra fault` and the
example helpers.

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
├── scripts/release/        candidate creation and offline verification
├── src/                    envelope, framing, and transports
├── tests/                  dependency-free unit/integration test runner
├── web/                    React + React Flow + ELK console
├── graphx.yaml             source topology model
├── VERSION                 authoritative product version
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
| `GRAPHX_PREVIOUS_CREDENTIALS_FILE` | telemetry | empty | Optional bounded, expiring redaction-only manifest used to preserve old-value filtering across restart |
| `GRAPHX_HEARTBEAT_TIMEOUT_MS` | telemetry | configured value | Operations/test override for offline detection |
| `GRAPHX_OTLP_HOST` | all nodes | empty (disabled) | Native OTLP/HTTP JSON collector host; loopback only |
| `GRAPHX_OTLP_PORT` | all nodes | `4318` | OTLP/HTTP collector port |
| `GRAPHX_OTLP_PATH` | all nodes | `/v1/traces` | OTLP trace endpoint |
| `GRAPHX_OTLP_ENDPOINT` | telemetry | configured value (disabled by default) | HTTPS OTLP/HTTP base endpoint; setting a nonempty value enables export |
| `GRAPHX_OTLP_AUTH_TOKEN[_FILE]` | telemetry | empty | Optional OTLP bearer credential; file form preferred |
| `GRAPHX_OTLP_CA_FILE` | telemetry | empty | Optional private CA bundle |
| `GRAPHX_OTLP_CERT_FILE`, `GRAPHX_OTLP_KEY_FILE` | telemetry | empty | Optional mTLS client credential pair |
| `GRAPHX_OTLP_EXPORT_INTERVAL_MS` | telemetry | `5000` | OTLP metric snapshot interval |
| `GRAPHX_OTLP_TIMEOUT_MS` | telemetry | `2000` | Per-request OTLP deadline |
| `GRAPHX_OTLP_QUEUE_CAPACITY` | telemetry | `1024` | Bounded export queue |
| `GRAPHX_OTLP_MAX_QUEUE_BYTES` | telemetry | `8388608` | Bounded export queue memory |
| `GRAPHX_OTLP_MAX_RESPONSE_BYTES` | telemetry | `65536` | Maximum collector response body; capped at 4 MiB |
| `GRAPHX_OTLP_RETRY_MAX_ATTEMPTS` | telemetry | `3` | Total attempts for retryable OTLP failures; 1–10 |
| `GRAPHX_OTLP_RETRY_INITIAL_BACKOFF_MS` | telemetry | `200` | Initial exponential retry delay; jittered |
| `GRAPHX_OTLP_RETRY_MAX_BACKOFF_MS` | telemetry | `5000` | Retry and `Retry-After` delay cap |
| `GRAPHX_CAPTURE_ENABLED` | nodes, telemetry | configured value | Enable or disable runtime capture |
| `GRAPHX_CAPTURE_PROVIDER` | nodes, telemetry | configured value | Capture provider: `pcapng` for application frames or established external `ovs-span` capture |
| `GRAPHX_CAPTURE_DIR` | nodes, telemetry | configured value | Shared capture-file directory |
| `GRAPHX_CAPTURE_SNAPLEN` | nodes, telemetry | `16777220` | Per-packet captured-byte limit; 256–16,777,220 |
| `GRAPHX_CAPTURE_MAX_FILE_BYTES` | nodes, telemetry | `268435456` | Per-node PCAPNG file limit; 64 KiB–4 GiB |
| `GRAPHX_CAPTURE_MAX_PACKETS` | nodes, telemetry | `1000000` | Per-node packet limit; 1–100,000,000 |
| `GRAPHX_CAPTURE_CATALOG_MAX_FILES` | telemetry | `128` | Maximum validated files returned in a capture catalog; 1–1,024 |
| `GRAPHX_CAPTURE_CATALOG_MAX_ENTRIES` | telemetry | `512` | Maximum directory entries examined per catalog refresh; catalog maximum–4,096 |
| `GRAPHX_WEB_ROOT` | telemetry | `web/dist` | Built frontend directory |
| `PORT` | telemetry | `8080` | HTTP service port |

## Extension guide

To add a transport, implement the three-method `Transport` interface, extend the
versioned schema and loader, and add the mapping to `TransportFactory`. TCP,
UDP, Unix-domain socket, and shared-memory implementations expose parallel
connect/listen constructors. Serialization stays above the transport layer so
the same envelope and tracing behavior can be compared across them. A future
specialized API may add zero-copy shared-memory views without expanding the
baseline contract prematurely.

To add observability, implement `TraceSink` and add its exporter name to the
typed `observability.metrics` or `observability.tracing` configuration. The
OTLP/HTTP adapters turn canonical envelope trace IDs into spans. Native direct
export is loopback-only; the telemetry-service adapter is the authenticated TLS
boundary for remote traces and metrics. See `docs/observability.md` for health,
SLO, Prometheus, and Grafana operation. The PCAPNG
capture sink writes exact canonical framed bytes and correlation comments;
the USER0 Lua dissector decodes both supported wire versions, and the extcap
adapter streams validated complete blocks. `ExtcapProvider` remains the C++
boundary for alternate live sources.

## Tests

On macOS with Docker Desktop (or another local Linux-container runtime), run the
workspace-owned Linux verifier for the focused TLS gate or the cumulative
portable suite:

```sh
scripts/test-linux-container.sh tls
scripts/test-linux-container.sh portable
scripts/test-linux-container.sh sanitizers
GRAPHX_FUZZ_SECONDS=30 scripts/test-linux-container.sh fuzz
```

Use `GRAPHX_CA_CERT=/absolute/path/to/root-ca.crt` or
`GRAPHX_CERT_INSTALL_SCRIPT=/absolute/path/to/install-certs.sh` when the Linux
image needs organization trust. See [`docs/test-procedure.md`](docs/test-procedure.md)
for all modes and the native-Linux networking boundary.

`graphx-tests` covers:

- framing prefix and payload preservation;
- deterministic envelope serialization/deserialization;
- exact 16 MiB envelope/frame acceptance, maximum-plus-one rejection, every
  proper v2 truncated prefix, and byte-stable v1/v2 golden vectors;
- in-process delivery;
- shared-memory wraparound, bounded blocking and rejection policies, size and
  layout validation, cleanup, live-owner protection, stale-owner recovery, and
  cross-process delivery/crash detection;
- TCP request/reply across a loopback socket, including framing, envelope
  decoding, and v1 followed by v2 on one live connection;
- UDP unicast delivery, malformed/truncated datagram recovery, cancellation,
  sequence-anomaly metrics, and two-listener multicast fan-out;
- fragmented headers/payloads, consecutive frames, closure boundaries, maximum
  frame rejection, full-frame deadlines, reconnect, listener replacement, and
  cancellation without `SIGPIPE`;
- Unix-domain socket request/reply with the same envelope and framing contract;
- repeated connected delivery, blocked-receive cancellation, cleanup and
  resource reuse for in-process, shared-memory, TCP, and Unix-domain transports,
  including TCP peer replacement;
- authoritative configuration loading and lookup;
- override precedence and typo rejection;
- aggregated semantic validation diagnostics;
- malformed and oversized configuration rejection;
- cycle rejection and all three transport-factory paths;
- the `graphx validate` CLI against the repository model.
- network-model parsing, reference validation, infrastructure planning, OVS mirrors,
  forwarding, Docker macvlan/ipvlan creation, and netem command generation.

The unit/integration tests use no third-party framework so a fresh scaffold
remains easy to build and study. Separate libFuzzer entry points continuously
exercise untrusted envelope and frame bytes under ASan/UBSan.

## Production-readiness roadmap

The authoritative sequence is below. The repository contains educational seams
and prototypes for some later capabilities, but those do not move their scheduled
production-hardening phase forward.

1. **Configuration** — schema, loader, validation, and transport factory (implemented).
2. **Runtime lifecycle** — bounded queues, cancellation, reconnect, and graceful shutdown
   (implemented, with repeated cross-transport stress coverage).
3. **Protocol** — specification, compatibility rules, and message/trace identities
   (implemented).
4. **Quality automation** — CI, sanitizers, fuzzing, static analysis, and expanded
   transport tests (implemented).
5. **Security** — authentication, TLS, API validation, and container hardening
   (implemented).
6. **Operations** — OpenTelemetry integration, health checks, SLOs, and dashboards
   (implemented).
7. **History** — durable or backend-driven telemetry history (implemented with
   an optional isolated SQLite backend and read-only API).
8. **Control plane** — authorized control and real runtime controls (implemented).
9. **Wireshark** — bounded PCAPNG, v1/v2 Lua dissector, and validated extcap
   implementation (implemented).
10. **Release engineering** — compatibility policy, packaging, and support processes
    (implemented; publication remains an explicit maintainer action).

## Design boundaries

This repository avoids a plugin registry, dependency-injection container, schema compiler, async runtime, and distributed control protocol at the scaffold stage. Those can be added when a concrete use case proves their shape. The current contracts are small enough to understand in one sitting and stable enough to support the next transport and telemetry adapters.

## License

MIT — see [`LICENSE`](LICENSE).
