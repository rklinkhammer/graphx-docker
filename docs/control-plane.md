# GraphX authorized control plane

Phase 8 turns the earlier demo pause button into a bounded, attributable runtime
control path. Observation remains read-only and uses
`GRAPHX_OBSERVATION_TOKEN`; operators use separate control principals.

## Policy and credentials

Set `GRAPHX_CONTROL_POLICY_FILE` to a version-1 JSON policy. Each token file must
contain 32–4,096 bytes and may have one trailing newline. Paths may be absolute
or relative to the policy file. Authentication headers are never copied into
commands, logs, or audit records.

```json
{
  "version": 1,
  "principals": [
    {"id":"source-operator","token_file":"source.token",
     "permissions":["pause","resume"],"nodes":["generator"]},
    {"id":"control-admin","token_file":"admin.token",
     "permissions":["pause","resume","reset","commands:read:any","audit:read"],
     "nodes":["*"]}
  ]
}
```

The policy is capped at 64 KiB and 64 unique principals. IDs, properties,
actions, topology node references, token lengths, and duplicate tokens are
validated strictly. In addition, the collector fingerprints every active
observation token, control-principal token, shared telemetry HMAC, and per-node
runtime HMAC and rejects reuse across any two roles. Fingerprints and credential
values are never logged. Replace policy/token files atomically. Ordinary HTTP
authentication checks the policy at most once per second. In addition, every
valid signed telemetry datagram forces one bounded combined credential reload
before any event value can be retained or exported. An invalid or missing
replacement disables all control and authenticated runtime ingestion until one
mutually distinct credential snapshot can be published. `/api/ready`,
`graphx_control_policy_valid`, `graphx_service_ready`, and the
`GraphXControlPolicyInvalid` alert expose this state. Diagnostics name only the
colliding roles.

For compatibility, `GRAPHX_CONTROL_TOKEN` creates a `legacy-operator` with all
permissions and all nodes when no policy file is configured. Configuring both
operator credential models is rejected. This legacy mode requires one
`GRAPHX_TELEMETRY_SHARED_SECRET` trust-domain secret.

Policy mode instead requires `GRAPHX_RUNTIME_IDENTITY_FILE`, a version-1 manifest
with exactly one distinct secret for every configured runtime node. A runtime
receives only its own secret; the collector receives the manifest and all node
secret files. Missing, duplicate, unknown, or unreadable identities disable
control and authenticated telemetry fails closed. This prevents one compromised
runtime from registering as another node and receiving or forging its commands.
The identity manifest is bounded to 64 KiB and is rechecked at most once per
second. Runtime processes must restart when their secret rotates.

Signed telemetry remains untrusted after authentication. The collector bounds
diagnostic text to 256 characters, accepts connection state only as
`disconnected`, `connecting`, `listening`, `connected`, `closed`, or `error`,
and accepts backpressure state only as `blocked` or `rejected`. Before any event
is sent to snapshots/WebSockets, OTLP, durable history, or capture-reference
state, unknown properties are removed and direct or embedded occurrences of
active, newly discovered candidate, and recently superseded GraphX credentials
are replaced with `[credential-redacted]`.
Successful rotation retains superseded values in the redaction set for 60
seconds, covering the HMAC freshness/replay interval and in-flight events. The
overlap is capped at 4,096 retired values; exceeding that defensive limit fails
the combined credential configuration closed until expired values are pruned.
The pre-fan-out reload means a valid event signed immediately after an atomic
file replacement is filtered against the candidate value even inside the
ordinary one-second authentication polling interval.
This prevents accidental credential retention; it does not make runtime-provided
operational metadata non-sensitive or prevent an authenticated malicious runtime
from encoding information in otherwise valid values.

### Restart-safe coordinated rotation

The automatic retired-value set is process memory. A deployment that might
restart the collector during rotation must additionally set
`GRAPHX_PREVIOUS_CREDENTIALS_FILE` to a deployment-projected version-1 JSON
manifest. The manifest supplies only redaction values; it never grants HTTP or
HMAC authentication.

```json
{
  "version": 1,
  "expires_at": "2026-09-03T20:30:00.000Z",
  "credentials": [
    {"role":"control_principal","id":"control-admin",
     "secret_file":"/run/secrets/graphx-previous-control-admin-token"},
    {"role":"runtime_identity","id":"generator",
     "secret_file":"/run/secrets/graphx-previous-runtime-generator-secret"}
  ]
}
```

`expires_at` must be a canonical UTC timestamp no more than 60 seconds in the
future when loaded. An expired manifest contributes no values. An unexpired
manifest needs at least one credential and accepts at most 4,096; the manifest
is capped at 64 KiB and each secret at 4,096 bytes with a 32-byte minimum. Roles
are `observation`, `shared_telemetry`, `control_principal`, or
`runtime_identity`. IDs use the same bounded identifier syntax as policy IDs.
The singleton roles use reserved IDs `observation` and `shared`; control and
runtime entries use the principal or topology-node ID they replace.
Manifest and secret files must be regular files and must not be group- or
world-writable. Invalid, unreadable, over-capacity, or cross-role-colliding
transition state fails readiness, control, and authenticated ingestion closed
without logging a value. A previous value may equal the staged current value
only when both identify the same logical role.

Use this order; all file publications are atomic replacements:

1. Copy the exact old values into separate protected previous-value files.
2. Publish an unexpired previous-credential manifest while the old current
   values are still active. Set expiry for at most 60 seconds from publication.
3. Publish the new current token files and runtime identity files/manifests.
   Restart each runtime whose HMAC changed.
4. The collector may reload or restart during the overlap. Current values alone
   authenticate; current and projected previous values are filtered.
5. After expiry, remove the previous files and unset the manifest projection.

Do not overwrite an old current file until its value exists in the previous
projection. Do not extend the expiry by repeatedly rewriting the manifest.
`config/previous-credentials.example.json` documents the format with an
intentionally expired timestamp. The optional
`compose.control-rotation.yaml` overlay demonstrates an admin/generator
rotation; generate a fresh manifest and provide the three required host paths
before using it:

```sh
export GRAPHX_PREVIOUS_CREDENTIALS_MANIFEST_FILE=/secure/previous-credentials.json
export GRAPHX_PREVIOUS_CONTROL_ADMIN_TOKEN_FILE=/secure/previous-admin.token
export GRAPHX_PREVIOUS_RUNTIME_GENERATOR_SECRET_FILE=/secure/previous-generator.hmac
docker compose -f compose.yaml -f compose.control.yaml \
  -f compose.control-rotation.yaml config --quiet
```

The Compose overlay projects two operator tokens and three runtime identities:

```sh
printf '%s' 'a-random-source-token-at-least-32-bytes' > /secure/source.token
printf '%s' 'a-random-admin-token-at-least-32-bytes' > /secure/admin.token
printf '%s' 'a-distinct-generator-secret-at-least-32-bytes' > /secure/generator.hmac
printf '%s' 'a-distinct-transform-secret-at-least-32-bytes' > /secure/transform.hmac
printf '%s' 'a-distinct-sink-secret-at-least-32-bytes' > /secure/sink.hmac
export GRAPHX_CONTROL_SOURCE_TOKEN_FILE=/secure/source.token
export GRAPHX_CONTROL_ADMIN_TOKEN_FILE=/secure/admin.token
export GRAPHX_RUNTIME_GENERATOR_SECRET_FILE=/secure/generator.hmac
export GRAPHX_RUNTIME_TRANSFORM_SECRET_FILE=/secure/transform.hmac
export GRAPHX_RUNTIME_SINK_SECRET_FILE=/secure/sink.hmac
docker compose -f compose.yaml -f compose.control.yaml up -d --build
```

Use restrictive file permissions and a secret manager in production. The
checked-in policy contains paths and authorization shape only, never secrets.

## Command API

Create a command with an actor-scoped idempotency key:

```sh
curl -sS -X POST http://127.0.0.1:8080/api/control/commands \
  -H "Authorization: Bearer $GRAPHX_OPERATOR_TOKEN" \
  -H 'Content-Type: application/json' \
  -H "Idempotency-Key: $(openssl rand -hex 16)" \
  --data '{"action":"pause","targetNodes":["generator"],"reason":"maintenance"}'
```

`action` is `pause`, `resume`, or `reset`. Omitted pause/resume targets mean all
configured source nodes. Reset always targets the collector; it clears current
aggregation but not durable history. Reasons are optional 1–256 character audit
metadata. A reason is retained in audit/history, so it must not contain secrets;
the service rejects a reason containing any active, in-process retired, or
projected previous GraphX bearer or HMAC credential. Bodies default to a 4 KiB maximum, unknown properties are rejected,
and state-changing requests remain origin checked and rate limited.

The response returns a command UUID. HTTP 202 means pause/resume was issued, not
that the runtime applied it. Query status with the issuing credential:

```sh
curl -sS -H "Authorization: Bearer $GRAPHX_OPERATOR_TOKEN" \
  http://127.0.0.1:8080/api/control/commands/COMMAND_UUID
```

A principal with `commands:read:any` may list or fetch every actor's commands;
otherwise it sees only its own. A principal with `audit:read` may read live audit
at `/api/control/audit?limit=100` and durable history at
`/api/control/audit/history?limit=100`. Observation-authorized `/api/history`
always excludes `control_audit`, even if that kind is requested, and observation
snapshots contain no command summary. The legacy bodyless POST routes remain
available at `/api/control/pause`, `/resume`, and `/reset`.

## Semantics and limits

| Setting | Default | Range |
|---|---:|---:|
| `command_timeout_ms` | 2,000 | 100–30,000 |
| `command_retention_seconds` | 3,600 | 60–86,400 |
| `max_commands` | 1,024 | 1–10,000 |
| `max_audit_records` | 4,096 | 10–100,000 |
| `idempotency_ttl_seconds` | 3,600 | 60–86,400 |
| `max_request_bytes` | 4,096 | 256–16,384 |

An exact retry with the same actor and `Idempotency-Key` returns the original
command. Reusing that key for a different action, targets, or reason returns
409. A valid signed acknowledgement must match the command UUID, action, and
target node and the live UDP endpoint to which that command was delivered.
Rejected acknowledgements produce `rejected`; absent or late ones
produce `timed-out`. Forged, replayed, stale, wrong-node, and unknown-command
acknowledgements do not mutate control state. In policy mode each node's HMAC
identity is distinct; endpoint matching remains defense in depth.

A negative acknowledgement may supply only a protocol error code: `busy`,
`invalid-state`, `policy-denied`, `runtime-error`, or `unsupported-action`.
The collector maps every other error value to `runtime-rejected` before it can
enter command state, an API response, audit, history, or a log. This deliberately
discards arbitrary authenticated runtime text: authentication proves which
runtime sent a packet, not that its contents are safe to retain.

Pause stops new source production while in-flight work drains; resume restarts
production. It does not suspend the process, queues, transformation, sink, or
network. Commands do not survive collector restart and are not automatically
retried. When Phase 7 history is enabled, credential-filtered `control_audit`
metadata does survive and can be queried only through the control-audit API.

Metrics include command outcomes, denials, pending commands, policy validity,
and dropped in-memory audits. Grafana panels and alerts cover policy/identity
failure and command timeout. The web console uses the command endpoint, polls
the actor-authorized command resource until `accepted`, `rejected`, or
`timed-out`, and updates effective pause state only from runtime acknowledgement.

## Validation

```sh
cmake --build --preset dev --parallel
ctest --preset dev --output-on-failure
(cd apps/telemetry && npm test)
(cd web && npm test && npm run build)
scripts/test-features.sh portable
```

The native C++ test proves the actual source-side pause flag changes only for a
fresh signed exact-target command and returns a correlated signed ACK. The Node
integration test starts the real collector and proves authentication denial,
scope denial, command identity, acknowledgement, replay, idempotency conflict,
forged wrong-node ACK rejection, timeout, audit authorization, and redaction.
