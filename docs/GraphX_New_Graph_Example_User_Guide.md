# GraphX New Graph Example User Guide

**Audience:** Developers adding a runnable GraphX example  
**Repository:** `graphx-docker`  
**Document date:** 2026-09-03  

This guide creates a new three-node TCP example, validates the authoritative
GraphX model, runs it locally and in Docker, and adds it to regression testing.
It also explains when a new topology requires new C++ node executables.

## 1 Understand the example boundary

GraphX separates the logical graph from transport and deployment. The YAML model
defines nodes, typed ports, edges, transport settings, optional network paths,
deployment hints, and observability policy. C++ applications load that model and
create transports through `TransportFactory`.

The checked-in demo executables are intentionally concrete:

- `graphx-generator` expects node `generator` and output edge `samples`.
- `graphx-transform` expects node `transform`, input edge `samples`, and output
  edge `transformed`.
- `graphx-sink` expects node `sink` and input edge `transformed`.

You can create a configuration-only example when those IDs and the current
Sample-to-TransformedSample behavior fit the demonstration. A graph with new
node IDs, extra edges, different processing, or another message contract needs
new node executables. Section 11 covers that extension.

## 2 Choose the graph before choosing Docker

Write down the logical processing first. For this guide:

```text
generator.samples -> transform.samples
transform.transformed -> sink.transformed
```

Choose these properties for every edge:

- a unique bounded edge ID;
- one existing output port and one existing input port;
- identical schema names at both ends;
- one supported transport: `in_process`, `tcp`, `unix`, or `shared_memory`;
- explicit capacity, timeout, retry, and backpressure values required by that
  transport; and
- an optional network path that matches the declared interfaces and networks.

Do not add Docker, OVS, or telemetry details to the logical graph solely to make
a deployment tool happy.

## 3 Create the example directory

From the repository root:

```bash
mkdir -p examples/hello-graph
```

Use a lowercase, hyphenated directory name and bounded lowercase identifiers.
Keep these files together:

```text
examples/hello-graph/
  README.md
  graphx.yaml
  compose.yaml
```

Add scripts only when they perform meaningful lifecycle or infrastructure work.
A simple bridge example can use Compose directly.

## 4 Write the authoritative GraphX model

Create `examples/hello-graph/graphx.yaml`:

```yaml
version: 1
graph:
  id: hello-graph
  nodes:
    - id: generator
      kind: source
      ports:
        - { name: samples, direction: output, schema: Sample }
    - id: transform
      kind: transform
      ports:
        - { name: samples, direction: input, schema: Sample }
        - { name: transformed, direction: output, schema: TransformedSample }
    - id: sink
      kind: sink
      ports:
        - { name: transformed, direction: input, schema: TransformedSample }
  edges:
    - id: samples
      from: generator.samples
      to: transform.samples
      transport: tcp
    - id: transformed
      from: transform.transformed
      to: sink.transformed
      transport: tcp

transport:
  tcp:
    samples:
      host: transform
      bind: 0.0.0.0
      port: 7101
      framing: u32be
      connect_timeout_ms: 2000
      send_timeout_ms: 5000
      reconnect: true
      retry: { max_attempts: 60, initial_backoff_ms: 100, max_backoff_ms: 2000 }
    transformed:
      host: sink
      bind: 0.0.0.0
      port: 7102
      framing: u32be
      connect_timeout_ms: 2000
      send_timeout_ms: 5000
      reconnect: true
      retry: { max_attempts: 60, initial_backoff_ms: 100, max_backoff_ms: 2000 }

network:
  networks:
    - id: hello-net
      driver: bridge
      subnet: 172.31.10.0/24
      gateway: 172.31.10.1
      external: false
  interfaces:
    generator: [{ id: data, network: hello-net }]
    transform: [{ id: data, network: hello-net }]
    sink: [{ id: data, network: hello-net }]
  edge_paths:
    samples: [generator, hello-net, transform]
    transformed: [transform, hello-net, sink]

deployment:
  network: hello-net
  services:
    generator: { image: graphx-demo:latest, command: graphx-generator }
    transform: { image: graphx-demo:latest, command: graphx-transform }
    sink: { image: graphx-demo:latest, command: graphx-sink }

observability:
  metrics: { enabled: true, exporters: [console] }
  tracing: { enabled: true, exporters: [console] }
  capture: { enabled: false, provider: pcapng, directory: captures }
```

The model uses service DNS names for TCP connections. Local processes will use
temporary host overrides in section 7.

## 5 Build the CLI and validate the model

```bash
cmake --preset dev
cmake --build --preset dev -j "$(nproc)"
unset GRAPHX_CONFIG GRAPHX_OVERRIDES
./build/dev/graphx validate examples/hello-graph/graphx.yaml
./build/dev/graphx inspect examples/hello-graph/graphx.yaml
./build/dev/graphx infra create examples/hello-graph/graphx.yaml --dry-run
```

Expected validation result:

```text
examples/hello-graph/graphx.yaml: valid GraphX configuration version 1 (3 nodes, 2 edges, 1 networks)
```

`inspect` must show three nodes, two TCP edges, the bridge, and both network
paths. The dry run must not change the host. Validation intentionally honors
`GRAPHX_OVERRIDES`, so clear any settings left by another demo before checking
the file as written.

## 6 Add negative configuration checks

Copy the file to a temporary location and make one invalid change at a time.
Confirm validation fails for each case:

- change `sink.transformed` to a nonexistent port;
- change one side's schema name so the edge schemas differ;
- duplicate a node, port, edge, or network ID;
- remove `transport.tcp.samples` while the edge still selects TCP;
- set a port above 65535 or a timeout outside its allowed range;
- put an unknown key in a strict object;
- reference a missing element in an edge path; and
- create a processing cycle if the graph is expected to be acyclic.

Example:

```bash
cp examples/hello-graph/graphx.yaml /tmp/hello-invalid.yaml
sed -i 's/to: transform.samples/to: transform.missing/' /tmp/hello-invalid.yaml
if ./build/dev/graphx validate /tmp/hello-invalid.yaml; then
  echo 'ERROR: invalid model was accepted' >&2
  exit 1
fi
```

Do not keep generated invalid fixtures in the example directory unless they are
part of an automated test.

## 7 Run the graph as local processes

The YAML uses Compose DNS names. Override only the two connection hosts for a
local process run:

```bash
export GRAPHX_CONFIG="$PWD/examples/hello-graph/graphx.yaml"
export GRAPHX_OVERRIDES='transport.tcp.samples.host=127.0.0.1;transport.tcp.transformed.host=127.0.0.1'
export GRAPHX_MAX_MESSAGES=10
export GRAPHX_INTERVAL_MS=20
```

Start the processes in this order in three terminals:

```bash
./build/dev/graphx-sink
```

```bash
./build/dev/graphx-transform
```

```bash
./build/dev/graphx-generator
```

The sink should finish with:

```text
sink seq=10 value=20
```

The trace identifier follows the line and will vary. The transform multiplies
each input value by two. After the run:

```bash
unset GRAPHX_CONFIG GRAPHX_OVERRIDES GRAPHX_MAX_MESSAGES GRAPHX_INTERVAL_MS
```

If a process is stopped manually, use `SIGTERM` or `Ctrl-C` and confirm all
three processes exit without leaving listening sockets.

## 8 Create the Docker projection

Create `examples/hello-graph/compose.yaml`:

```yaml
name: graphx-hello
services:
  sink:
    build: ../..
    image: graphx-demo:latest
    entrypoint: [/usr/local/bin/graphx-sink]
    environment: { GRAPHX_MAX_MESSAGES: "10" }
    volumes: [./graphx.yaml:/etc/graphx/graphx.yaml:ro]
    networks: [hello-net]
    read_only: true
    tmpfs: [/tmp]
    cap_drop: [ALL]
    security_opt: [no-new-privileges:true]
    pids_limit: 128
    init: true

  transform:
    build: ../..
    image: graphx-demo:latest
    entrypoint: [/usr/local/bin/graphx-transform]
    environment: { GRAPHX_MAX_MESSAGES: "10" }
    volumes: [./graphx.yaml:/etc/graphx/graphx.yaml:ro]
    networks: [hello-net]
    depends_on: [sink]
    read_only: true
    tmpfs: [/tmp]
    cap_drop: [ALL]
    security_opt: [no-new-privileges:true]
    pids_limit: 128
    init: true

  generator:
    build: ../..
    image: graphx-demo:latest
    entrypoint: [/usr/local/bin/graphx-generator]
    environment:
      GRAPHX_MAX_MESSAGES: "10"
      GRAPHX_INTERVAL_MS: "20"
    volumes: [./graphx.yaml:/etc/graphx/graphx.yaml:ro]
    networks: [hello-net]
    depends_on: [transform]
    read_only: true
    tmpfs: [/tmp]
    cap_drop: [ALL]
    security_opt: [no-new-privileges:true]
    pids_limit: 128
    init: true

networks:
  hello-net:
    driver: bridge
    ipam:
      config:
        - subnet: 172.31.10.0/24
          gateway: 172.31.10.1
```

The GraphX model remains authoritative. Compose projects it into containers but
does not redefine logical ports, edges, schemas, or transport semantics.

## 9 Validate run and clean up Docker

```bash
docker compose -p gx-hello -f examples/hello-graph/compose.yaml config --quiet
docker compose -p gx-hello -f examples/hello-graph/compose.yaml up -d --build
docker compose -p gx-hello -f examples/hello-graph/compose.yaml logs --no-color
docker compose -p gx-hello -f examples/hello-graph/compose.yaml ps -a
```

Confirm the sink log contains `sink seq=10 value=20`. Then remove only this
example's resources:

```bash
docker compose -p gx-hello -f examples/hello-graph/compose.yaml down --remove-orphans
```

Do not use a broad prune command. If the subnet conflicts with an existing
network, choose a dedicated RFC 1918 subnet and update both YAML files together.

## 10 Add the example to documentation and tests

Create `examples/hello-graph/README.md` with purpose, supported platforms,
prerequisites, exact start/status/stop commands, expected sink output, limits,
security assumptions, and cleanup.

Add a row to `examples/README.md`. Add `hello-graph` to the configuration-test
list in `CMakeLists.txt`:

```cmake
foreach(example mixed-network macvlan ipvlan-l2 ipvlan-l3 shared-memory hello-graph)
```

The portable suite already discovers every `examples/*/graphx.yaml` for direct
validation, inspection, and infrastructure dry runs. The explicit CTest entry
makes the example visible as an individually named regression.

Run:

```bash
cmake --preset dev --fresh
cmake --build --preset dev -j "$(nproc)"
ctest --preset dev --output-on-failure -R 'graphx-config-(hello-graph|tests)'
scripts/test-features.sh portable
docker compose -p gx-hello -f examples/hello-graph/compose.yaml config --quiet
git diff --check
```

Review `git status --short` and ensure no captures, credentials, build products,
or private network details were added.

## 11 Create new node behavior

A truly different graph needs applications that use the new node and edge IDs.
Do not rename the YAML while continuing to run binaries that hardcode the old
IDs.

Use the existing applications as small reference implementations:

- `apps/generator/main.cpp` for a source and reconnecting output.
- `apps/transform/main.cpp` for receive, processing, lineage, and send.
- `apps/sink/main.cpp` for bounded receive and graceful termination.
- `apps/common.hpp` for configuration path, signals, telemetry, and runtime
  control integration.

For each new executable:

1. Load the model with `graphx::load_config`.
2. Resolve its own node and edges by their declared IDs.
3. Create each transport through `graphx::TransportFactory`; do not instantiate
   Docker or vendor-specific networking in application logic.
4. Use `receive_result(deadline)` and handle `message`, `timeout`,
   `end_of_stream`, `cancelled`, and failure distinctly.
5. Bound queues, message sizes, timeouts, retries, and shutdown.
6. Preserve trace identity and set parent/message identities according to the
   protocol rules when deriving an envelope.
7. Emit sanitized operational metadata and never log secrets or payloads by
   default.
8. Add the target and installation rule in `CMakeLists.txt`.
9. Copy the executable into the runtime image or create a dedicated image.
10. Add unit, integration, failure-path, sanitizer, and finite end-to-end tests.

If a new public header, executable, document, or installed file is added, update
the Phase 10 release inventory and its genuine-package mutation tests. The exact
release contract intentionally fails until new installed content is reviewed.

## 12 Select another transport

### Shared memory

Use `examples/shared-memory/graphx.yaml` as the reference. Shared memory is
single-producer/single-consumer and local to a compatible IPC namespace. Define
one segment, capacity, maximum message size, backpressure policy, and timeouts
per edge. It is not a cross-host transport.

### Unix domain sockets

Define one safe local socket path and bounded connection/send behavior per edge.
Ensure shutdown removes only the socket owned by the listener. Add stale-path,
symlink, permissions, cancellation, and reconnect tests.

### macvlan or IPvlan

Start from the matching checked-in example. Use explicit addresses and an
isolated dummy parent until a real network design is reviewed. Native driver
behavior requires Linux and rootful Docker.

### Mixed network with Open vSwitch

Start from `examples/mixed-network`. Declare switches, router namespace,
interfaces, routes/policies, mirrors, and edge paths in the GraphX model. Review
the dry-run plan before `infra create`. Supply `up`, `status`, fault, capture,
and `down` scripts with narrow resource names and ownership checks.

## 13 Add observability controls and capture safely

Enable optional features one at a time:

- Metrics and tracing may use console, UDP JSON, or bounded OTLP export.
- History is disabled by default and requires a protected SQLite volume.
- Runtime control requires distinct observation, control, and per-node HMAC
  credentials projected as files or secrets.
- PCAPNG capture contains full envelope payloads and attributes. Treat captures
  as sensitive operational evidence and set byte and packet limits.

Never store bearer tokens, HMAC secrets, private keys, or private CA material in
`graphx.yaml`, Compose YAML, the example README, or Git. Use environment
projection, Compose secrets, and protected files as described in
`docs/security.md`, `docs/control-plane.md`, and `docs/capture.md`.

## 14 Completion checklist

- [ ] Graph purpose and expected output are documented.
- [ ] Node, port, edge, transport, network, and path IDs are unique and valid.
- [ ] Port schemas agree across each edge.
- [ ] Every transport has explicit limits and deadlines.
- [ ] `graphx validate`, `inspect`, and infrastructure dry run pass.
- [ ] Important invalid variants fail.
- [ ] The finite local run reaches its exact expected value.
- [ ] Compose validates, runs non-root, and cleans up narrowly.
- [ ] The example has no embedded credentials or private infrastructure data.
- [ ] The example is listed in `examples/README.md` and registered in CTest.
- [ ] Portable, sanitizer, quality, and applicable Docker/network gates pass.
- [ ] Documentation matches observed behavior.
- [ ] New installed content is represented in the Phase 10 package contract.
