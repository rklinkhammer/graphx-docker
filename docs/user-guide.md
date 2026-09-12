# GraphX user guide

This guide explains how to build GraphX, create a version-2 configuration, define
nodes and transports, describe Linux/Open vSwitch networking, configure packet
capture, and operate telemetry. `graphx.yaml` is the single authoritative source;
the C++ loader validates it and produces the normalized JSON consumed by telemetry.

## 1. Build GraphX

GraphX requires CMake 3.25 or newer, Ninja, OpenSSL 3, Python 3, and a C++20
compiler. Node.js 24 and npm are required for telemetry and the web console.

```sh
cmake --preset dev
cmake --build --preset dev -j 4
ctest --preset dev -L quick
```

The executables are written below `build/dev`; the command-line tool is
`build/dev/graphx`. To install the library, CLI, example node executables, schemas,
Wireshark integration, and documentation into a separate prefix:

```sh
cmake --install build/dev --prefix "$PWD/output/install"
```

Use `scripts/verify.sh portable` before sharing a normal change. System-OVS tests
must run as documented under [Linux and macOS networking](#8-linux-and-macos-networking).

## 2. Create a configuration

Start with the smallest graph that expresses the required nodes and edges. This is
a complete valid configuration:

```yaml
version: 2
graph:
  id: temperature-pipeline
  nodes:
    - id: sensor
      kind: source
      ports:
        - {name: readings, direction: output, schema: Temperature}
    - id: recorder
      kind: sink
      ports:
        - {name: readings, direction: input, schema: Temperature}
  edges:
    - id: readings
      from: sensor.readings
      to: recorder.readings
      transport: tcp
transport:
  tcp:
    readings:
      host: 127.0.0.1
      bind: 127.0.0.1
      port: 7201
      framing: u32be
```

Save it as, for example, `temperature.yaml`, then validate and inspect the resolved
model:

```sh
build/dev/graphx validate temperature.yaml
build/dev/graphx inspect temperature.yaml
build/dev/graphx config normalize temperature.yaml > normalized.json
```

The loader rejects unknown keys, wrong scalar types, dangling node/port/network
references, incompatible transport settings, graph cycles, and unsafe resource
definitions. The source schema is `config/schema/graphx.schema.json`, but the CLI
is authoritative because it also performs cross-reference and realization checks.

Configuration precedence is:

1. values in the YAML file;
2. dotted assignments in `GRAPHX_OVERRIDES`;
3. explicit `--set path=value` arguments.

For example:

```sh
GRAPHX_OVERRIDES='transport.tcp.readings.host=recorder' \
  build/dev/graphx validate temperature.yaml \
  --set observability.telemetry.port=9100
```

Use overrides for deployment-specific values, not as a second configuration file.
The `version` field cannot be overridden.

## 3. Configure nodes

Every node requires an `id`, a descriptive `kind`, and a `ports` array. Every port
requires a name, direction, and schema name. An edge must connect an `output` port
to an `input` port with the same schema.

```yaml
graph:
  id: processing-graph
  nodes:
    - id: filter
      kind: transform
      runtime: process
      execution: local
      lifecycle: managed
      control: graphx
      ports:
        - {name: input, direction: input, schema: Sample}
        - {name: output, direction: output, schema: FilteredSample}
```

Node fields have these meanings:

| Field | Supported values | Purpose |
| --- | --- | --- |
| `runtime` | `process`, `docker`, `qemu`, `external` | Technology hosting the node |
| `execution` | `local`, `host`, `container` | Execution boundary shown in topology |
| `lifecycle` | `managed`, `external` | Whether the declared deployment owns the node |
| `control` | `graphx`, `origin`, `none` | Control-plane behavior |
| `accelerator` | `auto`, `kvm`, `tcg`, `hvf` | QEMU-only requested accelerator |
| `architecture` | `x86_64` | QEMU guest architecture |

Defaults are `process`, `local`, `managed`, and `graphx`. Specify all four fields
for external or QEMU nodes so ownership is obvious. The supported QEMU TAP example
uses a host-executed, externally managed x86_64 guest with TCG.

The `kind` and schema strings are application vocabulary. GraphX validates their
identity and connectivity but does not maintain a registry of application-specific
kinds or payload schemas.

## 4. Connect nodes with transports

Each graph edge names one transport. A matching entry must exist under the
corresponding `transport` subsection and must use the edge ID as its key.

### TCP

```yaml
transport:
  tcp:
    readings:
      host: recorder
      bind: 0.0.0.0
      port: 7201
      framing: u32be
      connect_timeout_ms: 2000
      send_timeout_ms: 5000
      reconnect: true
      retry: {max_attempts: 60, initial_backoff_ms: 100, max_backoff_ms: 2000}
```

`host` is the connecting endpoint and `bind` is the listening address. Use
`framing: u32be` for GraphX envelopes and `framing: none` only for an explicitly
external raw-byte edge. TCP also supports a `tls` map containing `enabled`,
`verify_peer`, `require_client_certificate`, certificate paths, and `server_name`.

### UDP

```yaml
transport:
  udp:
    datagrams:
      mode: unicast
      destination: 127.0.0.1
      bind: 127.0.0.1
      port: 7301
      max_datagram_bytes: 1400
      receive_buffer_bytes: 65536
      send_buffer_bytes: 65536
      framing: u32be
```

Modes are `unicast`, `broadcast`, and `multicast`. Broadcast and multicast have
additional interface, TTL, loopback, and address requirements; use the current
examples in `examples/udp-broadcast` and `examples/udp-multicast` as templates.

### Local transports

```yaml
transport:
  unix:
    local-edge: {path: /tmp/graphx.sock, framing: u32be}
  in_process:
    internal-edge: {channel: internal, capacity: 64, backpressure: block}
  shared_memory:
    shared-edge:
      segment: graphx-shared
      capacity: 64
      max_message_bytes: 1048576
      backpressure: block
```

Use `in_process` only when both endpoints share a process. Unix sockets and shared
memory work across local processes. Queue backpressure is `block` or `reject` and
all configured capacities, sizes, and timeouts are bounded by the loader.

An edge with `data_plane: external` is displayed and observed by GraphX but is not
created by `TransportFactory`; its framing and network realization belong to the
external endpoint.

## 5. Describe deployment

The optional deployment section maps managed graph nodes to process metadata:

```yaml
deployment:
  project: temperature-pipeline
  services:
    sensor: {image: sensor:1.0, command: sensor-node}
    recorder: {image: recorder:1.0, command: recorder-node}
  telemetry: {service: telemetry, port: 8080}
```

This descriptive metadata is validated, normalized, included in configuration
projections, and displayed. It does not generate, create, or start services. Docker
Compose or another process manager must define the same service names, images,
commands, volumes, and management connectivity. GraphX separately realizes only
the declared Linux data-plane resources. A node marked `lifecycle: external` must
not appear in `deployment.services`; every managed node must appear when deployment
services are declared.

## 6. Configure networking

The `network` section serves two related purposes:

- `networks` and `edge_paths` document how logical edges traverse the topology;
- switches, routers, attachments, captures, and faults declare Linux resources
  owned by the `graphx infra` lifecycle.

A logical network without managed host resources can be as small as:

```yaml
network:
  networks:
    - id: data
      profile: ethernet
      subnets: [10.20.0.0/24]
      gateway: 10.20.0.1
      external: false
  edge_paths:
    readings: [sensor, data, recorder]
```

Managed networking uses system Open vSwitch. The names `macvlan`, `ipvlan-l2`,
`ipvlan-l3`, and `ipvlan-l3s` select validated OVS semantics; they are not Docker
network drivers. Non-Ethernet profiles require an `uplink`.

An OVS switch and container veth attachment look like:

```yaml
network:
  networks:
    - {id: data, profile: ethernet, subnets: [10.20.0.0/24], gateway: 10.20.0.1, external: false}
  switches:
    - id: br-data
      kind: openvswitch
      datapath: system
      ports:
        - {id: sensor-port, interface: gxsensor0, peer: gxsensor1}
  attachments:
    - id: sensor-data
      kind: container_veth
      owner: sensor
      network: data
      address: 10.20.0.10/24
      interface: gxsensor1
      peer: gxsensor0
      switch: br-data
```

Attachment kinds are:

- `container_veth`: moves one end of an owned veth pair into a named container;
- `namespace_veth`: moves one end into an owned Linux router namespace;
- `qemu_tap`: creates an owned TAP with explicit non-root `tap_uid` and `tap_gid`;
- `external`: describes a validated externally owned endpoint;
- `mirror`: identifies an OVS mirror output used for capture.

Routers can own interfaces, routes, forwarding, and accept/drop policies. Routes
with `install: create` are installed by `infra create`; `install: manual` routes can
only be changed through `graphx infra route apply|clear`. VLAN port metadata uses
an `access_tag` and/or `trunks` list. See `examples/static-route-policy` and
`examples/mixed-network` for complete realizations.

Always inspect a plan before allowing mutations:

```sh
build/dev/graphx infra create configuration.yaml --dry-run
sudo build/dev/graphx infra create configuration.yaml
sudo build/dev/graphx infra status configuration.yaml
sudo build/dev/graphx infra destroy configuration.yaml
```

The ownership ledger records stable kernel, OVS, process, and filesystem identities.
Destroy and recovery fail closed if a resource was replaced or ownership cannot be
proved. Do not manually rename or replace resources between lifecycle commands.

## 7. Configure PCAP capture

GraphX has two deliberately separate capture types.

### Application-envelope capture

Application capture records framed GraphX envelopes as bounded PCAPNG using the
USER0 link type:

```yaml
observability:
  capture:
    enabled: true
    provider: pcapng
    directory: captures
    snaplen: 16777220
    max_file_bytes: 67108864
    max_packets: 100000
```

Each GraphX node writes its own file in the configured directory. In the standard
Compose stack this directory is mapped to the `captures` volume. The telemetry
service mounts that volume read-only and exposes a bounded capture catalog and
download API. `tools/graphx-extcap` and `wireshark/graphx.lua` provide Wireshark
integration for the current envelope format.

### Ethernet/OVS capture

Network capture records Ethernet frames from an owned OVS mirror. It is available
only through the privileged Linux infrastructure lifecycle:

```yaml
network:
  switches:
    - id: br-observe
      kind: openvswitch
      datapath: system
      ports:
        - {id: capture, interface: gxcap0, peer: gxcap1}
      mirror: {id: observed-span, output_port: capture, select_all: true}
  attachments:
    - id: observed-span
      kind: mirror
      owner: br-observe
      interface: gxcap0
      switch: br-observe
  captures:
    - id: ethernet-span
      attachment: observed-span
      directory: /var/lib/graphx/captures/observed
      snaplen: 65535
      max_file_bytes: 67108864
      max_files: 4
      rotation_seconds: 300
      retention_seconds: 86400
```

The capture directory must be below `/var/lib/graphx/captures`. Live sessions are
root-owned and identity checked. Export a complete snapshot to a new destination
while the capture is healthy:

```sh
sudo build/dev/graphx infra capture export configuration.yaml \
  --capture ethernet-span --output /tmp/ethernet-span.pcapng
```

Do not attach a second unmanaged capture process to the mirror interface. Destroy
stops the owned capture and seals retained evidence read-only; retention cleanup
removes only unchanged, identity-proven sessions.

## 8. Linux and macOS networking

Portable process and Compose workloads run directly on Linux. On macOS, use
OrbStack for the ordinary Compose demo:

```sh
scripts/demo.sh start
scripts/demo.sh verify
scripts/demo.sh stop
```

System OVS, veth/TAP, namespaces, nftables, netem, network capture, and QEMU require
Linux. On Apple Silicon macOS they run in the dedicated GraphX Lima VM:

```sh
infrastructure/lima/start.sh
infrastructure/lima/verify.sh
scripts/network-lab.sh mixed-network plan
```

The checkout is mounted into Lima, but runtime state and high-I/O artifacts remain
under `/var/lib/graphx` in the guest. No privileged Docker or OVS socket is exposed
to macOS. Follow `infrastructure/lima/README.md` for provisioning, lifecycle, and
QEMU commands.

## 9. Configure telemetry

Runtime nodes emit metrics and trace events according to:

```yaml
observability:
  metrics: {enabled: true, exporters: [console, udp-json]}
  tracing: {enabled: true, exporters: [console, udp-json]}
  telemetry:
    host: telemetry
    port: 9000
    websocket: /ws
    heartbeat_interval_ms: 1000
    heartbeat_timeout_ms: 5000
```

The telemetry service accepts only normalized JSON produced by the C++ CLI. The
standard Compose stack creates that file in a one-shot `normalize-config` service,
then starts telemetry after normalization succeeds.

To run the repository’s generator/transform/sink stack with another compatible
configuration, provide its path before starting Compose:

```sh
GRAPHX_CONFIG_FILE="$PWD/my-graphx.yaml" docker compose \
  -f compose.yaml -f compose.history.yaml up --build
```

`GRAPHX_CONFIG_FILE` mounts the same YAML read-only into all three C++ node
containers and the normalization service. The file must retain node IDs and
transport roles compatible with those example executables. For different
applications, create a Compose file containing the required services and use the
same normalization pattern.

Keep functional policy in this file. In particular, telemetry heartbeat timing,
capture limits, history retention and queue bounds, and OTLP paths/retry/queue
limits are read from the validated `observability` section by every runtime
consumer. Use `GRAPHX_OVERRIDES` for temporary changes. Compose environment
settings are intentionally limited to secrets, bind/published ports, container
paths, collector placement, and operational enable/disable toggles.

Optional telemetry sections include:

```yaml
observability:
  history:
    enabled: true
    backend: sqlite
    database_file: /var/lib/graphx/history/history.sqlite
    retention_seconds: 86400
    max_records: 50000
    max_database_bytes: 67108864
    queue_capacity: 4096
    max_queue_bytes: 8388608
    batch_size: 100
    flush_interval_ms: 250
    query_limit: 200
    query_timeout_ms: 2000
    max_pending_queries: 16
    shutdown_timeout_ms: 2000
  otlp:
    enabled: false
    endpoint: https://collector.example/v1
    traces_path: /v1/traces
    metrics_path: /v1/metrics
    export_interval_ms: 5000
    timeout_ms: 2000
    queue_capacity: 1024
    max_queue_bytes: 8388608
    max_response_bytes: 65536
    retry_max_attempts: 3
    retry_initial_backoff_ms: 200
    retry_max_backoff_ms: 5000
  slos:
    window_seconds: 300
    minimum_window_seconds: 10
    availability_target: 0.99
    max_error_ratio: 0.01
    max_drop_ratio: 0.01
    max_p95_latency_us: 10000
```

Use `compose.history.yaml` for persistent SQLite storage,
`compose.observability.yaml` for Prometheus and Grafana, and the
`compose.otlp-secure.yaml` plus `compose.otlp-mtls.yaml` overlays for authenticated
TLS export. OTLP credentials and private keys belong in secret files, never in
`graphx.yaml`; normalized configuration intentionally contains no credentials.

Observation access, control access, and runtime event authentication are separate
credential domains. The simple demo generates local credentials in `.graphx`, while
`compose.control.yaml` demonstrates file-backed scoped control policy and per-node
runtime identities. Remote plaintext binding is rejected unless explicitly enabled.

Useful service endpoints include:

- `/api/health` and `/api/ready` for process and dependency state;
- `/api/graph/readiness` for node/edge readiness;
- `/api/topology` and `/api/metrics` for the current graph;
- `/api/history` and `/api/history/status` for bounded durable history;
- capture catalog/download endpoints for validated application PCAPNG files;
- authorized control and audit endpoints.

## 10. Recommended configuration workflow

1. Define node IDs, ports, schemas, and edges.
2. Add exactly one transport entry for every edge.
3. Validate the smallest file with `graphx validate`.
4. Add deployment metadata and logical `edge_paths`.
5. Add managed OVS resources only when the data plane requires them.
6. Enable application or Ethernet capture according to the evidence needed.
7. Configure telemetry, history, SLO, and OTLP limits.
8. Inspect normalized JSON and an infrastructure dry run.
9. Start processes or Compose services.
10. After testing, stop processes and run `infra destroy` for owned Linux resources.

Use `examples/` as current, focused reference configurations. Avoid copying runtime
state, generated normalized JSON, captures, ownership ledgers, or downloaded build
artifacts into a new configuration project.
