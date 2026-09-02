# ADR 0007: Centralized bounded OTLP export and operational health

Status: accepted

## Context

GraphX nodes already produced transport-neutral events and had a small native
OTLP/HTTP trace seam. Production operations require secure remote export,
truthful health checks, measurable SLOs, and reproducible dashboards without
allowing telemetry failure to stall the graph. A full OpenTelemetry SDK in each
C++ process would add dependency and lifecycle complexity, duplicate security
configuration, and make every node a remote egress principal.

## Decision

Runtime nodes continue to send bounded, best-effort, optionally HMAC-authenticated
UDP events to the telemetry service. The service is the remote OTLP/HTTP JSON
egress point and supports HTTPS, bearer authentication, private CAs, and mTLS.
Its queue, request deadline, and response size are bounded; it does not retry a
permanent failure. Retryable connection failures, disconnects, and OTLP/HTTP
429, 502, 503, and 504 responses receive a bounded exponential backoff with
jitter; `Retry-After` is honored up to the configured delay cap. Backoff and
active requests are interrupted by shutdown. The existing native exporter
remains supported only for a loopback collector and rejects remote hosts.

Service readiness means the HTTP and UDP listeners are active and shutdown has
not started. Graph readiness separately means every configured node has a fresh
heartbeat and every edge is connected. Container health uses service readiness.
Graph readiness is sampled into a bounded in-memory SLO window along with edge
counter and latency-histogram deltas. Prometheus metrics are the dashboard and
alert contract; Grafana and Prometheus assets are provisioned from versioned
files.

## Consequences

- Telemetry loss or collector failure cannot block graph processing.
- Remote credentials and TLS policy live at one egress boundary and are not
  placed in `graphx.yaml`.
- An application outage remains observable instead of restarting the telemetry
  process and discarding its evidence.
- UDP remains best effort and the SLO describes events observed by the current
  collector process, not durable accounting.
- Bounded retry reduces loss during transient collector failures but does not
  provide durable delivery; records are still discarded after the configured
  attempt limit or when the bounded pending queue is full.
- W3C propagation, sampling, official SDK features, and durable history are not
  implied by this adapter. Durable history remains Phase 7.
