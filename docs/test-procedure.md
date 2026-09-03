# GraphX feature test procedure

This document is the exhaustive developer/acceptance procedure. For the first
user run of the complete application, follow
[`complete-system-demo.md`](complete-system-demo.md); it has one start command,
observable success criteria, a live traffic check, and troubleshooting steps.

This procedure exercises the source model, all four transports, hardened TCP
behavior, authenticated runtime control, correlated GraphX/Ethernet PCAPNG and
extcap, browser assets, Docker deployment, infrastructure
planning, and the optional native network laboratories. Start with the portable
tier. The privileged tier is deliberately never selected automatically.

## Test tiers

| Tier | Host | Coverage | Command |
|---|---|---|---|
| Quality | macOS or Linux | Repository formatting, clang-tidy, cppcheck | `scripts/check-format.sh && scripts/run-static-analysis.sh` |
| Sanitizers | macOS or Linux | Complete CTest under ASan and UBSan | `cmake --preset sanitizers && cmake --build --preset sanitizers && ctest --preset sanitizers` |
| Fuzz | Clang host | Envelope and frame libFuzzer targets under ASan/UBSan | `GRAPHX_FUZZ_SECONDS=30 scripts/run-fuzz.sh` |
| Portable | macOS or Linux | C++20/23, unit/integration tests, config/infra dry-runs, TCP and shared-memory process pipelines, graceful SIGTERM, web build, configuration-driven telemetry, heartbeat expiry, API and Prometheus output | `scripts/test-features.sh portable` |
| Docker | macOS or Linux with Docker | Portable tier plus the standard bridge-network Compose deployment | `scripts/test-features.sh docker` |
| Native network | Linux only | Portable tier plus real macvlan, IPvlan L2/L3, OVS, namespace routing, nftables and netem | `GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/test-features.sh linux-network` |

The script uses a temporary directory for logs, tears down its local processes,
and leaves build directories and installed JavaScript dependencies in place for
inspection. Override ports with `GRAPHX_TEST_HTTP_PORT` and
`GRAPHX_TEST_UDP_PORT` if 18080 or 19000 is occupied.

## Prerequisites

Portable tests need CMake 3.25+, Ninja, OpenSSL 3 and its development headers, a
C++20/23 compiler, Node.js/npm and curl. Docker tests need Docker Engine/Desktop with Compose. Native network tests
also need a Linux host, Open vSwitch, iproute2, nftables and root/sudo access.
tcpdump or dumpcap is optional for capture checks.

Quality checks additionally need clang-format-18, clang-tidy-18, and cppcheck. Fuzzing
needs Clang with libFuzzer and `xxd`. Override tool names with `CLANG_FORMAT`,
`CLANG_TIDY`, and `CPPCHECK`; override build directories with
`GRAPHX_QUALITY_BUILD_DIR` and `GRAPHX_FUZZ_BUILD_DIR`.

Docker Desktop does not expose native macvlan/ipvlan semantics. On macOS, use the
mixed-network macOS profile to test the containerized userspace OVS simulation;
use native Linux for driver-accurate results.

## 1. Quality automation

The checked-in workflow at `.github/workflows/ci.yml` runs on pushes to `main`,
pull requests, a weekly schedule, and manual dispatch. Its external actions are pinned to immutable
commit SHAs. The matrix covers C++20 and C++23 on Ubuntu and macOS. Separate jobs
run ASan/UBSan, clang-format 18, clang-tidy 18, cppcheck, bounded libFuzzer smoke
runs, the portable feature suite, npm production audits, and Compose image
builds. Workflow permissions are read-only and duplicate branch runs are
cancelled.

Run the same focused gates locally:

```sh
scripts/check-format.sh
scripts/run-static-analysis.sh
cmake --preset sanitizers
cmake --build --preset sanitizers
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --preset sanitizers
GRAPHX_FUZZ_SECONDS=30 scripts/run-fuzz.sh
```

Apple's sanitizer runtime may not provide LeakSanitizer in every toolchain. The
CI macOS job disables leak detection explicitly while retaining ASan/UBSan; the
Ubuntu job requires leak detection. Sanitized binaries are test artifacts and
must not be shipped as production executables.

The sanitizer configuration compiles every GraphX-owned library, application,
test, and enabled fuzzer translation unit with ASan/UBSan and frame pointers.
The `graphx-sanitizer-coverage` CTest audits `compile_commands.json` and fails if
any GraphX-owned source is linked to the sanitizer runtime without also being
compiled with instrumentation. Fetched yaml-cpp remains deliberately outside
that project-owned instrumentation policy. The fuzz runner invokes the same
audit after configuring its fuzzer targets, so the enabled fuzzer translation
units are covered as well.

The fuzz script converts the exact v1/v2 golden fixtures into a temporary seed
corpus. `graphx-envelope-fuzz` exercises parsing, canonical reserialization, and
round-trip invariants. `graphx-frame-fuzz` exercises length-prefix validation,
framing invariants, and framed envelope decoding. Routine CI uses a bounded
30-second smoke run for each target; the weekly run uses 300 seconds per target,
and local campaigns can set `GRAPHX_FUZZ_SECONDS` without changing the harness.

Privileged Linux network laboratories remain manual opt-in tests. CI validates
their configuration and dry-run plans but never mutates runner networking.

## 2. Portable automated acceptance

Run:

```sh
scripts/test-features.sh portable
```

Expected results:

- CTest passes for both C++23 and C++20. This includes envelope/framing, TCP
  fragmentation, deadlines, reconnect and `SIGPIPE` handling; Unix sockets;
  shared-memory wraparound, ownership, process death and backpressure; config
  validation; infrastructure-plan generation; exact v1/v2 golden vectors;
  identity lineage; and malformed-envelope rejection.
- An ephemeral-credential TLS test proves TLS 1.3 mutual-authentication and
  reconnect round trips, and rejects a peer-name mismatch, an untrusted CA, and a
  missing client certificate. Telemetry tests reject bad HMACs, stale timestamps,
  replay, unknown identities, unbounded values, rate-state overflow, and malformed
  HTTP/WebSocket targets while proving that the server remains healthy.
- Every `graphx.yaml` validates. Infrastructure create/status/destroy and a
  netem fault are rendered with `--dry-run` without changing the host.
- The TCP and shared-memory pipelines each deliver sequence 8 with value 16.
- Generator, transform, and sink exit cleanly after finite runs and SIGTERM.
- Runtime output contains structured connection and processing events.
- Browser credential/control helpers pass their tests and the web production
  bundle builds.
- `/api/health`, `/api/topology`, and `/metrics` respond. An unauthenticated
  pause is rejected; bearer- and HMAC-authenticated pause/resume is delivered and acknowledged.
  The real TCP generator's sent counter stops while paused and advances again
  after resume.
- `/api/topology` reflects the nodes, edges, transport details, and network paths
  in `GRAPHX_CONFIG`; a silent node transitions to `offline` after its configured
  heartbeat timeout.
- Node heartbeats update measured process CPU in `/api/topology`, the node cards,
  and the `graphx_node_cpu_percent` Prometheus gauge.
- Edge telemetry keeps sent/received message and wire-byte counters separate,
  derives five-second message/byte rates, exports a valid latency histogram, and
  counts errors, drops, rejections, reconnects and backpressure independently.
- The finite TCP pipeline writes valid non-empty PCAPNG files for all three
  nodes, retains trace IDs in capture metadata, and passes a byte-for-byte
  extcap and HTTP-download check. Binary unit tests verify USER0 (147) for
  GraphX frames and Ethernet (1) for actual Ethernet-frame capture.
- The container job runs `scripts/test-container-hardening.sh` after image builds
  to prove both fresh-volume initialization orders allow native capture writes,
  while the collector can read but cannot write the mounted volume.

For a focused rerun:

```sh
ctest --test-dir build/dev --output-on-failure
examples/shared-memory/run.sh
```

## 3. Observe the live TCP pipeline

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
3. edge connection state changes to connected; sent/received values advance and
   rates replace unavailable markers;
4. mean and p95 receive latency appear, while metric provenance identifies
   counters as measured and five-second rates as derived;
5. recent messages retain the same trace ID through the transform;
6. `curl http://localhost:8080/metrics` exposes directional counters, latency
   histogram buckets/sum/count, errors, drops, rejections, connection, reconnect
   and backpressure series.

Set `GRAPHX_MAX_MESSAGES=20` on the generator for a finite run.

When returning to configuration validation or another example in the same
shell, clear the TCP-only settings:

```sh
unset GRAPHX_CONFIG GRAPHX_OVERRIDES GRAPHX_MAX_MESSAGES GRAPHX_INTERVAL_MS
```

CTest and `scripts/test-features.sh` isolate themselves from these variables,
but a direct `graphx validate` command intentionally honors exported overrides.

## 4. OTLP/HTTP trace export

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
and `graphx.process` spans; that every operation has a distinct, valid non-zero
span ID even across node processes; and that `graphx.sequence`,
`graphx.subject`, status, and wire-byte attributes are present.

The native test suite also initializes GraphX identity state before `fork()`,
exports one parent and one child span for the same trace, and requires distinct
span IDs. This guards prefork worker lifecycles that same-process exporter tests
cannot exercise.

Native direct export intentionally accepts loopback collectors only. To verify
the secure telemetry-service path, configure `GRAPHX_OTLP_ENDPOINT` with an
HTTPS collector and optionally `GRAPHX_OTLP_AUTH_TOKEN_FILE`,
`GRAPHX_OTLP_CA_FILE`, and the `GRAPHX_OTLP_CERT_FILE` / `GRAPHX_OTLP_KEY_FILE`
pair. Confirm `/metrics` advances `graphx_otlp_exports_total`, failure does not
interrupt the graph, the queue never exceeds its configured item or byte cap,
and tokens and payloads do not appear in logs or metrics. The telemetry unit
suite includes live UDP-to-authenticated-OTLP and private-CA/mTLS integration
tests plus transient/permanent retry classification, `Retry-After`, absolute
slow-drip deadline, shutdown-during-backoff, collector refusal followed by
recovery, queue pressure during retry backoff, oversized-response, byte-cap,
and forced all-zero identifier failure tests.

For the hardened Compose projection, set host paths and validate both overlays:

```sh
export GRAPHX_OTLP_ENDPOINT=https://otel-collector.example:4318
export GRAPHX_OTLP_AUTH_TOKEN_FILE=/host/secrets/otlp-token
export GRAPHX_OTLP_CA_FILE=/host/secrets/otlp-ca.pem
export GRAPHX_OTLP_CERT_FILE=/host/secrets/otlp-client.pem
export GRAPHX_OTLP_KEY_FILE=/host/secrets/otlp-client-key.pem
docker compose -f compose.yaml -f compose.otlp-secure.yaml \
  -f compose.otlp-mtls.yaml config --quiet
```

Verify operational endpoints independently:

```sh
curl -f http://127.0.0.1:8080/api/live
curl -f http://127.0.0.1:8080/api/ready
curl -i http://127.0.0.1:8080/api/graph/ready
curl http://127.0.0.1:8080/api/slo
```

Before nodes start, graph readiness must be 503 while service readiness is 200.
After the graph is live, graph readiness must become 200. Stop a node and
confirm it returns to 503 after the heartbeat deadline without restarting the
telemetry service. The SLO is `warming` for its configured minimum window, then
reports `met` or `violated` from measured rolling data.

Validate the provisioned dashboard and alert assets with:

```sh
docker compose -f compose.yaml -f compose.observability.yaml config --quiet
docker run --rm --entrypoint /bin/promtool \
  -v "$PWD/deploy/observability:/etc/prometheus:ro" \
  prom/prometheus:v3.13.0@sha256:c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7 \
  check config /etc/prometheus/prometheus.yml
docker run --rm --entrypoint /bin/promtool \
  -v "$PWD/deploy/observability:/etc/prometheus:ro" \
  -w /etc/prometheus \
  prom/prometheus:v3.13.0@sha256:c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7 \
  test rules alerts.test.yml
```

Static validation is not sufficient. Start the exact combined stack under an
isolated Compose project, require both telemetry and Prometheus to become
healthy, verify Prometheus reports the GraphX target `up`, query
`graphx_service_ready`, and confirm Grafana returns dashboard UID
`graphx-operations`. Then stop that isolated project. The operations regression
script polls every condition under one bounded deadline, rather than assuming
the first Prometheus scrape has completed when its process becomes ready. On a
timeout it emits bounded container, target, and log diagnostics before cleanup.
The checked-in regression scripts perform both the operations-stack and
secure-export checks:

```sh
scripts/test-phase6-operations.sh
scripts/test-phase6-secure-otlp.sh
```

## Phase 7 durable history acceptance

The telemetry test suite covers strict history configuration, unknown/empty
value rejection at full service startup, and query parsing,
bounded queues, count retention, stable pagination, reduced database-cap
startup, a shutdown deadline under a real SQLite writer lock, schema and graph-
ownership rejection, observation authentication, degraded-backend isolation,
abrupt process restart, and record persistence:

```sh
npm test --prefix apps/telemetry
```

The history tests also reduce age/count policies across a database reopen,
verify query-time non-disclosure and idle pruning without a long sleep, and
force an idle maintenance lock failure to confirm isolated degradation.

The web suite mounts the real history component to verify stable, deduplicated
backward pages, paused polling, return-to-newest behavior, token changes, and
stale-response suppression in addition to credential handling:

```sh
npm test --prefix web
```

Validate both the default-disabled projection and the persistent-volume overlay:

```sh
docker compose config --quiet
docker compose -f compose.yaml -f compose.history.yaml config --quiet
```

Then run the isolated live test:

```sh
scripts/test-phase7-history.sh
```

It builds and starts only an isolated telemetry service with an observation
token, waits for service and history readiness, requires an unauthenticated 401,
injects a valid UDP event, retrieves it through the authenticated bounded
history API, restarts that container, and requires an authenticated reread from
the named volume. Its cleanup is scoped to the generated Compose project and
volume.
This proves local SQLite/volume persistence; it does not certify a remote
backend, distributed durability, or end-to-end delivery of best-effort UDP.

## 5. Docker bridge deployment

The user-level smoke test is:

```sh
scripts/demo.sh start
scripts/demo.sh logs       # Ctrl-C leaves the demo running
scripts/demo.sh verify
scripts/demo.sh stop
```

`verify` fails unless all four services run, both TCP edges report connected,
and received counters on both edges increase during a two-second observation.

For exhaustive automated coverage, run `scripts/test-features.sh docker`. For
raw manual inspection:

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

## 6. Native Linux network drivers

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
OVS attachments. The IPvlan L3 demo verifies three independent node subnets in
one external, multi-subnet IPvlan L3 network on the shared parent path. Docker
rejects multiple IPvlan network objects that claim the same parent, so
subnet/IPAM domains—not duplicate parent claims—provide the per-node L3
separation. The mixed demo verifies macvlan-to-ipvlan routing through
10.10.0.1 and 10.20.0.1, forwarding, nftables policy, OVS mirrors and a netem
apply/clear cycle.

During a manual run, use each example's `scripts/status.sh`. Confirm Docker
networks are external and owned by the infrastructure layer, and confirm the
node Compose projects have different project names. Do not interpret failed
host-to-macvlan-container pings as a routing failure: parent-host reachability is
blocked by macvlan design unless a host macvlan shim is added.

## 7. OVS mirrors, capture and fault behavior

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

OVS capture remains real network-packet capture. Separately, the application
PCAPNG sink correlates GraphX frames by message ID (or the documented v1
fallback), packet index, and byte offset.
The current acceptance criterion does not require automatic matching between
those two independent capture files.

## 8. macOS userspace-OVS simulation

On Docker Desktop:

### Local Linux verifier container on macOS

Docker Desktop runs Linux containers inside its Linux VM, so the repository's
Linux verifier image can reproduce Linux process, OpenSSL, socket, and SIGPIPE
behavior while keeping build products out of the macOS checkout.

Run the focused TLS security test first:

```sh
scripts/test-linux-container.sh tls
```

Additional modes are cumulative CTest builds for C++23 and C++20, the portable
Phase 1–7 feature suite, and the pinned quality/fuzz gates:

```sh
scripts/test-linux-container.sh ctest
scripts/test-linux-container.sh portable
GRAPHX_FUZZ_SECONDS=30 scripts/test-linux-container.sh fuzz
scripts/test-linux-container.sh quality
```

Logs are written under `outputs/linux-container/`. The image uses Ubuntu 24.04,
Node 24, OpenSSL 3, and Clang 18. To install an organization root CA into this
local verifier image without adding it to the repository, provide its absolute
path as a BuildKit secret:

```sh
GRAPHX_CA_CERT=/absolute/path/to/company-root-ca.crt \
  scripts/test-linux-container.sh tls
```

If the organization supplies a certificate installation script instead, save
and inspect it locally, then pass that file without piping network content into
the image build:

```sh
GRAPHX_CERT_INSTALL_SCRIPT=/absolute/path/to/install-certs.sh \
  scripts/test-linux-container.sh tls
```

Do not use this container as proof of native macvlan, IPvlan, physical-parent,
OVS, namespace-router, nftables, or netem behavior. Those acceptance gates still
require a dedicated native Linux host. Docker documents macvlan as unsupported
on Docker Desktop for Mac and Windows.

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

For the mixed native-Linux lab, `RTNETLINK answers: File exists` means an earlier
run left one or more named interfaces behind; a dry run never creates them. Run
`examples/mixed-network/scripts/linux-down.sh` and then retry `linux-up.sh`. The
startup helper now detects this state before making changes and rolls back only
resources created by its own failed attempt.

For a failure, retain the failing CTest output, node logs, `graphx inspect`
output, Docker service status, infrastructure status, OVS state and router
routes/qdiscs. That evidence distinguishes a model-validation problem from a
transport, container, or host-networking problem.
