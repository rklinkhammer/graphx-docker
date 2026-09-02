# GraphX observability

GraphX reports application-level facts about each logical edge independently of
the transport and network-infrastructure models. Runtime nodes emit best-effort
UDP JSON events to the telemetry service. The service exposes the current model
through `/api/topology`, WebSocket snapshots, the browser console, and Prometheus
text at `/metrics`.

When `GRAPHX_TELEMETRY_SHARED_SECRET` is configured on nodes and collector, UDP
events and control messages are HMAC-authenticated with clock and replay checks.
See [`security.md`](security.md) for the trust boundary and resource limits.

## Metric semantics

| Signal | Meaning | Source |
|---|---|---|
| messages sent / received | Envelope send and receive callbacks, kept as separate counters | measured |
| wire bytes sent / received | Framed bytes passed to the transport callback in each direction | measured |
| message and byte throughput | Sent counters in the active five-second collector window | derived |
| mean and p95 latency | Receive-event latency; p95 is estimated from histogram buckets | measured / derived summary |
| errors | Transport or decoding error callback | measured |
| dropped | Explicit drop event or a backpressure-policy rejection | measured |
| rejected | Backpressure events whose policy result is `rejected` | measured |
| reconnects | Reconnect callback after a previously established connection | measured |
| backpressure | Number of waits/rejections and their accumulated duration | measured |
| node CPU | GraphX process CPU consumed between heartbeats, as a percentage of one core | measured |

The console labels unavailable values with `—`. Rates use send events only, so a
normally delivered message is not double-counted when its receive event arrives.
The first few seconds after startup are a warm-up interval; a quiet edge returns
to zero after its last send leaves the five-second window.

The receive-latency histogram uses microsecond bounds of 10, 50, 100, 500,
1,000, 5,000 and 10,000, plus `+Inf`. Prometheus receives conventional
`_bucket`, `_sum`, and `_count` series. The console's p95 value is the upper
bound of the bucket containing the percentile, not a raw percentile sample.

## Trace correlation

An envelope owns a message ID, trace ID, optional parent-message ID, sequence,
subject and timestamp. Send, receive, retry and ordinary transformation retain
the message identity. Explicit derivation creates a new message in the same
trace and records the parent. The recent-message table keys exact records by
message ID and exposes message and trace values as a tooltip. Version-1 events
fall back to trace ID plus sequence because v1 has no message ID. GraphX has two
OTLP/HTTP JSON paths. The native node exporter is a bounded, best-effort seam
for a collector on loopback. The telemetry service is the production-facing
exporter: it converts authenticated UDP events to OTLP traces, periodically
exports current metrics, and supports HTTPS, bearer authentication, a private
CA, and mutual TLS. Neither path blocks graph data.

```sh
export GRAPHX_OTLP_HOST=127.0.0.1
export GRAPHX_OTLP_PORT=4318
export GRAPHX_OTLP_PATH=/v1/traces
```

The native exporter rejects non-loopback hosts because it does not implement
TLS or authentication. Configure remote export on the telemetry service:

```sh
export GRAPHX_OTLP_ENDPOINT=https://otel-collector.example:4318
export GRAPHX_OTLP_AUTH_TOKEN_FILE=/run/secrets/otlp-token
export GRAPHX_OTLP_CA_FILE=/run/secrets/otlp-ca.pem
# Optional mTLS, always configured as a pair:
export GRAPHX_OTLP_CERT_FILE=/run/secrets/otlp-client.pem
export GRAPHX_OTLP_KEY_FILE=/run/secrets/otlp-client-key.pem
```

For Compose, keep host secret paths out of `compose.yaml` and use the checked-in
secure overlays. The first overlay projects the bearer token and private CA;
the second adds optional mTLS:

```sh
export GRAPHX_OTLP_ENDPOINT=https://otel-collector.example:4318
export GRAPHX_OTLP_AUTH_TOKEN_FILE=/host/secrets/otlp-token
export GRAPHX_OTLP_CA_FILE=/host/secrets/otlp-ca.pem
docker compose -f compose.yaml -f compose.otlp-secure.yaml up -d --build

# Add mutual TLS when the receiver requires it:
export GRAPHX_OTLP_CERT_FILE=/host/secrets/otlp-client.pem
export GRAPHX_OTLP_KEY_FILE=/host/secrets/otlp-client-key.pem
docker compose -f compose.yaml -f compose.otlp-secure.yaml \
  -f compose.otlp-mtls.yaml up -d --build
```

The service uses `/v1/traces` and `/v1/metrics`, `application/json`, OTLP's
required hexadecimal trace/span identifiers, and decimal-string nanosecond timestamps.
The queue is bounded by count (default 1,024) and encoded bytes (default 8 MiB),
each request has an absolute elapsed deadline (2 seconds), and responses are
capped (64 KiB, hard maximum 4 MiB). Retryable connection failures,
disconnects, and HTTP 429, 502, 503, and 504 responses receive at most three
attempts by default with exponential backoff and jitter. `Retry-After` is
honored up to the configured five-second delay cap. Permanent responses such as
HTTP 400 are not retried. Retry waiting and active requests are interrupted by
shutdown. Export counters, retry attempts, and queue depth are exposed in
Prometheus and never
include tokens, payloads, or endpoint credentials. Bounded JSON success
responses are validated and partial-success rejections are counted. A plaintext non-loopback
endpoint is rejected unless `GRAPHX_ALLOW_INSECURE_OTLP=true` is deliberately
set. CA, certificate, and key files are each limited to 1 MiB before reading.
The retry policy is configured by `retry_max_attempts` (1–10),
`retry_initial_backoff_ms` (10–60,000), and `retry_max_backoff_ms`
(10–600,000) under `observability.otlp`, or by the corresponding
`GRAPHX_OTLP_RETRY_*` deployment variables. The maximum backoff may not be less
than the initial backoff. This is bounded best-effort resilience, not durable
history.

For v2, the canonical GraphX trace ID is the OTLP trace ID and message/parent IDs
are span attributes. Every exported operation receives a fresh, non-zero 64-bit
span ID; span IDs are not derived from an edge name, sequence number, or message
identity. Span IDs come directly from operating-system entropy and remain fresh
when a process forks after initializing GraphX identity state. Reserved all-zero
entropy results are retried a bounded number of times and then normalized to a
valid non-zero fallback. Legacy free-form trace strings are deterministically
hashed and normalized to a non-zero identifier for export. W3C trace-context
propagation, sampling, span-parent relationships, and a full OpenTelemetry SDK
remain future compatibility work; this adapter implements the bounded
OTLP/HTTP JSON export contract only.

## Health, readiness, and SLOs

The endpoints intentionally answer different questions:

| Endpoint | HTTP status | Meaning | Authentication |
|---|---|---|---|
| `/api/live` | 200 while the event loop serves requests | process liveness | none |
| `/api/ready` | 200/503 | HTTP and UDP listeners are active and shutdown has not begun | none |
| `/api/graph/ready` | 200/503 | every configured node has a fresh running heartbeat and every edge is connected | observation token when configured |
| `/api/health` | always 200 while live | compatibility summary; includes service and graph readiness booleans | none |
| `/api/slo` | 200 | current bounded rolling SLO evaluation | observation token when configured |
| `/api/history/status` | 200 | optional durable backend state and bounded resource counters | observation token when configured |
| `/api/history` | 200/400/429/503 | bounded, filtered, newest-first durable records | observation token when configured |

Docker health checking uses service readiness, not graph readiness, so an
application outage does not cause the collector to restart and erase its
in-memory evidence. SIGTERM/SIGINT immediately make the service unready, stop
timers, close HTTP/WebSocket/UDP endpoints, and abort outstanding OTLP calls.

`observability.slos` defines a 300-second rolling window, 10-second warm-up,
99% graph-availability target, maximum 1% error and drop ratios, and a 10 ms p95
receive-latency target in the example. The evaluator samples once per second,
retains at most `window_seconds + 2` samples, derives counter/histogram deltas,
and reports `warming`, `met`, or `violated`. Prometheus exports this state as
the one-hot `graphx_slo_status` gauge so alerts never treat warm-up as a
violation. The evaluator itself always begins a new live window after restart;
when Phase 7 history is enabled, prior evaluations remain available as read-only
evidence but are not replayed into the new window. Error and drop ratios divide observed errors/drops by sent
plus received events. A quiet graph has zero error/drop ratios and no latency
violation, while availability continues to be evaluated.

## Inspecting the live service

```sh
curl http://localhost:8080/api/topology
curl http://localhost:8080/metrics
curl http://localhost:8080/api/graph/ready
curl http://localhost:8080/api/slo
curl http://localhost:8080/api/history/status
curl 'http://localhost:8080/api/history?limit=100&node=generator'
```

Useful Prometheus series include:

- `graphx_edge_messages_total{edge="samples",direction="sent|received"}`;
- `graphx_edge_wire_bytes_total{edge="samples",direction="sent|received"}`;
- `graphx_edge_message_rate` and `graphx_edge_wire_byte_rate`;
- `graphx_edge_latency_seconds_bucket`, `_sum`, and `_count`;
- `graphx_edge_errors_total`, `graphx_edge_dropped_total`, and
  `graphx_edge_rejected_total`;
- `graphx_edge_reconnects_total`, `graphx_edge_backpressure_events_total`, and
  `graphx_edge_backpressure_seconds_total`;
- `graphx_edge_connected` and `graphx_node_cpu_percent`.
- `graphx_service_ready`, `graphx_graph_ready`, `graphx_slo_met`,
  `graphx_slo_status{status="warming|met|violated"}`,
  `graphx_slo_ratio`, and `graphx_slo_latency_seconds`;
- `graphx_otlp_exports_total{outcome="exported|failed|retried|dropped|rejected"}` and
  `graphx_otlp_queue_depth`.
- `graphx_history_enabled`, `graphx_history_backend_up`,
  `graphx_history_records_total{outcome="written|failed|dropped|pruned"}`,
  `graphx_history_queue_depth`, `graphx_history_queue_bytes`, and
  `graphx_history_database_bytes`.

## Provisioned operations stack

Prometheus rules and a version-controlled Grafana dashboard live under
`deploy/observability`. Start the optional, loopback-published stack with:

```sh
docker compose -f compose.yaml -f compose.observability.yaml up -d --build
```

Grafana is at `http://127.0.0.1:3000` and Prometheus at
`http://127.0.0.1:9090`. Set a strong `GRAPHX_GRAFANA_ADMIN_PASSWORD` outside a
local lab. The dashboard covers graph readiness, SLO state and targets,
throughput, p95 latency, errors/drops, node CPU, OTLP exporter health, and
durable-history health, outcomes, queue bytes, and storage bytes.
Alerts cover sustained graph unavailability, SLO violation, export failure,
export queue loss, and an enabled but unavailable history backend. If observation authentication protects `/metrics`, add a
Prometheus bearer-token file through a deployment-specific secret projection;
do not put the token in `prometheus.yml`.

The implementation follows the OpenTelemetry
[OTLP specification](https://opentelemetry.io/docs/specs/otlp/) and
[resource semantic conventions](https://opentelemetry.io/docs/specs/semconv/resource/),
Prometheus [recording and alerting rule](https://prometheus.io/docs/prometheus/latest/configuration/recording_rules/)
syntax, and Grafana's [provisioning model](https://grafana.com/docs/grafana/latest/administration/provisioning/).

Authenticated `POST /api/control/reset` resets collector-side edge aggregation while retaining
the current connection state. Collector restart also resets these in-memory
counters. UDP delivery is intentionally best effort, so the values describe
events observed by this collector; Phase 7 can retain those observations, but
does not turn best-effort delivery into durable end-to-end accounting. See
[`history.md`](history.md).

## Authenticated runtime control

Set both `GRAPHX_CONTROL_TOKEN` and `GRAPHX_TELEMETRY_SHARED_SECRET` on the
telemetry service, and the shared secret on every runtime, to enable
pause/resume/reset. Each credential must contain at least 32 bytes. Calls must
use `Authorization: Bearer <token>`:

```sh
curl -i -X POST -H 'Authorization: Bearer choose-a-control-token-at-least-32-bytes' \
  http://localhost:8080/api/control/pause
curl -i -X POST -H 'Authorization: Bearer choose-a-control-token-at-least-32-bytes' \
  http://localhost:8080/api/control/resume
```

HTTP 202 means the command was sent to at least one recently active runtime;
the snapshot's `control.acknowledgements` records authenticated replies. HTTP 401 means the
credential is absent or wrong, 409 means no runtime endpoint is live, and 503
means server-side control is disabled. HMAC authentication, timestamp freshness,
and nonce replay rejection protect the datagram exchange. Pause currently acts
at source nodes: it prevents new envelopes while downstream nodes drain. It is
not a process suspension, durable queue, or distributed transaction.

## Packet capture boundary

OVS SPAN ports and the example `tcpdump`/`dumpcap` hooks collect standard
Ethernet packets now.
The existing PCAPNG sink records canonical application frames and reports
packet indexes/offsets through telemetry. The console can download available
files, and the extcap adapter can follow one in Wireshark. See
[`capture.md`](capture.md). Automated matching to separate OVS packet captures
remains future hardening.
