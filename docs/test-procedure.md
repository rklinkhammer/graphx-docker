# GraphX feature test procedure

This procedure exercises the source model, all four transports, hardened TCP
behavior, telemetry and browser assets, Docker deployment, infrastructure
planning, and the optional native network laboratories. Start with the portable
tier. The privileged tier is deliberately never selected automatically.

## Test tiers

| Tier | Host | Coverage | Command |
|---|---|---|---|
| Portable | macOS or Linux | C++20/23, unit/integration tests, config/infra dry-runs, TCP and shared-memory process pipelines, web build, telemetry API and Prometheus output | `scripts/test-features.sh portable` |
| Docker | macOS or Linux with Docker | Portable tier plus the standard bridge-network Compose deployment | `scripts/test-features.sh docker` |
| Native network | Linux only | Portable tier plus real macvlan, IPvlan L2/L3, OVS, namespace routing, nftables and netem | `GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/test-features.sh linux-network` |

The script uses a temporary directory for logs, tears down its local processes,
and leaves build directories and installed JavaScript dependencies in place for
inspection. Override ports with `GRAPHX_TEST_HTTP_PORT` and
`GRAPHX_TEST_UDP_PORT` if 18080 or 19000 is occupied.

## Prerequisites

Portable tests need CMake 3.25+, Ninja, a C++20/23 compiler, Node.js/npm and
curl. Docker tests need Docker Engine/Desktop with Compose. Native network tests
also need a Linux host, Open vSwitch, iproute2, nftables and root/sudo access.
tcpdump or dumpcap is optional for capture checks.

Docker Desktop does not expose native macvlan/ipvlan semantics. On macOS, use the
mixed-network macOS profile to test the containerized userspace OVS simulation;
use native Linux for driver-accurate results.

## 1. Portable automated acceptance

Run:

```sh
scripts/test-features.sh portable
```

Expected results:

- CTest passes for both C++23 and C++20. This includes envelope/framing, TCP
  fragmentation, deadlines, reconnect and `SIGPIPE` handling; Unix sockets;
  shared-memory wraparound, ownership, process death and backpressure; config
  validation; and infrastructure-plan generation.
- Every `graphx.yaml` validates. Infrastructure create/status/destroy and a
  netem fault are rendered with `--dry-run` without changing the host.
- The TCP and shared-memory pipelines each deliver sequence 8 with value 16.
- Runtime output contains structured connection and processing events.
- The web production bundle builds.
- `/api/health`, `/api/topology`, and `/metrics` respond. Telemetry reset is
  accepted; pause returns HTTP 501 because an authenticated runtime control
  channel does not yet exist. The UI must not claim that rejected controls ran.

For a focused rerun:

```sh
ctest --test-dir build/dev --output-on-failure
examples/shared-memory/run.sh
```

## 2. Observe the live TCP pipeline

Start the telemetry service and the three nodes in separate terminals:

```sh
npm ci --prefix apps/telemetry
npm ci --prefix web
npm run build --prefix web
node apps/telemetry/server.mjs
```

```sh
export GRAPHX_CONFIG="$PWD/graphx.yaml"
export GRAPHX_OVERRIDES='transport.tcp.samples.host=127.0.0.1;transport.tcp.transformed.host=127.0.0.1'
./build/dev/graphx-sink
```

Run the transform with the same exports, then the generator. Open
`http://localhost:8080`. Verify that:

1. application and network-path views both render;
2. selecting `samples` highlights macvlan → OVS → router → OVS → ipvlan;
3. edge connection state changes to connected and message/latency values replace
   unavailable markers;
4. recent messages retain the same trace ID through the transform;
5. `curl http://localhost:8080/metrics` exposes edge counters, latency,
   connection, reconnect and backpressure series.

Set `GRAPHX_MAX_MESSAGES=20` on the generator for a finite run.

## 3. OTLP/HTTP trace export

Run any OTLP/HTTP collector that accepts JSON on port 4318, then add these
variables to each node:

```sh
export GRAPHX_OTLP_HOST=127.0.0.1
export GRAPHX_OTLP_PORT=4318
export GRAPHX_OTLP_PATH=/v1/traces
```

GraphX emits bounded asynchronous send, receive and processing spans using the
envelope trace ID as the trace identity. Collector failure does not stop graph
processing; when the bounded queue is full, new export records are dropped.
Verify that one envelope produces correlated `graphx.send`, `graphx.receive`,
and `graphx.process` spans and that `graphx.sequence`, `graphx.subject`, status,
and wire-byte attributes are present.

## 4. Docker bridge deployment

Run `scripts/test-features.sh docker`. Then, for manual inspection:

```sh
docker compose up --build
docker compose ps
curl http://localhost:8080/api/topology
curl http://localhost:8080/metrics
docker compose down --remove-orphans
```

All services should be running while the generator is active, sink logs should
show doubled values, and the telemetry snapshot should mark all three nodes as
running.

## 5. Native Linux network drivers

Review each generated plan before opting in:

```sh
./build/dev/graphx infra create examples/macvlan/graphx.yaml --dry-run
./build/dev/graphx infra create examples/ipvlan-l2/graphx.yaml --dry-run
./build/dev/graphx infra create examples/ipvlan-l3/graphx.yaml --dry-run
./build/dev/graphx infra create examples/mixed-network/graphx.yaml --dry-run
```

Then run the privileged tier:

```sh
GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/test-features.sh linux-network
```

The standalone macvlan demo verifies explicit container MAC addresses. The
IPvlan L2 demo verifies three independent Docker domains routed through their
OVS attachments. The IPvlan L3 demo verifies three independent node subnets on
the L3 parent path. The mixed demo verifies macvlan-to-ipvlan routing through
10.10.0.1 and 10.20.0.1, forwarding, nftables policy, OVS mirrors and a netem
apply/clear cycle.

During a manual run, use each example's `scripts/status.sh`. Confirm Docker
networks are external and owned by the infrastructure layer, and confirm the
node Compose projects have different project names. Do not interpret failed
host-to-macvlan-container pings as a routing failure: parent-host reachability is
blocked by macvlan design unless a host macvlan shim is added.

## 6. OVS mirrors, capture and fault behavior

With the mixed network running:

```sh
examples/mixed-network/scripts/fault.sh apply
examples/mixed-network/scripts/fault.sh clear
examples/mixed-network/scripts/capture.sh mac
examples/mixed-network/scripts/capture.sh ipv
```

In another terminal, inspect OVS bridge/port/mirror state and router interface
qdiscs. A faulted run should show the configured delay/loss on the selected
router interface and increased application latency or loss. Clearing the fault
must remove the netem qdisc. Capture hooks should receive traffic mirrored from
both bridge domains; stop the capture explicitly after collecting a short sample.

Capture-to-envelope correlation and PCAPNG custom blocks remain extension
points, so the current acceptance criterion is valid mirrored packet capture,
not a clickable Wireshark packet offset in the GUI.

## 7. macOS userspace-OVS simulation

On Docker Desktop:

```sh
examples/mixed-network/scripts/macos-up.sh
examples/mixed-network/scripts/status.sh
docker logs gx-ovs-ovs-router-1
examples/mixed-network/scripts/fault.sh apply
examples/mixed-network/scripts/fault.sh clear
examples/mixed-network/scripts/macos-down.sh
```

This checks the OVS/router/control shape in a privileged container using bridge
networks. It does not certify macvlan or ipvlan behavior.

## Cleanup and failure triage

Always use the matching `down.sh`/`linux-down.sh`/`macos-down.sh` before deleting
external networks. If a run stops unexpectedly, inspect `docker compose ls`,
`docker network ls`, `ip netns list`, and `ovs-vsctl show`, then rerun that
example's teardown helper. Shared-memory listeners unlink their segments during
normal shutdown and replace stale names on the next start.

For a failure, retain the failing CTest output, node logs, `graphx inspect`
output, Docker service status, infrastructure status, OVS state and router
routes/qdiscs. That evidence distinguishes a model-validation problem from a
transport, container, or host-networking problem.
