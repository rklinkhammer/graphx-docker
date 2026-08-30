# GraphX observability

GraphX reports application-level facts about each logical edge independently of
the transport and network-infrastructure models. Runtime nodes emit best-effort
UDP JSON events to the telemetry service. The service exposes the current model
through `/api/topology`, WebSocket snapshots, the browser console, and Prometheus
text at `/metrics`.

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

An envelope owns a trace ID, sequence, subject and timestamp. Send, receive and
processing callbacks retain that identity. The recent-message table displays a
short trace ID and exposes the full value as a tooltip. When an OTLP destination
is configured, GraphX asynchronously exports `graphx.send`, `graphx.receive`,
`graphx.process`, and error spans without blocking the data path.

```sh
export GRAPHX_OTLP_HOST=127.0.0.1
export GRAPHX_OTLP_PORT=4318
export GRAPHX_OTLP_PATH=/v1/traces
```

W3C trace-context propagation, exporter retry/batching conformance, and richer
span relationships remain Phase 5 follow-ups.

## Inspecting the live service

```sh
curl http://localhost:8080/api/topology
curl http://localhost:8080/metrics
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

`POST /api/control/reset` resets collector-side edge aggregation while retaining
the current connection state. Collector restart also resets these in-memory
counters. UDP delivery is intentionally best effort, so the values describe
events observed by this collector; they are not durable accounting records.

## Packet capture boundary

OVS SPAN ports and the example `tcpdump`/`dumpcap` hooks can collect packets now.
Phase 6 adds the Wireshark-facing layer: PCAPNG generation, extcap control,
packet offsets, and reliable packet-to-envelope correlation. Until then the
console shows live application trace IDs but keeps **Open capture** disabled.
