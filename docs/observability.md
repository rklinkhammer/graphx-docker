# Observability

For the end-to-end configuration and verification workflow, see
[telemetry and observability](user-guide.md#9-configure-telemetry).

GraphX emits bounded metrics and trace events for nodes, edges, transports, drops,
errors, reconnects, queue pressure, and latency. The telemetry service combines
those events with normalized topology and runtime evidence.

Current endpoints include liveness, readiness, graph readiness, health, SLO,
topology, metrics, history, capture catalog/download, runtime evidence, and
authorized control. Observation and control credentials are separate. Remote
plaintext binding is rejected unless explicitly enabled; TLS and mutual TLS are
available.

OTLP/HTTP export is asynchronous and bounded by queue size, response size, timeout,
retry, and backoff settings. Export failure degrades the relevant status without
blocking graph traffic. Prometheus and Grafana examples consume the same current
metrics surface.
