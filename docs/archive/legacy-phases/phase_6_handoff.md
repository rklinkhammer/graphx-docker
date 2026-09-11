# Phase 6 implementation handoff

Date: 2026-09-01

Work package: OpenTelemetry integration, health checks, SLOs, and operational dashboards

## 1. Outcome summary

Phase 6, including the remediation required by `phase_6_verification.md`, is
implemented and ready for independent reverification. GraphX now has:

- a secure, bounded OTLP/HTTP JSON egress path in the telemetry service for
  traces and metric snapshots;
- truthful process liveness, telemetry-service readiness, and graph readiness
  endpoints with intentionally different semantics;
- a bounded rolling in-memory SLO evaluator with explicit availability, error,
  drop, and p95 latency objectives;
- Prometheus operational metrics and five alert rules;
- a version-controlled Grafana operations dashboard and reproducible
  provisioning;
- a telemetry container health check and graceful telemetry shutdown; and
- typed configuration, schema validation, negative tests, operational docs, and
  an ADR for the new architecture.

The remediation closes all reported P1/P2 findings: the exact Prometheus
Compose service now starts on a bounded writable tmpfs; OTLP has a real elapsed
deadline and bounded standards-aligned retry; empty deployment values no longer
disable YAML configuration; checked-in overlays project token/CA/mTLS files;
SLO alerts distinguish warm-up from violation; operational integers are
strictly validated; and dashboard panels no longer mix units.

The subsequent verifier P2/P3 findings are also remediated. The live operations
script now polls service health, first scrape, readiness query, rule load, and
dashboard provisioning under one deadline and emits bounded diagnostics on
timeout. Fallback OTLP IDs cannot be all zero, even under forced zero entropy,
and committed tests cover collector refusal/recovery and queue pressure during
retry backoff.

The implementation does not introduce durable telemetry history (Phase 7), a
new authorization/control model (Phase 8), or new capture/dissector behavior
(Phase 9).

## 2. Concrete requirements and acceptance criteria

The work package was translated into these concrete requirements before editing:

| Requirement | Acceptance criterion | Result |
|---|---|---|
| OTLP traces | Valid OTLP/HTTP JSON reaches a receiver without blocking graph work; canonical trace identity is retained and span IDs are valid and unique | Met |
| OTLP metrics | Periodic resource/scope metric requests include graph, edge, node, and SLO operational state | Met |
| Secure remote export | HTTPS, bearer token, private CA, and optional mTLS configuration; no credentials in `graphx.yaml`; plaintext remote export rejected by default | Met by live private-CA/mTLS test and checked-in Compose secret overlays |
| Bounded failure behavior | Explicit item, byte, absolute request-time, response-size, retry-attempt, and backoff limits; queue overflow and receiver failure cannot block graph processing | Met |
| Truthful health | Liveness, service readiness, and application graph readiness are separate and return meaningful status codes | Met |
| Graceful shutdown | Readiness drops before HTTP/WebSocket/UDP/exporter shutdown; outstanding exporter requests are aborted | Met |
| Measurable SLOs | Objectives, formulas, units, warm-up, rolling window, reset handling, and resource bounds are explicit | Met |
| Operations metrics | Stable Prometheus names, conventional units, fixed-cardinality labels derived only from validated topology IDs | Met |
| Alerts/dashboard | Provisioned rules and dashboard cover readiness, SLOs, traffic, latency, errors/drops, CPU, and exporter health | Met |
| Deployment | Telemetry health check and optional pinned Prometheus/Grafana projection validate and run without extra privileges | Met |
| Compatibility | Configuration version and wire envelope unchanged; `/api/health` retained; native exporter remains available for loopback collectors | Met |
| Documentation/tests | README, technical docs, procedure, ADR, unit/integration/negative/container checks agree with implementation | Met |

## 3. Reused components, invariants, and decisions

### Reused components

- The transport-neutral `TraceSink` and canonical envelope trace/message
  identities remain the source of runtime events.
- The Phase 5 HMAC-authenticated UDP path remains the node-to-telemetry trust
  boundary.
- Existing edge counters, latency buckets, heartbeat state, Prometheus handler,
  and observation bearer are extended rather than duplicated.
- The existing native `OtlpHttpTraceSink` remains a small local-collector seam.
- The existing strict C++ configuration loader and JSON schema now type and
  bound `observability.otlp` and `observability.slos`.

### Important invariants

- Telemetry failure never blocks, stops, or backpressures graph data.
- Credentials and private-key paths are deployment inputs, never graph model
  values or telemetry output.
- Health endpoints do not conflate a live process, ready listeners, and a ready
  application graph.
- Container health uses service readiness so a graph outage does not restart
  the collector and erase its evidence.
- Export queues are bounded by both record count and encoded bytes.
- SLO sample history is bounded to `window_seconds + 2` and is not durable.
- Metric units are not mixed: ratios and latency seconds have separate series.
- Validated graph IDs are the only dynamic Prometheus label values.
- No payload body is exported as a span attribute or diagnostic.

### Architecture and compatibility decisions

ADR 0007 records the consequential decision: remote OTLP egress is centralized
in the telemetry service. This avoids making every C++ node a TLS and credential
principal. The native exporter now rejects non-loopback hosts rather than
appearing secure when it is plaintext-only.

The centralized adapter follows OTLP/HTTP JSON rules: lower-camel-case fields,
hexadecimal trace/span IDs, integer enum values, decimal-string 64-bit values,
`application/json`, and `/v1/traces` plus `/v1/metrics`. It validates bounded
success responses and counts partial-success rejections. Retryable connection
failures, disconnects, and HTTP 429/502/503/504 responses use bounded
exponential backoff with jitter and capped `Retry-After`; permanent failures are
not retried. Attempt count, pending queue, encoded bytes, elapsed request time,
response bytes, and delay are all bounded. This improves transient resilience
without implementing Phase 7 durability.

`/api/health` remains a 200 compatibility summary. New endpoints are:

- `GET /api/live`: process/event-loop liveness, unauthenticated;
- `GET /api/ready`: HTTP+UDP service readiness, unauthenticated;
- `GET /api/graph/ready`: all nodes fresh/running and all edges connected,
  observation-authenticated when configured, 200/503;
- `GET /api/slo`: rolling evaluation, observation-authenticated when configured.

## 4. Major components changed

| Component | Evidence |
|---|---|
| OTLP configuration/exporter, readiness, SLO evaluator, OTLP encoders | `apps/telemetry/operations.mjs` |
| API integration, metrics, timers, graceful shutdown | `apps/telemetry/server.mjs` |
| Adversarial and live UDP-to-OTLP tests | `apps/telemetry/operations.test.mjs`, `apps/telemetry/security.test.mjs` |
| Typed C++ model/parser and negative tests | `include/graphx/config.hpp`, `src/config.cpp`, `tests/test_config.cpp` |
| Authoritative model/schema | `graphx.yaml`, `config/schema/graphx.schema.json` |
| Native exporter security boundary | `apps/common.hpp` |
| Telemetry image and Compose health | `docker/telemetry.Dockerfile`, `compose.yaml` |
| Optional operations deployment | `compose.observability.yaml`, `deploy/observability/` |
| Secure OTLP file projection | `compose.otlp-secure.yaml`, `compose.otlp-mtls.yaml` |
| Prometheus rules | `deploy/observability/alerts.yml` |
| Grafana provisioning/dashboard | `deploy/observability/grafana/` |
| Architecture and operations docs | `docs/adr/0007-centralized-bounded-otlp-and-operational-health.md`, `docs/observability.md`, `docs/test-procedure.md`, `README.md` |
| Pinned image/license inventory | `THIRD_PARTY.md` |

The pre-existing edits to `prompt/implement.md` and `prompt/verifier.md` were
preserved and not used as implementation output.

## 5. Limits and failure behavior

Defaults, all strictly validated:

| Limit | Default | Allowed range / behavior |
|---|---:|---|
| OTLP pending records | 1,024 | 1–65,536; newest enqueue is dropped when full |
| OTLP pending encoded bytes | 8 MiB | 64 KiB–64 MiB; oversized/newest enqueue is dropped |
| OTLP request timeout | 2 s | 100 ms–60 s; active request is destroyed on deadline |
| OTLP response body | 64 KiB | 1 KiB–4 MiB; excess is a non-successful export |
| OTLP attempts | 3 | 1–10; only transient failures retry |
| OTLP initial retry backoff | 200 ms | 10–60,000 ms; exponential with jitter |
| OTLP maximum retry backoff | 5 s | 10–600,000 ms; also caps `Retry-After` |
| OTLP CA/certificate/key file | not set | at most 1 MiB per file |
| Metric export interval | 5 s | 250 ms–600 s |
| SLO window | 300 s | 10–3,600 s |
| SLO minimum warm-up | 10 s | 1 s through configured window |
| SLO samples | 302 by default | at most `window_seconds + 2` |
| Telemetry input | Existing 16 KiB datagram boundary | malformed/untrusted datagrams ignored after authentication and validation |

OTLP endpoints must be HTTP(S) origins without credentials, query strings,
fragments, or embedded signal paths. Signal paths are independently validated.
Plaintext non-loopback export requires the explicit
`GRAPHX_ALLOW_INSECURE_OTLP=true` escape hatch. Bearer tokens use the existing
bounded secret loader and may be read from `GRAPHX_OTLP_AUTH_TOKEN_FILE`.
Client certificate and key must be supplied together.

## 6. SLO definitions

The evaluator samples once per second:

- availability = ready graph samples / all graph samples;
- error ratio = observed error increments / (send + receive increments);
- drop ratio = observed drop increments / (send + receive increments);
- p95 latency = upper bound of the p95 bucket over receive-latency increments.

Default objectives are availability at least 0.99, error ratio at most 0.01,
drop ratio at most 0.01, and p95 latency at most 10,000 microseconds. Counter
resets are treated as a new counter epoch rather than producing a negative or
silent multi-minute delta. Quiet graphs have zero error/drop ratios and no
latency violation, while graph availability continues to be evaluated. Status
is `warming`, `met`, or `violated`; `graphx_slo_status` exports those states
separately so the critical rule cannot page during warm-up.

## 7. Verification performed

### Native clean build and complete tests

```text
cmake -S . -B build/phase6-remediation-clean -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DGRAPHX_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/phase6-remediation-clean -j 4
ctest --test-dir build/phase6-remediation-clean --output-on-failure
```

Result: configure/build passed with AppleClang 21 and OpenSSL 3.6.3; 12/12 tests
passed in the final run.

### ASan/UBSan

```text
cmake -S . -B build/phase6-remediation-sanitizers -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DGRAPHX_BUILD_TESTS=ON -DGRAPHX_ENABLE_SANITIZERS=ON -DCMAKE_CXX_STANDARD=23
cmake --build build/phase6-remediation-sanitizers -j 4
ctest --test-dir build/phase6-remediation-sanitizers --output-on-failure
```

Result: 13/13 tests passed, including sanitizer coverage audit.

### Static analysis and formatting

```text
cmake -S . -B build/phase6-cppcheck ... -DGRAPHX_ENABLE_CPPCHECK=ON
cmake --build build/phase6-cppcheck -j 4
```

Result: cppcheck-enabled build passed without findings.

`clang-format --dry-run --Werror` passed for all modified C++ files using the
installed clang-format 23.1.0. The authoritative `scripts/check-format.sh`
could not complete because it requires clang-format 18.x. It exited 2 and
reported the installed 23.1.0 version. `scripts/run-static-analysis.sh` exited
2 before analysis because `clang-tidy` is not installed. These are unverified
exact-tool gates, not passes.

### Telemetry, API, OTLP, and web

```text
npm test                         # apps/telemetry
scripts/test-features.sh portable
npm audit --omit=dev             # apps/telemetry
npm audit                        # web
```

Final results:

- telemetry: 24/24 tests passed;
- web: 3/3 tests passed;
- Vite production build passed (existing 1.85 MB chunk-size warning remains);
- portable C++23 and C++20 runs: 12/12 CTest tests passed in each;
- finite TCP and shared-memory pipelines, coordinated SIGTERM, authenticated
  pause/resume, HTTPS/API security, topology validation, and infrastructure
  dry-runs passed;
- both npm audits reported 0 vulnerabilities.

The first full portable-suite attempt delivered all eight shared-memory demo
messages but did not finish its pre-existing demo shutdown path after `TERM`.
Only that verifier-owned process was force-stopped. An immediate isolated run of
the same shared-memory example passed, and a complete portable-suite rerun then
passed. No Phase 6 code changes touch the shared-memory transport, but the
one-off shutdown behavior is retained here for independent regression focus.

The telemetry suite covers valid OTLP JSON mapping, unique and forced-zero
fallback span IDs, SLO
warm-up/evaluation/reset/window bounds, strict operational integers, empty-env
precedence, secure configuration rejection, live private-CA/mTLS acceptance and
missing-certificate rejection, item and byte queue bounds, absolute slow-drip
deadline, transient/permanent retry classification, `Retry-After`, collector
refusal followed by recovery, queue pressure during retry backoff, interrupted
retry shutdown, oversized receiver response, partial success, bearer
forwarding, malformed HTTP targets, observation authorization, and a real child
telemetry service converting a validated UDP trace event into an authenticated
OTLP request.

### Compose, containers, Prometheus, and Grafana

```text
docker compose -f compose.yaml -f compose.observability.yaml config --quiet
docker compose build telemetry
docker run --rm --entrypoint /bin/promtool ... check config /etc/prometheus/prometheus.yml
```

Results:

- combined Compose validation passed;
- telemetry image built successfully, including the new operations module;
- an isolated final telemetry container became Docker `healthy` from
  `/api/ready`; `/api/ready` returned 200 while `/api/graph/ready` correctly
  returned 503 before nodes started;
- Prometheus 3.13.0 validated its config and all 5 rules;
- `promtool test rules` passed warm-up and evaluated-violation alert cases;
- an isolated live Prometheus instance scraped the telemetry container with
  target health `up` and returned `graphx_service_ready = 1`;
- Grafana 13.2.0 started successfully, reported database health `ok`, provisioned
  the Prometheus datasource, and returned the `GraphX Operations` dashboard at
  stable UID `graphx-operations` through its search API;
- dashboard JSON parsed with `jq`;
- all temporary validation containers and network were removed; the three
  pre-existing `gx-*` containers remained running and untouched.

The checked-in `scripts/test-phase6-operations.sh` polls the exact live stack,
scrape, query, rule, and dashboard checks under one 60-second deadline. It
passed twice consecutively after the verifier-requested fix, including a
cached-image run, and both isolated projects were removed. The separate
`scripts/test-phase6-secure-otlp.sh` generated ephemeral credentials, mounted
the token/private CA/client certificate/client key through the secure overlays,
and completed an authenticated mTLS metric export again after the remediation.
Both scripts remove their isolated Compose resources and ephemeral credentials.

### Fuzzing

`GRAPHX_FUZZ_SECONDS=2 scripts/run-fuzz.sh` exited 1 and could not link either existing fuzz
target because the installed Apple command-line tools do not contain
`libclang_rt.fuzzer_osx.a`. The failure occurred before fuzz execution and is
environmental. Phase 6 did not change the envelope or frame parsing boundaries,
but this check is recorded as unverified rather than passed.

## 8. Known limitations and verifier focus

1. The adapter is intentionally not represented as a full OpenTelemetry SDK.
   It does not implement W3C trace-context propagation, sampling, true parent
   span relationships, compression, or exporter batching.
2. SLO history and collector counters reset when the telemetry process restarts.
   Durable/backend-driven history is Phase 7.
3. Prometheus scraping with `GRAPHX_OBSERVATION_TOKEN` enabled requires a
   deployment-specific bearer-token file projection; the checked-in default
   stack is for the default unauthenticated internal metrics endpoint.
4. Exact clang-format-18, clang-tidy, and libFuzzer jobs still need a CI/Linux
   environment with the repository's required toolchain.
5. The existing web bundle-size warning is unchanged and does not block Phase 6.
6. Longer exporter-recovery soak testing remains useful; deterministic tests
   now cover collector refusal/recovery and retry queue pressure.

## 9. Acceptance checklist

- [x] OTLP trace and metric requests follow the current official JSON mapping.
- [x] Canonical GraphX trace IDs survive export; fallback span IDs use
  operating-system entropy and a bounded non-zero safeguard for reserved output.
- [x] Remote OTLP TLS/auth inputs are outside the graph model and never exposed.
- [x] Queue item count, queue bytes, request deadline, and response bytes are
  explicit and enforced.
- [x] Export failures, drops, partial rejections, and queue usage are observable.
- [x] Retry attempts, exponential delay, jitter, permanent/transient
  classification, `Retry-After`, and shutdown interruption are bounded and tested.
- [x] Telemetry failure remains isolated from the graph data path.
- [x] Liveness, service readiness, and graph readiness are distinct.
- [x] Container health checks service readiness.
- [x] Shutdown makes the service unready and releases owned network resources.
- [x] SLO objectives and formulas are measurable, bounded, documented, and
  covered for warm-up, violation, reset, and rolling-window behavior.
- [x] Prometheus rules and Grafana dashboard are version controlled and live
  provisioned successfully.
- [x] Configuration, schema, examples, documentation, and behavior agree.
- [x] Existing configuration version, envelope format, transport interfaces,
  API compatibility endpoint, and security defaults are preserved.
- [x] No Phase 7 persistence or Phase 8 authorization expansion was introduced.
- [x] Clean build, CTest, sanitizers, cppcheck, web, API, Compose, container,
  Prometheus, Grafana, and dependency audit checks passed as recorded.
- [ ] Exact clang-format-18, clang-tidy, and libFuzzer execution require the CI
  toolchain described above.

## 10. Recommended next work package

After independent Phase 6 verification accepts this implementation, proceed to
Phase 7: durable or backend-driven telemetry history. It must preserve the
Phase 6 security boundary, never make graph progress depend on storage, retain
bounded ingestion/backpressure behavior, define retention and migration rules,
keep live readiness distinct from historical backend health, and avoid changing
the GraphX envelope or transport contracts.
